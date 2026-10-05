#pragma once
// What the app needs from the operating system: where it lives, the clipboard, files and a file
// picker.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace bitzkey::ui {

// Where the app writes reports and exports: a Bitzkey folder in the user's Documents (the folder
// holding the executable if Documents can't be found), so an installed copy never writes into its
// own install folder.
std::filesystem::path data_dir();

std::optional<std::string> clipboard_text();  // none when the clipboard holds no text
bool set_clipboard_text(const std::string& text, std::string& error);

// Ask the user for a Bitzkey file to import; none if they cancel.
std::optional<std::filesystem::path> choose_file(const std::filesystem::path& start);

std::string read_file(const std::filesystem::path& path);                 // throws std::runtime_error
void write_file(const std::filesystem::path& path, std::string_view text);  // throws std::runtime_error

// A path given as UTF-8 text, as SDL reports dropped files.
std::filesystem::path path_from_utf8(std::string_view text);
std::string utf8_name(const std::filesystem::path& path);  // the file name, for messages

}  // namespace bitzkey::ui
