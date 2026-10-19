#pragma once
// Languages. Every piece of text is written in English and translated where it is drawn: the
// catalogs, i18n/<code>.json beside the executable, map the English to each language. They are the
// web version's catalogs and work the same way: a key is the English exactly as shown, except that
// numbers can stand as {#} and other changing words as {*}; a translation puts them back in order,
// or in another order with {#1}, {*2}, ... Keys in capitals are shown in capitals.
//
// BITZKEY_I18N_COLLECT=<file> appends every piece of text with no translation to that file, once.

#include <string>
#include <string_view>
#include <vector>

namespace bitzkey::ui {

struct LanguageName {
    const char* code;
    const char* name;  // in its own language
};

const std::vector<LanguageName>& languages();
const std::string& language();                // "en" until another is chosen
void start_language();                        // the language saved last time, else the system's
void set_language(const std::string& code, bool remember = true);  // switch now, and remember it
std::string tr(std::string_view text);  // the text in the chosen language

// Text already in the chosen language, drawn as it is: shortened to fit, say. While an
// `AsShown` lives, nothing is translated, for measuring such text a piece at a time.
const std::string& as_shown(const std::string& text);
struct AsShown {
    AsShown();
    ~AsShown();
    AsShown(const AsShown&) = delete;
    AsShown& operator=(const AsShown&) = delete;
};

// Hebrew is written right to left: the text in the order its characters are drawn, left to right.
bool has_right_to_left(std::string_view text);
std::string visual_order(std::string_view text);

void pop_character(std::string& text);  // drop the last UTF-8 character, however many bytes it takes

}  // namespace bitzkey::ui
