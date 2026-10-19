#include "ui/Language.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <cctype>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <SDL.h>
#include <nlohmann/json.hpp>

#include "ui/Platform.hpp"
#include "ui/Ui.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace bitzkey::ui {

namespace {

const std::vector<LanguageName> LANGUAGES = {
    {"en", "English"}, {"fr", "Français"}, {"it", "Italiano"}, {"es", "Español"}, {"de", "Deutsch"},
    {"pt", "Português"}, {"pl", "Polski"}, {"uk", "Українська"}, {"zh", "简体中文"}, {"ja", "日本語"},
    {"hi", "हिन्दी"}, {"he", "עברית"},
};

const std::string NUMBER = R"([-+]?\d+(?:[.,]\d+)*)";
const std::regex NUMBERS(NUMBER);
// Not words: genome codes (hex), file names, dates and times.
const std::regex NOT_WORDS(R"(^(?:[\s0-9A-Fa-f#:.\-]*\d[\s0-9A-Fa-f#:.\-]*|[\w.\-]+\.(?:txt|json|bkw|bzw|csv))$)");
const std::regex SPACES(R"(\s+)");
const std::regex BLANK(R"(\{([#*])(\d*)\})");

struct Pattern {
    std::regex regex;
    std::size_t length;     // of the key: longer patterns are tried first, so the most specific wins
    std::string kinds;      // '#' or '*' for each blank, in order
    std::string translation;
    bool capitals;
};

struct State {
    std::string language = "en";
    std::unordered_map<std::string, std::string> exact;   // key -> translation
    std::unordered_map<std::string, std::string> spaced;  // key with its spaces run together -> key
    std::unordered_map<std::string, std::string> folded;  // lowercased plain key -> translation
    std::vector<Pattern> patterns;
    std::unordered_map<std::string, std::string> cache;
    std::set<std::string> missing;
    std::unordered_set<std::string> shown;  // translations, and text made from them: drawn as they are
    int as_shown = 0;                       // while above 0, nothing is translated
    std::string collect = [] {
        const char* path = SDL_getenv("BITZKEY_I18N_COLLECT");
        return std::string(path ? path : "");
    }();
};

State& state() {
    static State instance;
    return instance;
}

bool is_capitals(std::string_view text) {
    bool upper = false;
    for (const char c : text) {
        if (c >= 'a' && c <= 'z') return false;
        if (c >= 'A' && c <= 'Z') upper = true;
    }
    return upper;
}

bool has_letter(std::string_view text) {
    return std::any_of(text.begin(), text.end(), [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte >= 0x80 || std::isalpha(byte);
    });
}

// Upper or lower case, by the language's own rules.
std::string map_case(const std::string& text, bool upper) {
#ifdef _WIN32
    if (text.empty()) return text;
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    const std::string& code = state().language;
    const std::wstring locale(code.begin(), code.end());
    const DWORD flags = (upper ? LCMAP_UPPERCASE : LCMAP_LOWERCASE) | LCMAP_LINGUISTIC_CASING;
    const int mapped = LCMapStringEx(locale.c_str(), flags, wide.c_str(), length, nullptr, 0, nullptr, nullptr, 0);
    if (mapped <= 0) return text;
    std::wstring out(static_cast<std::size_t>(mapped), L'\0');
    LCMapStringEx(locale.c_str(), flags, wide.c_str(), length, out.data(), mapped, nullptr, nullptr, 0);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, out.data(), mapped, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, out.data(), mapped, result.data(), bytes, nullptr, nullptr);
    return result;
#else
    std::string result = text;
    for (char& c : result) c = static_cast<char>(upper ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
    return result;
#endif
}

std::string ascii_lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string shout(const std::string& text, bool capitals) { return capitals ? map_case(text, true) : text; }

std::string escape(const std::string& text) {
    static const std::regex special(R"([.*+?^${}()|[\]\\])");
    return std::regex_replace(text, special, R"(\$&)");
}

std::string fill(const std::string& translation, const std::string& kinds, const std::vector<std::string>& values) {
    std::vector<std::string> numbers, words;
    for (std::size_t index = 0; index < kinds.size(); ++index) (kinds[index] == '#' ? numbers : words).push_back(values[index]);
    std::size_t next_number = 0, next_word = 0;
    std::string result;
    auto at = translation.cbegin();
    for (std::sregex_iterator found(translation.begin(), translation.end(), BLANK), end; found != end; ++found) {
        result.append(at, translation.cbegin() + found->position());
        const bool number = (*found)[1] == "#";
        const std::vector<std::string>& list = number ? numbers : words;
        std::size_t& next = number ? next_number : next_word;
        const std::size_t index = (*found)[2].length() ? std::stoul((*found)[2]) - 1 : next++;
        result += index < list.size() ? list[index] : found->str();
        at = translation.cbegin() + found->position() + found->length();
    }
    result.append(at, translation.cend());
    return result;
}

// Numbers in the text as {#}, and the numbers taken out.
std::string generic_of(const std::string& text, std::vector<std::string>& values) {
    std::string result;
    auto at = text.cbegin();
    for (std::sregex_iterator found(text.begin(), text.end(), NUMBERS), end; found != end; ++found) {
        result.append(at, text.cbegin() + found->position());
        result += "{#}";
        values.push_back(found->str());
        at = text.cbegin() + found->position() + found->length();
    }
    result.append(at, text.cend());
    return result;
}

void note_missing(const std::string& text) {
    State& s = state();
    if (s.collect.empty() || !s.missing.insert(text).second) return;
    std::ofstream(s.collect, std::ios::app | std::ios::binary) << text << "\n";
}

std::optional<std::string> lookup(const std::string& trimmed) {
    State& s = state();
    if (const auto found = s.exact.find(trimmed); found != s.exact.end()) return shout(found->second, is_capitals(trimmed));
    if (const auto found = s.folded.find(ascii_lower(trimmed)); found != s.folded.end()) {
        // The same words in another case: "grazers" from the key "GRAZERS".
        const std::string& translation = found->second;
        if (is_capitals(trimmed)) return shout(translation, true);
        if (std::islower(static_cast<unsigned char>(trimmed[0])) && s.language != "de") {
            std::string first = translation;
            std::string rest;
            std::size_t width = 1;
            while (width < first.size() && (static_cast<unsigned char>(first[width]) & 0xC0) == 0x80) ++width;
            rest = first.substr(width);
            first.resize(width);
            return map_case(first, false) + rest;
        }
        return translation;
    }
    std::vector<std::string> values;
    const std::string generic = generic_of(trimmed, values);
    if (!values.empty()) {
        std::string key;
        if (s.exact.contains(generic)) key = generic;
        else if (const auto found = s.spaced.find(std::regex_replace(generic, SPACES, " ")); found != s.spaced.end()) key = found->second;
        if (!key.empty()) {
            std::string capital_key = std::regex_replace(key, std::regex(R"(\{#\})"), "");
            return shout(fill(s.exact.at(key), std::string(values.size(), '#'), values), is_capitals(capital_key));
        }
    }
    std::smatch found;
    for (const Pattern& pattern : s.patterns) {
        if (!std::regex_match(trimmed, found, pattern.regex)) continue;
        std::vector<std::string> parts;
        for (std::size_t index = 0; index < pattern.kinds.size(); ++index) {
            const std::string part = found[index + 1].str();
            parts.push_back(pattern.kinds[index] == '*' ? tr(part) : part);
        }
        return shout(fill(pattern.translation, pattern.kinds, parts), pattern.capitals);
    }
    note_missing(values.empty() ? trimmed : generic);
    return std::nullopt;
}

void load_catalog(const std::string& code) {
    State& s = state();
    s.exact.clear();
    s.spaced.clear();
    s.folded.clear();
    s.patterns.clear();
    s.cache.clear();
    s.shown.clear();
    s.language = "en";
    if (code == "en") return;
    char* base = SDL_GetBasePath();
    const std::filesystem::path folder = path_from_utf8(base ? base : "") / "i18n";
    if (base) SDL_free(base);
    nlohmann::json catalog;
    try {
        catalog = nlohmann::json::parse(read_file(folder / (code + ".json")));
    } catch (const std::exception&) {
        return;  // no catalog: English, rather than broken text
    }
    s.language = code;
    for (const auto& [key, value] : catalog.items()) {
        const std::string translation = value.get<std::string>();
        if (translation.empty()) continue;
        const bool words = key.find("{*}") != std::string::npos;
        const bool numbers = key.find("{#}") != std::string::npos;
        if (words) {
            Pattern pattern{std::regex(), key.size(), "", translation, false};
            std::string source;
            std::string plain;
            std::size_t at = 0;
            while (at < key.size()) {
                const std::size_t next = std::min(key.find("{#}", at), key.find("{*}", at));
                const std::string text = key.substr(at, next == std::string::npos ? std::string::npos : next - at);
                source += escape(text);
                plain += text;
                if (next == std::string::npos) break;
                pattern.kinds += key[next + 1];
                source += key[next + 1] == '#' ? "(" + NUMBER + ")" : "([\\s\\S]+?)";
                at = next + 3;
            }
            pattern.regex = std::regex(source);
            pattern.capitals = is_capitals(plain);
            s.patterns.push_back(std::move(pattern));
        } else {
            s.exact.emplace(key, translation);
        }
        if (!words && !numbers) s.folded.emplace(ascii_lower(key), translation);
        if (numbers && !words) s.spaced.emplace(std::regex_replace(key, SPACES, " "), key);
    }
    std::stable_sort(s.patterns.begin(), s.patterns.end(), [](const Pattern& a, const Pattern& b) { return a.length > b.length; });
}

std::filesystem::path settings_path() { return data_dir() / "settings.json"; }

// ---- right to left

std::u32string decode(std::string_view text) {
    std::u32string out;
    for (std::size_t at = 0; at < text.size();) {
        const auto byte = static_cast<unsigned char>(text[at]);
        const int extra = byte >= 0xF0 ? 3 : byte >= 0xE0 ? 2 : byte >= 0xC0 ? 1 : 0;
        char32_t point = extra ? byte & (0x3F >> extra) : byte;
        for (int index = 1; index <= extra && at + index < text.size(); ++index) point = (point << 6) | (text[at + index] & 0x3F);
        out.push_back(point);
        at += 1 + extra;
    }
    return out;
}

std::string encode(const std::u32string& points) {
    std::string out;
    for (const char32_t point : points) {
        if (point < 0x80) out += static_cast<char>(point);
        else if (point < 0x800) {
            out += static_cast<char>(0xC0 | (point >> 6));
            out += static_cast<char>(0x80 | (point & 0x3F));
        } else if (point < 0x10000) {
            out += static_cast<char>(0xE0 | (point >> 12));
            out += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (point & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (point >> 18));
            out += static_cast<char>(0x80 | ((point >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (point & 0x3F));
        }
    }
    return out;
}

bool hebrew(char32_t point) { return (point >= 0x0590 && point <= 0x05FF) || (point >= 0xFB1D && point <= 0xFB4F); }

bool neutral(char32_t point) {
    return point < 0x80 && !std::isalnum(static_cast<int>(point)) && point != '#' && point != '%';
}

char32_t mirrored(char32_t point) {
    switch (point) {
        case '(': return ')';
        case ')': return '(';
        case '[': return ']';
        case ']': return '[';
        case '{': return '}';
        case '}': return '{';
        case '<': return '>';
        case '>': return '<';
        default: return point;
    }
}

}  // namespace

const std::vector<LanguageName>& languages() { return LANGUAGES; }

const std::string& language() { return state().language; }

void start_language() {
    std::string code;
    try {
        const nlohmann::json saved = nlohmann::json::parse(read_file(settings_path()));
        code = saved.value("language", "");
    } catch (const std::exception&) {
    }
    if (code.empty()) {
        if (SDL_Locale* locales = SDL_GetPreferredLocales()) {
            for (SDL_Locale* locale = locales; locale->language && code.empty(); ++locale) {
                for (const LanguageName& known : LANGUAGES) {
                    if (known.code == std::string(locale->language)) code = known.code;
                }
            }
            SDL_free(locales);
        }
    }
    load_catalog(code.empty() ? "en" : code);
    UI.close_fonts();
}

void set_language(const std::string& code, bool remember) {
    load_catalog(code);
    UI.close_fonts();  // other languages draw with other fonts
    if (!remember) return;
    nlohmann::json saved = nlohmann::json::object();
    try {
        saved = nlohmann::json::parse(read_file(settings_path()));
    } catch (const std::exception&) {
    }
    saved["language"] = code;
    try {
        write_file(settings_path(), saved.dump(2));
    } catch (const std::exception&) {
    }
}

std::string tr(std::string_view text) {
    State& s = state();
    const std::string whole(text);
    if (s.as_shown > 0 || (s.language == "en" && s.collect.empty()) || s.shown.contains(whole)) return whole;
    for (const LanguageName& known : LANGUAGES) {
        if (whole == known.name) return whole;  // each language is named in its own words
    }
    const std::size_t first = whole.find_first_not_of(" \t\n");
    if (first == std::string::npos) return whole;
    const std::size_t last = whole.find_last_not_of(" \t\n");
    const std::string trimmed = whole.substr(first, last - first + 1);
    if (trimmed.size() < 2 || !has_letter(trimmed) || std::regex_match(trimmed, NOT_WORDS)) return whole;
    if (const auto cached = s.cache.find(whole); cached != s.cache.end()) return cached->second;
    std::string shown = whole;
    if (s.language == "en") {
        std::vector<std::string> values;
        note_missing(generic_of(trimmed, values));
    } else if (const auto result = lookup(trimmed)) {
        shown = whole.substr(0, first) + *result + whole.substr(last + 1);
    }
    if (s.cache.size() > 20000) {
        s.cache.clear();
        s.shown.clear();
    }
    s.cache.emplace(whole, shown);
    if (shown != whole) s.shown.insert(shown);
    return shown;
}

const std::string& as_shown(const std::string& text) {
    if (state().language != "en") state().shown.insert(text);
    return text;
}

AsShown::AsShown() { ++state().as_shown; }
AsShown::~AsShown() { --state().as_shown; }

bool has_right_to_left(std::string_view text) {
    for (const char32_t point : decode(text)) {
        if (hebrew(point)) return true;
    }
    return false;
}

std::string visual_order(std::string_view text) {
    // A plain form of the Unicode bidirectional algorithm, enough for one line of the app's text:
    // runs of Hebrew read right to left, runs of other letters and numbers left to right, and the
    // spaces and punctuation between them take the direction of their neighbours.
    const std::u32string points = decode(text);
    const bool rtl_base = language() == "he";
    std::vector<int> direction(points.size());  // 1 right to left, 0 left to right, -1 not yet known
    for (std::size_t index = 0; index < points.size(); ++index) {
        direction[index] = hebrew(points[index]) ? 1 : neutral(points[index]) ? -1 : 0;
    }
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (direction[index] != -1) continue;
        std::size_t end = index;
        while (end < points.size() && direction[end] == -1) ++end;
        const int before = index > 0 ? direction[index - 1] : (rtl_base ? 1 : 0);
        const int after = end < points.size() ? direction[end] : (rtl_base ? 1 : 0);
        const int resolved = before == after ? before : (rtl_base ? 1 : 0);
        for (std::size_t at = index; at < end; ++at) direction[at] = resolved;
        index = end - 1;
    }
    std::vector<std::u32string> runs;
    std::vector<int> run_direction;
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (runs.empty() || run_direction.back() != direction[index]) {
            runs.emplace_back();
            run_direction.push_back(direction[index]);
        }
        runs.back().push_back(points[index]);
    }
    for (std::size_t index = 0; index < runs.size(); ++index) {
        if (run_direction[index] == 1) {
            std::reverse(runs[index].begin(), runs[index].end());
            for (char32_t& point : runs[index]) point = mirrored(point);
        }
    }
    if (rtl_base) std::reverse(runs.begin(), runs.end());
    std::u32string out;
    for (const auto& run : runs) out += run;
    return encode(out);
}

void pop_character(std::string& text) {
    while (!text.empty()) {
        const unsigned char last = static_cast<unsigned char>(text.back());
        text.pop_back();
        if ((last & 0xC0) != 0x80) break;
    }
}

}  // namespace bitzkey::ui
