#pragma once

#include <filesystem>
#include <array>
#include <string>
#include <map>
#include <vector>

namespace xfx {

enum class ThemeMode { Follow, Light, Dark };

// What happens to the input method when the console opens and closes.
enum class ImeMode {
    Restore,        // switch to English on open, restore the previous state on close
    ChineseOnOpen,  // Chinese on open, English on close
    EnglishAlways,  // English on open and English on close
    Disabled        // leave the input method completely alone
};

struct Settings {
    // Kept for older import files; ime_mode is authoritative.
    bool use_english_mode = false;
    bool fixed_window = false;
    bool auto_close = true;
    bool light_theme = false;
    ThemeMode theme_mode = ThemeMode::Follow;
    bool full_resolution_screenshots = true;
    // Number keys 1-9 apply the Nth visible search result while the console has
    // results on screen. Off by default for users who want digits to type normally;
    // the numeric keypad is always excluded from this shortcut.
    bool result_digit_shortcut = true;
    ImeMode ime_mode = ImeMode::Restore;
    // Optional nine-button favourites strip under the results list.
    bool show_slot_row = false;
    // Key that runs favourites (modifier+1..9). Empty means Ctrl.
    std::wstring slot_row_modifier = L"Ctrl";
    // Key that applies the Nth visible result (modifier+1..9). Empty means none.
    std::wstring result_digit_modifier = L"";
    // Text shown in each of the nine favourites buttons.
    std::array<std::wstring, 9> slot_button_labels{};
    // Diagnostics are always on and not user configurable; the flag is kept so an
    // imported older settings file still parses.
    bool diagnostic_logging = true;
    // Empty retains the existing product Capture directory for upgrades.
    std::wstring capture_directory;
    std::wstring hotkey = L"Ctrl+Space";
    std::vector<std::wstring> favorites;
    std::vector<std::wstring> pinned;
    std::vector<std::wstring> recent;
    std::map<std::wstring, std::wstring> overrides;
    std::array<std::wstring, 9> slots{};
    std::map<std::wstring, std::vector<std::wstring>> custom_aliases;
    std::array<std::wstring, 9> slot_labels{};
};

class SettingsStore {
public:
    // Startup allows a missing file. Explicit imports require a readable file.
    static Settings load(const std::filesystem::path& path, bool required = false);
    static void save(const std::filesystem::path& path, const Settings& settings);
    static void export_file(const std::filesystem::path& source, const std::filesystem::path& target);
};

} // namespace xfx
