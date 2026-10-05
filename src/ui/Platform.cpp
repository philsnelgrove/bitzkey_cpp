#include "ui/Platform.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <knownfolders.h>
#include <shlobj.h>
#endif

namespace bitzkey::ui {

std::filesystem::path data_dir() {
#ifdef _WIN32
    PWSTR documents = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) {
        const std::filesystem::path path = std::filesystem::path(documents) / "Bitzkey";
        CoTaskMemFree(documents);
        return path;
    }
#endif
    if (char* base = SDL_GetBasePath()) {
        const std::filesystem::path path = path_from_utf8(base);
        SDL_free(base);
        return path;
    }
    return std::filesystem::current_path();
}

std::optional<std::string> clipboard_text() {
    if (!SDL_HasClipboardText()) return std::nullopt;
    char* text = SDL_GetClipboardText();
    if (!text) return std::nullopt;
    std::string copy(text);
    SDL_free(text);
    // Windows line endings become plain newlines.
    std::string cleaned;
    cleaned.reserve(copy.size());
    for (const char c : copy) {
        if (c != '\r') cleaned += c;
    }
    if (cleaned.empty()) return std::nullopt;
    return cleaned;
}

bool set_clipboard_text(const std::string& text, std::string& error) {
    if (SDL_SetClipboardText(text.c_str()) == 0) return true;
    error = SDL_GetError();
    return false;
}

std::filesystem::path path_from_utf8(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string utf8_name(const std::filesystem::path& path) {
    const std::u8string name = path.filename().u8string();
    return std::string(reinterpret_cast<const char*>(name.data()), name.size());
}

std::optional<std::filesystem::path> choose_file(const std::filesystem::path& start) {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768, L'\0');
    const std::wstring folder = std::filesystem::exists(start) ? start.wstring() : std::wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.lpstrFilter = L"Bitzkey genomes (*.json, *.txt)\0*.json;*.txt\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
    dialog.lpstrTitle = L"Import Bitzkeys";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) return std::filesystem::path(buffer.data());
#else
    (void)start;
#endif
    return std::nullopt;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("can't open it");
    std::stringstream text;
    text << file.rdbuf();
    std::string content = text.str();
    if (content.rfind("\xEF\xBB\xBF", 0) == 0) content.erase(0, 3);  // a UTF-8 byte order mark
    return content;
}

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("can't write " + path.string());
    file << text;
}

}  // namespace bitzkey::ui
