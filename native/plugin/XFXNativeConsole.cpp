#include "AEConfig.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifdef AE_OS_WIN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <imm.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <wrl/client.h>
#endif

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#include "entry.h"
#include "AE_GeneralPlug.h"
#include "AE_Macros.h"
#include "AEGP_SuiteHandler.h"

#include "../src/hotkey.h"
#include "../src/ime_controller.h"
#include "../src/search_core.h"
#include "../src/effect_catalog.h"
#include "../src/data/saved_native_labels.h"
#include "../src/layer_styles.h"
#include "../src/settings.h"
#include "../src/ae_script.h"
#include "../src/ae_capture.h"
#include "../src/console_support.h"
#include "../src/catalog_customization.h"
#include "../src/text_input_dialog.h"
#include "../src/extendscript_bridge.h"
#include "../src/ui_theme.h"
#include "../src/ui_shapes.h"
#include "../src/ui_loading.h"
#include "../src/themed_menu.h"
#include "../src/gallery_model.h"
#include "../src/thumbnail_list.h"
#include "../src/thumbnail_cache.h"
#include "../src/update_client.h"
#include <shellapi.h>

namespace {

namespace fs = std::filesystem;

constexpr int kHotkeyId = 0x5846;
constexpr wchar_t kHotkeyWindowClassName[] = L"XFXNativeConsoleHotkeyWindow";
constexpr wchar_t kHotkeyCaptureClassName[] = L"XFXNativeConsoleHotkeyCapture";
// Summon shortcut is chosen from a fixed list. See kHotkeyChoices below.
constexpr int kSearchId = 1001;
constexpr int kFilterId = 1002;
constexpr int kResultsId = 1003;
constexpr int kApplyId = 1004;
constexpr int kRefreshId = 1005;
constexpr int kImeModeId = 1008;
// Nine optional favourites buttons in their own strip.
constexpr int kSlotRowFirstId = 1060;
constexpr int kSlotRowToggleId = 1070;
constexpr int kSlotRowKeyId = 1071;
constexpr int kSlotRowHintId = 1072;
constexpr int kResultDigitKeyId = 1073;
constexpr int kResultDigitHintId = 1074;
constexpr int kSlotRowCaptionId = 1075;
constexpr int kResultDigitCaptionId = 1076;
constexpr int kLibraryCaptionId = 1079;
constexpr int kImeCaptionId = 1077;
constexpr int kThemeCaptionId = 1078;
// Results-view digit shortcut switch (applies the Nth visible search result).
constexpr int kResultDigitId = 1041;
constexpr int kDiagnosticLogId = 1043;
constexpr int kExportLogId = 1044;
// Read-only version label pinned to the bottom-right corner of the console.
constexpr int kVersionLabelId = 1042;
constexpr int kStatusId = 1009;
constexpr int kCountId = 1010;
constexpr int kTitleId = 1011;
constexpr int kSubtitleId = 1012;
constexpr int kHotkeyLabelId = 1013;
// Summon shortcut is chosen from a fixed list instead of being typed into a hidden
// capture control. See kHotkeyChoices. The id is outside the layer-style range
// (kLayerStyleFirstId..+8); reusing one of those made GetDlgItem resolve to a style
// button and the dropdown rendered as a blank panel.
constexpr int kHotkeyComboId = 1090;
constexpr int kFavoriteId = 1014;
constexpr int kSlotId = 1015;
constexpr int kImportId = 1016;
constexpr int kExportId = 1017;
constexpr int kCaptureId = 1020;
constexpr int kGalleryId = 1021;
constexpr int kDictionaryId = 1022;
constexpr int kPreferencesId = 1023;
constexpr int kGalleryListId = 1024;
constexpr int kPreviewId = 1025;
constexpr int kSaveCaptureId = 1026;
constexpr int kImportCaptureId = 1027;
constexpr int kCaptureFolderId = 1028;
constexpr int kCopyCaptureId = 1030;
constexpr int kDeleteCaptureId = 1031;
constexpr int kLightThemeId = 1032;
constexpr int kGalleryCanvasId = 1033;
constexpr int kCaptureLocationId = 1034;
constexpr int kChooseCaptureLocationId = 1035;
constexpr int kCaptureLocationLabelId = 1036;
constexpr int kFullResolutionId = 1037;
constexpr int kThumbnailCacheClearId = 1038;
constexpr int kUpdateId = 1039;
constexpr char kProductVersion[] = "1.5.48";
constexpr int kLayerStyleFirstId = 1100;
constexpr int kLayerStyleRowHeight = 36;
constexpr int kSlotRowHeight = 44;
// Room for the results digit-shortcut row added above the update entry.
constexpr int kSettingsHeight = 272;
constexpr int kResultHeight = 28;
constexpr int kResultActionWidth = 24;
constexpr int kShortcutColumnWidth = 55;
constexpr int kShortcutHeight = 40;
constexpr int kToolbarCenterY = 28;
constexpr int kToolbarButtonSize = 36;
constexpr int kToolbarGap = 4;
constexpr UINT_PTR kDragTimerId = 0xD001;
// Polls the real cursor while a result is being dragged to the second strip. The timer is
// owned by the MAIN window, not the results list: the list is destroyed and recreated when
// the search box empties or fills, and a timer on it would silently disappear mid-drag.
constexpr UINT_PTR kResultDragTimerId = 0xD002;
constexpr UINT kDragHoldMs = 280;
constexpr int kDragEdge = 14;
constexpr UINT_PTR kResultTooltipId = 0x7F01;

enum class ResultAction { None, Pin, Favorite, Shortcut };

AEGP_Command g_command = 0;
AEGP_PluginID g_plugin_id = 0;
SPBasicSuite* g_basic = nullptr;
#ifdef XFX_CONSOLE_TEST
fs::path g_test_settings_path;
#ifdef XFX_CONSOLE_TEST
int g_test_measure_calls = 0;
int g_test_popup_erase_calls = 0;
int g_test_last_measure_type = -1;
int g_test_last_measure_id = -1;
int g_test_last_measure_height = -1;
int g_test_measure_by_type[8] = {};
#endif
std::vector<xfx::CatalogItem> g_test_catalog;
unsigned int g_test_catalog_scan_count = 0;
std::optional<xfx::ScriptReply> g_test_script_reply;
void (*g_test_before_script)() = nullptr;
std::string g_test_last_script;
std::optional<POINT> g_test_cursor_position;
// The drag helper reads the real cursor, which a test cannot move.
std::optional<POINT> g_test_drag_cursor;
POINT g_test_cursor_point{};
bool g_test_cursor_in_surface = false;
unsigned g_test_cursor_calls = 0;
std::optional<ULONGLONG> g_test_button_hover_time;
std::optional<bool> g_test_drag_button_held;
std::optional<UINT> g_test_popup_command;
std::optional<DWORD> g_test_hotkey_foreground_process;
int g_test_window_erase_count = 0;
int g_test_window_paint_count = 0;
int g_test_result_draw_count = 0;
int g_test_result_erase_count = 0;
int g_test_result_buffered_paint_count = 0;
int g_test_search_hint_paint_count = 0;
int g_test_search_default_paint_count = 0;
int g_test_initial_background_fill_count = 0;
COLORREF g_test_initial_background_color = CLR_INVALID;
#endif

std::wstring utf8_to_wide(const char* value) {
    if (!value || !*value) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (size > 0) {
        std::wstring result(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), size);
        result.resize(static_cast<size_t>(size - 1));
        return result;
    }
    const int local_size = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
    if (local_size <= 0) return {};
    std::wstring result(static_cast<size_t>(local_size), L'\0');
    MultiByteToWideChar(CP_ACP, 0, value, -1, result.data(), local_size);
    result.resize(static_cast<size_t>(local_size - 1));
    return result;
}

bool contains_cjk(const std::wstring& value) {
    for (const wchar_t ch : value) {
        const auto code = static_cast<unsigned int>(ch);
        if ((code >= 0x3400 && code <= 0x9FFF) || (code >= 0xF900 && code <= 0xFAFF)) return true;
    }
    return false;
}

bool contains_non_ascii(const std::wstring& value) {
    for (const wchar_t ch : value) {
        if (static_cast<unsigned int>(ch) > 0x7F) return true;
    }
    return false;
}

std::wstring strict_utf8_to_wide(const char* value) {
    if (!value || !*value) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result.data(), size);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

// AEGP_GetEffectName/GetEffectCategory return A_char text in AE's current
// application runtime encoding. Keep this separate from the UTF-8 decoder
// used for our TSV files: the host may be using the Windows ACP (for example
// GBK) even when the plug-in's own data files are UTF-8. Modern AE builds can
// also expose UTF-8 labels, so accept a valid non-ASCII UTF-8 candidate first.
std::wstring runtime_text_to_wide(const char* value) {
    if (!value || !*value) return {};
    const auto utf8 = strict_utf8_to_wide(value);
    // Do not restrict UTF-8 detection to CJK: third-party labels can contain
    // accented Latin, Cyrillic, symbols, or emoji. A valid UTF-8 string with
    // any non-ASCII code point is already a stronger signal than decoding the
    // same bytes through the machine ACP (which turns UTF-8 into mojibake on
    // GBK/Windows-1252 hosts).
    if (!utf8.empty() && contains_non_ascii(utf8)) return utf8;
    const int size = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (size > 0) {
        std::wstring result(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_ACP, 0, value, -1, result.data(), size);
        result.resize(static_cast<size_t>(size - 1));
        return result;
    }
    return utf8;
}

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

std::string wide_to_ansi(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

// A 32 KB stack buffer in a helper that runs on every repaint costs stack, and MSVC's
// analyzer flagged ten of them (C6262, up to 68 KB frames). These APIs need a writable
// wide buffer, not a fixed one: hand out heap storage with the same capacity.
constexpr size_t kWidePathCapacity = 32768;
std::vector<wchar_t> wide_buffer(size_t capacity = kWidePathCapacity) {
    return std::vector<wchar_t>(capacity, L'\0');
}

std::filesystem::path settings_path() {
#ifdef XFX_CONSOLE_TEST
    if (!g_test_settings_path.empty()) return g_test_settings_path;
#endif
    auto profile = wide_buffer();
    const DWORD length = GetEnvironmentVariableW(L"XFX_NATIVE_DATA_DIR", profile.data(),
        static_cast<DWORD>(profile.size()));
    if (length && length < profile.size()) return fs::path(profile.data()) / L"settings.ini";
    wchar_t documents[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents);
    return fs::path(documents) / L"LoYoi" / L"XFXNative" / L"settings.ini";
}

std::wstring after_effects_root() {
    // The running host is authoritative, including custom installs and multiple AE versions.
    auto host_path = wide_buffer();
    if (GetModuleFileNameW(nullptr, host_path.data(), static_cast<DWORD>(host_path.size()))) {
        return fs::path(host_path.data()).parent_path().wstring();
    }
    HKEY key = nullptr;
    wchar_t plugin_path[1024] = {};
    DWORD size = sizeof(plugin_path);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Adobe\\After Effects\\25.0", 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        const LONG result = RegQueryValueExW(key, L"PluginInstallPath", nullptr, nullptr,
                                              reinterpret_cast<LPBYTE>(plugin_path), &size);
        RegCloseKey(key);
        if (result == ERROR_SUCCESS) {
            std::wstring path(plugin_path);
            while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
            const size_t marker = path.rfind(L"\\Plug-ins");
            if (marker != std::wstring::npos) return path.substr(0, marker);
        }
    }

    wchar_t program_files[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, program_files);
    return (fs::path(program_files) / L"Adobe" / L"Adobe After Effects 2025" / L"Support Files").wstring();
}

struct AliasEntry {
    int priority = 0;
    bool custom_translation = false;
    std::string match_name;
    std::string english_name;
    std::wstring chinese_name;
    std::string pinyin_full;
    std::string pinyin_initials;
    std::vector<std::wstring> aliases;
};

struct AliasCatalog {
    std::map<std::string, AliasEntry> by_match_name;
    std::map<std::string, AliasEntry> by_english_name;
};

std::vector<std::string> split_tsv(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream input(line);
    while (std::getline(input, field, '\t')) fields.push_back(field);
    while (fields.size() < 6) fields.emplace_back();
    return fields;
}

// AE effect names are supplied by third-party plug-ins. A few vendors include
// harmless leading/trailing spaces or change ASCII casing between releases;
// normalize only those presentation differences for dictionary lookup while
// retaining the original bytes in CatalogItem::match_name for application.
std::string canonical_alias_key(std::string value) {
    const auto is_space = [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    };
    size_t begin = 0;
    while (begin < value.size() && is_space(static_cast<unsigned char>(value[begin]))) ++begin;
    size_t end = value.size();
    while (end > begin && is_space(static_cast<unsigned char>(value[end - 1]))) --end;
    std::string result;
    result.reserve(end - begin);
    bool pending_space = false;
    for (size_t index = begin; index < end; ++index) {
        const unsigned char ch = static_cast<unsigned char>(value[index]);
        if (is_space(ch)) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) result.push_back(' ');
        pending_space = false;
        result.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : static_cast<char>(ch));
    }
    // Match the built-in dictionary's BCC+ prefix rule without rewriting
    // the host label or matchName used when applying an effect.
    if (result.rfind("bcc+ ", 0) == 0) result.erase(4, 1);
    return result;
}

std::string alias_display_fallback(const AliasEntry& alias) {
    if (!alias.english_name.empty()) return alias.english_name;
    if (alias.match_name.empty()) return {};

    // Adobe's Photoshop effects use an internal ADBE PSL matchName but their
    // user-facing English label is stable and readable after removing that
    // implementation prefix. Other ADBE matchNames follow the same pattern;
    // third-party matchNames are retained verbatim as a safe last resort.
    constexpr std::string_view photoshop_prefix = "ADBE PSL ";
    constexpr std::string_view adobe_prefix = "ADBE ";
    if (alias.match_name.rfind(photoshop_prefix, 0) == 0)
        return "Photoshop " + alias.match_name.substr(photoshop_prefix.size());
    if (alias.match_name.rfind(adobe_prefix, 0) == 0)
        return alias.match_name.substr(adobe_prefix.size());
    return alias.match_name;
}

std::wstring safe_effect_display_name(const std::wstring& decoded, const AliasEntry* alias,
                                      std::string_view stable_id = {}, bool chinese_host = false) {
    if (!alias) return decoded;
    const std::string fallback_utf8 = alias_display_fallback(*alias);
    if (fallback_utf8.empty()) return decoded;
    const auto fallback = runtime_text_to_wide(fallback_utf8.c_str());
    const std::string decoded_key = canonical_alias_key(wide_to_utf8(decoded));
    const std::string fallback_key = canonical_alias_key(fallback_utf8);
    // A short non-CJK token or replacement character is a strong sign that
    // AE returned a localized A_char buffer in a different code page. Only
    // treat arbitrary short tokens as suspect when a real English label is
    // known: matchName-only third-party entries legitimately use Lux/Tao/Geo.
    // For matchName-only aliases still repair longer mixed labels such as
    // "Photoshop iy" by comparing with the derived stable fallback.
    const bool mojibake_token = decoded_key == "iy" ||
        (decoded_key.size() > 3 && decoded_key.rfind(" iy") == decoded_key.size() - 3);
    const bool suspicious = !decoded.empty() &&
        (decoded.find(0xFFFD) != std::wstring::npos ||
         (!contains_cjk(decoded) && decoded_key != fallback_key &&
          ((decoded.size() <= 3 && !alias->english_name.empty()) || mojibake_token)));
    if (suspicious || decoded.empty()) {
        // Restore the exact known Chinese-host label, not an inferred English
        // name from a damaged A_char token. Unknown/third-party IDs stay on
        // the existing fallback path; legitimate short product names survive.
        if (chinese_host) if (const auto* localized=xfx::saved_native_chinese_label(stable_id.empty() ? std::string_view(alias->match_name) : stable_id)) return localized;
        return fallback;
    }
    return decoded;
}

std::vector<std::wstring> split_aliases(const std::string& value) {
    std::vector<std::wstring> aliases;
    std::istringstream input(value);
    std::string alias;
    while (std::getline(input, alias, '|')) {
        const std::wstring wide = utf8_to_wide(alias.c_str());
        if (!wide.empty()) aliases.push_back(wide);
    }
    return aliases;
}

fs::path aliases_path() {
#ifdef XFX_CONSOLE_TEST
    if (!g_test_settings_path.empty()) return g_test_settings_path.parent_path() / L"XFXNativeConsole.aliases.tsv";
#endif
    HMODULE module = nullptr;
    auto filename = wide_buffer();
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&g_basic), &module) &&
        GetModuleFileNameW(module, filename.data(), static_cast<DWORD>(filename.size()))) {
        return fs::path(filename.data()).parent_path() / L"XFXNativeConsole.aliases.tsv";
    }
    return fs::path(after_effects_root()) / L"Plug-ins" / L"XFXNativeConsole.aliases.tsv";
}

AliasCatalog load_aliases() {
    AliasCatalog catalog;
    int priority = 0;
    const fs::path legacy = aliases_path();
    const fs::path sources[] = {legacy.parent_path() / L"XFXNativeConsole.aliases.default.tsv", legacy,
        settings_path().parent_path() / L"aliases.user.tsv"};
    for (const auto& source : sources) {
    std::ifstream input(source, std::ios::binary);
    if (!input) continue;
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto fields = split_tsv(line);
        AliasEntry entry;
        entry.priority = ++priority;
        entry.custom_translation = source != sources[0];
        entry.match_name = fields[0];
        entry.english_name = fields[1];
        entry.chinese_name = utf8_to_wide(fields[2].c_str());
        entry.pinyin_full = fields[3];
        entry.pinyin_initials = fields[4];
        entry.aliases = split_aliases(fields[5]);
        if (!fields[0].empty()) catalog.by_match_name[canonical_alias_key(fields[0])] = entry;
        if (!fields[1].empty()) catalog.by_english_name[canonical_alias_key(fields[1])] = entry;
    }
    }
    return catalog;
}

const AliasEntry* find_effect_alias(const AliasCatalog& aliases, const char* name, const char* match_name) {
    const auto find = [](const auto& values, const char* raw) -> const AliasEntry* {
        auto found = values.find(canonical_alias_key(raw ? raw : ""));
        if (found != values.end()) return &found->second;
        // Preserve legacy ACP-label fallback while sharing the exact native
        // selection path with the real-inventory regression.
        const auto wide = runtime_text_to_wide(raw);
        if (wide.empty()) return nullptr;
        found = values.find(canonical_alias_key(wide_to_utf8(wide)));
        return found == values.end() ? nullptr : &found->second;
    };
    const auto* alias = find(aliases.by_match_name, match_name);
    const auto* english = find(aliases.by_english_name, name);
    return english && (!alias || english->priority > alias->priority) ? english : alias;
}

bool append_registered_effect(std::vector<xfx::CatalogItem>& items, const AliasCatalog& aliases,
                              std::set<std::string>& seen, const char* name, const char* match_name,
                              const char* category, bool category_known) {
    if (!name || !match_name || !category ||
        !xfx::effect_is_discoverable(match_name, name, category, category_known)) return false;
    // Different valid effects may share a label; only exact IDs deduplicate.
    if (!seen.insert(match_name).second) return false;
    const auto* alias = find_effect_alias(aliases, name, match_name);
    xfx::CatalogItem item;
    const auto original=runtime_text_to_wide(name);
    item.host_category=runtime_text_to_wide(category);
    const bool chinese_host=xfx::saved_chinese_native_category(item.host_category);
    item.display_name = safe_effect_display_name(original,alias,match_name,chinese_host);
    item.pinyin_full = alias ? alias->pinyin_full : "";
    item.pinyin_initials = alias ? alias->pinyin_initials : "";
    // Preserve the raw host identity for application; visible labels use UTF-8
    // so ACP/GBK bytes never enter the search core's UTF-8 decoder.
    item.english_name = alias && !alias->english_name.empty() ? alias->english_name : wide_to_utf8(item.display_name);
    item.match_name = match_name;
    item.translated_name = alias ? alias->chinese_name : L"";
    item.translated_name_custom=alias && alias->custom_translation;
    if (alias) item.aliases.insert(item.aliases.end(),alias->aliases.begin(),alias->aliases.end());
    item.category = L"效果";
    if (!contains_cjk(item.display_name) && chinese_host) {
        if (const auto* localized=xfx::saved_native_chinese_label(match_name)) item.display_name=localized;
    }
    if(!original.empty() && (original!=item.display_name || wide_to_utf8(original)!=item.english_name) &&
        std::find(item.aliases.begin(),item.aliases.end(),original)==item.aliases.end()) item.aliases.push_back(original);
    item.source = "ae";
    item.item_type = L"effect";
    xfx::enrich_catalog_item(item);
    items.push_back(std::move(item));
    return true;
}

void append_effects(std::vector<xfx::CatalogItem>& items) {
    if (!g_basic) return;
    try {
        AEGP_SuiteHandler suites(g_basic);
        A_long count = 0;
        if (suites.EffectSuite2()->AEGP_GetNumInstalledEffects(&count)) return;

        const AliasCatalog aliases = load_aliases();
        std::set<std::string> seen;
        AEGP_InstalledEffectKey key = AEGP_InstalledEffectKey_NONE;
        for (A_long index = 0; index < count; ++index) {
            if (suites.EffectSuite2()->AEGP_GetNextInstalledEffect(key, &key)) break;
            // The SDK size constants describe the maximum text length. The
            // suite writes a terminating NUL as well, so every destination
            // needs one extra byte. Without it, names near the limit can be
            // truncated or leave an unterminated string, which then feeds the
            // encoding conversion and produces short garbage labels such as
            // the previously observed "iy" for "投影".
            char name[AEGP_MAX_EFFECT_NAME_SIZE + 1] = {};
            char match_name[AEGP_MAX_EFFECT_MATCH_NAME_SIZE + 1] = {};
            char category[AEGP_MAX_EFFECT_CATEGORY_NAME_SIZE + 1] = {};
            if (suites.EffectSuite2()->AEGP_GetEffectName(key, name)) continue;
            if (suites.EffectSuite2()->AEGP_GetEffectMatchName(key, match_name)) continue;
            const bool category_known = suites.EffectSuite2()->AEGP_GetEffectCategory(key, category) == A_Err_NONE;
            append_registered_effect(items, aliases, seen, name, match_name, category, category_known);
        }
    } catch (...) {
    }
}

void append_presets_from(const fs::path& root, std::vector<xfx::CatalogItem>& items,
                         std::set<std::wstring>& seen) {
    if (!fs::exists(root)) return;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            std::wstring extension = entry.path().extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            if (extension != L".ffx") continue;
            const std::wstring path = entry.path().wstring();
            if (!seen.insert(path).second) continue;
            const std::wstring stem = entry.path().stem().wstring();

            xfx::CatalogItem item;
            item.display_name = stem;
            item.english_name = wide_to_utf8(stem);
            item.category = L"预设";
            item.source = "file";
            item.item_type = L"preset";
            item.file_path = path;
            xfx::enrich_catalog_item(item);
            items.push_back(std::move(item));
        }
    } catch (...) {
    }
}

std::vector<xfx::CatalogItem> scan_catalog() {
#ifdef XFX_CONSOLE_TEST
    ++g_test_catalog_scan_count;
    return g_test_catalog;
#endif
    std::vector<xfx::CatalogItem> items;
    append_effects(items);

    std::set<std::wstring> seen_presets;
    const fs::path root(after_effects_root());
    wchar_t documents[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents);
    const fs::path documents_root(documents);
    append_presets_from(root / L"Presets", items, seen_presets);
    append_presets_from(root / L"Support Files" / L"Presets", items, seen_presets);

    const fs::path adobe_documents = documents_root / L"Adobe";
    if (fs::exists(adobe_documents)) {
        for (const auto& version : fs::directory_iterator(
                 adobe_documents, fs::directory_options::skip_permission_denied)) {
            if (!version.is_directory() || version.path().filename().wstring().rfind(L"After Effects", 0) != 0) continue;
            append_presets_from(version.path() / L"User Presets", items, seen_presets);
            append_presets_from(version.path() / L"Presets", items, seen_presets);
        }
    }

    const fs::path adobe_program_files = fs::path(L"C:\\Program Files\\Adobe");
    if (fs::exists(adobe_program_files)) {
        for (const auto& version : fs::directory_iterator(
                 adobe_program_files, fs::directory_options::skip_permission_denied)) {
            if (!version.is_directory() || version.path().filename().wstring().rfind(L"Adobe After Effects", 0) != 0) continue;
            append_presets_from(version.path() / L"Support Files" / L"Presets", items, seen_presets);
        }
    }
    return items;
}

struct ButtonHoverState {
    struct Ripple { POINT origin; ULONGLONG started; };
    std::vector<Ripple> ripples;
    double progress = 0;
    double start_progress = 0;
    ULONGLONG started = 0;
    bool hovered = false;
};

struct PluginState {
    HWND window = nullptr;
    HWND hotkey_window = nullptr;
    HWND search = nullptr;
    HWND filter = nullptr;
    RECT search_surface_bounds{};
    std::array<HWND, 9> layer_style_buttons{};
    std::array<HWND, 9> slot_row_buttons{};
    HWND slot_row_caption = nullptr;
    HWND library_caption = nullptr;
    HWND ime_caption = nullptr;
    HWND theme_caption = nullptr;
    HWND result_digit_caption = nullptr;
    HWND slot_row_checkbox = nullptr;
    HWND slot_row_key = nullptr;
    HWND slot_row_hint = nullptr;
    HWND result_digit_key = nullptr;
    HWND result_digit_hint = nullptr;
    HWND results = nullptr;
    HWND refresh = nullptr;
    HWND hotkey_combo = nullptr;
    HWND ime_mode_combo = nullptr;
    HWND result_digit_checkbox = nullptr;
    HWND diagnostic_checkbox = nullptr;
    HWND export_log_button = nullptr;
    HWND version_label = nullptr;
    HWND status = nullptr;
    HWND update_button = nullptr;
    bool update_available = false;
    HWND count = nullptr;
    HWND title = nullptr;
    HWND subtitle = nullptr;
    HWND hotkey_label = nullptr;
    HWND slot = nullptr;
    HWND import_settings = nullptr;
    HWND export_settings = nullptr;
    HWND light_theme = nullptr;
    HWND capture_location = nullptr;
    HWND choose_capture_location = nullptr;
    HWND capture_location_label = nullptr;
    HWND thumbnail_cache_clear = nullptr;
    HFONT font = nullptr;
    HFONT slot_number_font = nullptr;
    HFONT status_font = nullptr;
    HBRUSH background_brush = nullptr;
    HBRUSH input_brush = nullptr;
    HBRUSH search_brush = nullptr;
    xfx::SearchEngine engine;
    xfx::Settings settings;
    xfx::ImeController ime;
    xfx::GlobalHotkey hotkey;
    std::vector<xfx::CatalogItem> catalog;
    std::vector<xfx::CatalogItem> base_catalog;
    bool catalog_initialized = false; // survives palette close; reset with PluginState
    std::vector<int> visible_slot_numbers;
    HWND capture = nullptr;
    HWND gallery = nullptr;
    HWND dictionary = nullptr;
    HWND preferences = nullptr;
    HWND gallery_window = nullptr;
    HWND gallery_list = nullptr;
    HWND gallery_status = nullptr;
    HWND gallery_canvas = nullptr;
    xfx::GalleryModel gallery_model;
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> gallery_images;
    int gallery_split = -1;
    RECT gallery_splitter{};
    bool gallery_dragging_splitter = false;
    int gallery_drag_view = -1;
    POINT gallery_drag_start{};
    double gallery_drag_offset_x = 0, gallery_drag_offset_y = 0;
    HWND tooltip = nullptr;
    std::wstring result_tooltip;
    std::map<UINT_PTR, std::wstring> tooltip_texts;
    std::map<HWND, ButtonHoverState> button_hovers;
    ResultAction result_hover_action = ResultAction::None;
    int result_hover_row = -1;
    HWND preview_window = nullptr;
    wchar_t suppressed_search_character = 0;
    std::vector<fs::path> captures;
    bool settings_visible = false;
    bool settings_animation_active = false;
    bool redraw_transaction = false;
    bool ime_session_active = false;
    HWND ime_source_window = nullptr;
    bool applying_item = false;
    bool result_click_pending = false;
    bool result_click_redraw_held = false;
    int result_click_row = -1;
    ResultAction result_click_action = ResultAction::None;
    // Dragging a search result onto the second strip binds it there. Kept separate from
    // result_click_pending so a drag never turns into an apply.
    bool result_drag_armed = false;
    int result_drag_row = -1;
    int result_drag_target = -1;
    bool initial_background_primed = false;
    bool initial_frame_painted = false;
    unsigned int modal_depth = 0;
    ULONG_PTR gdiplus_token = 0;
    HHOOK keyboard_hook = nullptr;
    xfx::HotkeySpec hotkey_spec;
    bool hotkey_key_down = false;
    bool drag_pending = false;
    bool drag_active = false;
    POINT drag_anchor_cursor{};
    POINT drag_anchor_window{};
    bool search_composing = false;
    SIZE corner_size{};
    bool status_important = false;
    std::optional<COLORREF> host_background;
    xfx::ThemeColors palette = xfx::resolve_theme(xfx::ThemeMode::Follow);
    ULONGLONG last_theme_check = 0;
    ULONGLONG status_expires = 0;
    ULONGLONG expand_started = 0;
    int expand_from = 0, expand_to = 0, settings_extent = 0;

    PluginState() : engine({}) {}
};

PluginState* g_state = nullptr;
const xfx::ThemeColors& colors() { return g_state ? g_state->palette : xfx::theme_colors(false); }
int ui_px(int value) {const UINT dpi=g_state && IsWindow(g_state->window)?GetDpiForWindow(g_state->window):96;return MulDiv(value,dpi,96);}
bool effective_light_theme() { return xfx::is_light_background(colors().background); }
bool reduced_motion() {
    BOOL enabled = TRUE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && !enabled;
}

void repaint_theme_windows() {
    xfx::script_bridge().set_theme(colors());
    for (HWND window : {g_state->window, g_state->gallery_window, g_state->preview_window}) {
        if (IsWindow(window)) RedrawWindow(window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_NOERASE | RDW_ALLCHILDREN);
    }
    if (g_state->tooltip) {
        SendMessageW(g_state->tooltip, TTM_SETTIPBKCOLOR, colors().surface, 0);
        SendMessageW(g_state->tooltip, TTM_SETTIPTEXTCOLOR, colors().text, 0);
    }
}
struct ThemeBrushes {
    HBRUSH background=nullptr,input=nullptr,search=nullptr;
    explicit ThemeBrushes(bool light):ThemeBrushes(xfx::theme_colors(light)) {}
    explicit ThemeBrushes(const xfx::ThemeColors& palette) {
        background=CreateSolidBrush(palette.background);input=CreateSolidBrush(palette.input);search=CreateSolidBrush(palette.search);
        if(!background || !input || !search) {
            if(background)DeleteObject(background);if(input)DeleteObject(input);if(search)DeleteObject(search);
            throw std::bad_alloc();
        }
    }
    ~ThemeBrushes(){if(background)DeleteObject(background);if(input)DeleteObject(input);if(search)DeleteObject(search);}
    ThemeBrushes(const ThemeBrushes&)=delete;ThemeBrushes& operator=(const ThemeBrushes&)=delete;
    void install(){std::swap(background,g_state->background_brush);std::swap(input,g_state->input_brush);std::swap(search,g_state->search_brush);repaint_theme_windows();}
};

void refresh_host_theme(bool force = false) {
    if (!g_state) return;
    const auto now = GetTickCount64();
    if (!force && now - g_state->last_theme_check < 500) return;
    g_state->last_theme_check = now;
    std::optional<COLORREF> background;
    if (g_basic) {
        try {
            AEGP_SuiteHandler suites(g_basic);
            PF_App_Color color{};
            if (!suites.AppSuite6()->PF_AppGetBgColor(&color)) {
                // PF_App_Color uses 16-bit channels, unlike COLORREF.
                background = RGB((color.red + 128) / 257, (color.green + 128) / 257, (color.blue + 128) / 257);
            }
        } catch (...) { /* Missing host suite falls back to preview AE gray. */ }
    }
    const auto palette = xfx::resolve_theme(g_state->settings.theme_mode, background);
    g_state->host_background = background;
    if (!force && palette.background == colors().background && palette.button == colors().button) return;
    ThemeBrushes brushes(palette);
    g_state->palette = palette;
    brushes.install();
}
LRESULT theme_control_color(UINT message,HDC dc,HWND control=nullptr) {
    const bool search=control==g_state->search;
    // Notes next to a control are secondary text; the same dim colour the status line
    // uses keeps the page readable without every row shouting at once.
    const bool quiet=(control==g_state->status && !g_state->status_important) ||
        control==g_state->slot_row_hint || control==g_state->result_digit_hint;
    SetTextColor(dc,quiet?colors().quiet:colors().text);
    const bool background=message==WM_CTLCOLORSTATIC || message==WM_CTLCOLORBTN;
    SetBkColor(dc,background?colors().background:search?colors().search:colors().input);
    return reinterpret_cast<LRESULT>(background?g_state->background_brush:search?g_state->search_brush:g_state->input_brush);
}

constexpr UINT_PTR kButtonHoverSubclassId = 0xB001;
constexpr UINT_PTR kButtonHoverTimerId = 0xB002;
constexpr UINT_PTR kSettingsAnimationTimerId = 0xB004;
constexpr double kButtonHoverDurationMs = 160.0;

ULONGLONG button_hover_time() {
#ifdef XFX_CONSOLE_TEST
    if (g_test_button_hover_time) return *g_test_button_hover_time;
#endif
    return GetTickCount64();
}

void advance_button_hover(ButtonHoverState& state) {
    const double elapsed = static_cast<double>(button_hover_time() - state.started) / kButtonHoverDurationMs;
    state.progress = state.hovered ? (std::min)(1.0, state.start_progress + elapsed)
        : (std::max)(0.0, state.start_progress - elapsed);
}
void start_button_ripple(HWND hwnd, POINT point) {
    if (!g_state || !IsWindowEnabled(hwnd) || reduced_motion()) return;
    auto& waves = g_state->button_hovers[hwnd].ripples;
    if (waves.size() >= 12) waves.erase(waves.begin());
    waves.push_back({point, button_hover_time()});
    SetTimer(hwnd, kButtonHoverTimerId, 15, nullptr);
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool forward_edge_drag(HWND child, UINT message, WPARAM wparam, LPARAM lparam);
xfx::ScriptReply execute_row_slot(int slot);
void assign_row_slot(int slot);
// Context menu for one second-strip cell: bind / run / clear.
void show_slot_row_menu(int slot);
// Binds an explicit list row; drag-and-drop resolves the row before selection changes.
void assign_row_slot_dragged(int slot, int row);

LRESULT CALLBACK button_hover_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR) {
    // Right-click on a second-strip button opens its own menu: bind the selected search
    // result, run the bound item, or clear the slot. The old one-click bind gave no way
    // back and failed silently when nothing was selected.
    if (g_state && message == WM_CONTEXTMENU) {
        for (int index = 0; index < 9; ++index) {
            if (g_state->slot_row_buttons[static_cast<size_t>(index)] != hwnd) continue;
            show_slot_row_menu(index);
            return 0;
        }
    }
    if (g_state) {
        const auto found = g_state->button_hovers.find(hwnd);
        if (found != g_state->button_hovers.end()) {
            auto& state = found->second;
            if (message == WM_LBUTTONDOWN && IsWindowEnabled(hwnd) && hwnd != g_state->preferences)
                start_button_ripple(hwnd, {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            if (((message == WM_KEYDOWN && (wparam == VK_SPACE || wparam == VK_RETURN) && !(lparam & (1L << 30))) || message == BM_CLICK) &&
                hwnd != g_state->preferences) {
                RECT rect{}; GetClientRect(hwnd, &rect);
                start_button_ripple(hwnd, {rect.right / 2, rect.bottom / 2});
            }
            if ((message == WM_MOUSEMOVE && IsWindowEnabled(hwnd)) || message == WM_MOUSELEAVE) {
                RECT client{}; GetClientRect(hwnd, &client);
                const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                // Buttons retain mouse capture while pressed. Captured moves
                // outside their client area must continue the fade out.
                const bool hovered = message == WM_MOUSEMOVE && PtInRect(&client, point);
                if (hovered != state.hovered) {
                    advance_button_hover(state);
                    state.start_progress = state.progress;
                    state.started = button_hover_time();
                    state.hovered = hovered;
                    state.progress = hovered ? 1.0 : 0.0;
                    if (hovered) {
                        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
                        TrackMouseEvent(&tracking);
                    }
                    if (state.ripples.empty()) KillTimer(hwnd, kButtonHoverTimerId);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            } else if (message == WM_TIMER && wparam == kButtonHoverTimerId) {
                auto& waves = state.ripples;
                waves.erase(std::remove_if(waves.begin(), waves.end(), [](const auto& wave) {
                    return button_hover_time() - wave.started >= 700;
                }), waves.end());
                state.progress = state.hovered ? 1.0 : 0.0;
                if (waves.empty()) KillTimer(hwnd, kButtonHoverTimerId);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            } else if ((message == WM_SHOWWINDOW || message == WM_ENABLE) && !wparam) {
                KillTimer(hwnd, kButtonHoverTimerId);
                state = {};
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (message == WM_NCDESTROY) {
                KillTimer(hwnd, kButtonHoverTimerId);
                g_state->button_hovers.erase(found);
                RemoveWindowSubclass(hwnd, button_hover_proc, subclass);
            }
        }
    }
    if (message == WM_ERASEBKGND) return TRUE;
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

COLORREF button_background_color(HWND button, COLORREF base) {
    const auto found = g_state->button_hovers.find(button);
    if (found == g_state->button_hovers.end()) return base;
    const double amount = found->second.progress;
    const auto target = colors().button_pressed;
    const auto blend = [amount](int first, int second) {
        return static_cast<int>(first + (second - first) * amount + 0.5);
    };
    return RGB(blend(GetRValue(base), GetRValue(target)),
        blend(GetGValue(base), GetGValue(target)), blend(GetBValue(base), GetBValue(target)));
}

Gdiplus::Color vector_color(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}
void paint_button_surface(const DRAWITEMSTRUCT& draw, bool primary = false) {
    SetDCBrushColor(draw.hDC, colors().background);
    FillRect(draw.hDC, &draw.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    Gdiplus::Graphics graphics(draw.hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const auto& c = colors();
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    const bool selected = primary || (draw.CtlID == kPreferencesId && g_state->settings_visible);
    COLORREF base = selected ? c.selected : c.button;
    if (!disabled) base = draw.itemState & ODS_SELECTED ? c.button_pressed : button_background_color(draw.hwndItem, base);
    Gdiplus::RectF rect(static_cast<float>(draw.rcItem.left) + .5f, static_cast<float>(draw.rcItem.top) + .5f,
        static_cast<float>(draw.rcItem.right - draw.rcItem.left) - 1, static_cast<float>(draw.rcItem.bottom - draw.rcItem.top) - 1);
    const float radius = 4.0f * GetDpiForWindow(draw.hwndItem) / 96.0f;
    Gdiplus::GraphicsPath clip;
    xfx::rounded_path(clip, rect, radius);
    Gdiplus::SolidBrush brush(vector_color(base)); graphics.FillPath(&brush, &clip);
    graphics.SetClip(&clip);
    const auto found = g_state->button_hovers.find(draw.hwndItem);
    if (!disabled && !reduced_motion() && found != g_state->button_hovers.end()) for (const auto& wave : found->second.ripples) {
        const double t = (std::min)(1.0, (button_hover_time() - wave.started) / 700.0);
        const double dx = (std::max)(wave.origin.x - draw.rcItem.left, draw.rcItem.right - wave.origin.x);
        const double dy = (std::max)(wave.origin.y - draw.rcItem.top, draw.rcItem.bottom - wave.origin.y);
        const float r = static_cast<float>(std::hypot(dx, dy) * (1 - std::pow(1-t, 3)));
        Gdiplus::SolidBrush ink(vector_color(selected ? c.selected_text : c.accent, static_cast<BYTE>(85 * (1-t))));
        graphics.FillEllipse(&ink, wave.origin.x-r, wave.origin.y-r, 2*r, 2*r);
    }
    graphics.ResetClip();
    Gdiplus::Pen border(vector_color((draw.itemState & ODS_FOCUS) ? c.accent : c.divider), (draw.itemState & ODS_FOCUS) ? 2.0f : 1.0f);
    graphics.DrawPath(&border, &clip);
}

// "Ctrl+1" style caption for the number badge: the prefix is whatever the user bound.
std::wstring slot_number_text(int index, const std::wstring& modifier) {
    std::wstring text;
    if (modifier == L"Ctrl") text = L"Ctrl+";
    else if (modifier == L"Alt") text = L"Alt+";
    else if (modifier == L"Shift") text = L"Shift+";
    else if (modifier == L"Ctrl+Alt") text = L"Ctrl+Alt+";
    text += std::to_wstring(index + 1);
    return text;
}

void paint_text_button(const DRAWITEMSTRUCT& draw) {
    const int saved = SaveDC(draw.hDC);
    if (!saved) return;
    if (g_state->font) SelectObject(draw.hDC, g_state->font);
    const auto& palette = colors();
    const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
    paint_button_surface(draw);
    wchar_t text[256]{}; GetWindowTextW(draw.hwndItem, text, 256);
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, draw.itemState & ODS_DISABLED ? palette.disabled : palette.text);
    RECT text_rect = draw.rcItem;
    DrawTextW(draw.hDC, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RestoreDC(draw.hDC, saved);
}
struct ModalGuard {
    ModalGuard() { if (g_state) ++g_state->modal_depth; }
    ~ModalGuard() { if (g_state && g_state->modal_depth) --g_state->modal_depth; }
};
struct BusyFeedback {
    std::vector<std::pair<HWND, bool>> controls;
    std::unique_ptr<xfx::LoadingSurface> surface;
    explicit BusyFeedback(const wchar_t* text) {
        if (!g_state) return;
        HWND anchor = IsWindowVisible(g_state->window) ? g_state->window : GetForegroundWindow();
        UINT dpi = anchor ? GetDpiForWindow(anchor) : 96; if (!dpi) dpi = 96;
        const float scale = dpi / 96.0f;
        RECT bounds{}; if (anchor) GetWindowRect(anchor,&bounds);
        const int width = static_cast<int>(320*scale), height = static_cast<int>(34*scale);
        bounds.left += 8; bounds.top = bounds.bottom+4; bounds.right = bounds.left+width; bounds.bottom = bounds.top+height;
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST),&monitor)) {
            const int x = (std::max)(monitor.rcWork.left,(std::min)(bounds.left,monitor.rcWork.right-width));
            const int y = (std::max)(monitor.rcWork.top,(std::min)(bounds.top,monitor.rcWork.bottom-height));
            bounds = {x,y,x+width,y+height};
        }
        surface = std::make_unique<xfx::LoadingSurface>(bounds,colors(),text,scale,reduced_motion());
        for (HWND control : {g_state->capture,g_state->refresh,g_state->import_settings,g_state->export_settings}) {
            if (IsWindow(control)) { controls.emplace_back(control,IsWindowEnabled(control)!=FALSE); EnableWindow(control,FALSE); }
        }
    }
    ~BusyFeedback() {
        surface.reset();
        for (const auto& [control,enabled] : controls) if(IsWindow(control)) EnableWindow(control,enabled);
    }
};
WNDPROC g_search_proc = nullptr;
WNDPROC g_results_proc = nullptr;
LRESULT CALLBACK hotkey_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

bool hotkey_event_matches(UINT message, UINT virtual_key, UINT modifiers,
                          const xfx::HotkeySpec& spec) {
    return (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
        virtual_key == spec.virtual_key && modifiers == spec.modifiers;
}

UINT current_hotkey_modifiers() {
    UINT modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) modifiers |= MOD_WIN;
    return modifiers;
}

void update_trace(const std::string& text, bool always = false);

// Index of the second-strip cell under the given screen point, or -1.
int slot_row_cell_at(POINT point) {
    if (!g_state) return -1;
    for (size_t index = 0; index < g_state->slot_row_buttons.size(); ++index) {
        RECT cell{};
        if (!IsWindowVisible(g_state->slot_row_buttons[index])) continue;
        if (!GetWindowRect(g_state->slot_row_buttons[index], &cell)) continue;
        if (PtInRect(&cell, point)) return static_cast<int>(index);
    }
    return -1;
}

// Repaints the strip cells so the drop target is visible. They are ownerdraw child
// windows; invalidating the parent does not repaint them.
void repaint_slot_row_cells() {
    if (!g_state) return;
    for (HWND button : g_state->slot_row_buttons) {
        if (IsWindow(button)) InvalidateRect(button, nullptr, FALSE);
    }
}

// Follows the real cursor. A drag can receive coalesced or filtered moves, so the
// reported position is not trustworthy; the cursor always is.
void handle_drag_poll();

HWND drag_timer_owner() {
    if (g_state && IsWindow(g_state->window)) return g_state->window;
    return nullptr;
}
void start_drag_poll() {
    const HWND owner = drag_timer_owner();
    if (owner) SetTimer(owner, kResultDragTimerId, 30, nullptr);
}
void stop_drag_poll() {
    const HWND owner = drag_timer_owner();
    if (owner) KillTimer(owner, kResultDragTimerId);
}

void update_result_drag_target() {
    if (!g_state) return;
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
#ifdef XFX_CONSOLE_TEST
    if (g_test_drag_cursor) cursor = *g_test_drag_cursor;
#endif
    const int target = slot_row_cell_at(cursor);
    if (target == g_state->result_drag_target) return;
    g_state->result_drag_target = target;
    update_trace("drag-target=" + std::to_string(target) + " cursor=" +
        std::to_string(cursor.x) + "," + std::to_string(cursor.y));
    repaint_slot_row_cells();
}

// The shortcut is no longer recorded by typing into a hidden control; it is picked from a
// list. Nothing suppresses the global shortcut any more, so the console stays reachable
// by its own key at all times.
bool hotkey_capture_active() { return false; }

LRESULT CALLBACK console_keyboard_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0 && wparam == PM_REMOVE && g_state && g_state->modal_depth == 0) {
        auto* message = reinterpret_cast<MSG*>(lparam);
        if (!hotkey_capture_active() &&
            hotkey_event_matches(message->message, static_cast<UINT>(message->wParam),
                                 current_hotkey_modifiers(), g_state->hotkey_spec)) {
            DWORD process = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &process);
            if (process == GetCurrentProcessId()) {
                if (!(message->lParam & (1L << 30))) PostMessageW(g_state->hotkey_window, WM_APP + 1, 0, 0);
                message->message = WM_NULL;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK console_low_level_keyboard_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0 && g_state && g_state->hotkey_window && lparam) {
        auto* event = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
        const bool down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
        const bool up = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;
        if (up && event->vkCode == g_state->hotkey_spec.virtual_key) {
            g_state->hotkey_key_down = false;
        } else if (down && g_state->modal_depth == 0 &&
                   !hotkey_capture_active() &&
                   !g_state->hotkey_key_down &&
                   hotkey_event_matches(static_cast<UINT>(wparam), event->vkCode,
                                        current_hotkey_modifiers(),
                                         g_state->hotkey_spec)) {
            DWORD process = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &process);
            if (process == GetCurrentProcessId()) {
                g_state->hotkey_key_down = true;
                PostMessageW(g_state->hotkey_window, WM_APP + 1, 0, 0);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

HMODULE plugin_module_handle() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&console_low_level_keyboard_hook), &module);
    return module;
}

bool register_configured_hotkey() {
    if (!g_state || !g_state->hotkey_window) return false;
    xfx::HotkeySpec spec;
    if (!xfx::parse_hotkey(g_state->settings.hotkey, &spec)) return false;
    const auto record_registration = [&](HWND window, bool registered, DWORD error) {
        if (!window) return;
        SetPropW(window, L"XFXHotkeyRegistered",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(registered ? 1 : 0)));
        SetPropW(window, L"XFXHotkeyLastError",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(error)));
        SetPropW(window, L"XFXHotkeyModifiers",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(spec.modifiers)));
        SetPropW(window, L"XFXHotkeyVirtualKey",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(spec.virtual_key)));
    };
    // Register against the message-only window first. The previous thread
    // hook depended on plugin initialization happening on AE's UI thread and
    // missed real keyboard messages in some host sessions.
    SetLastError(ERROR_SUCCESS);
    if (g_state->hotkey.register_hotkey(g_state->hotkey_window, kHotkeyId,
                                        g_state->settings.hotkey)) {
        record_registration(g_state->hotkey_window, true, ERROR_SUCCESS);
        g_state->hotkey_spec = spec;
        g_state->hotkey_key_down = false;
        // RegisterHotKey is the primary path. Keeping the low-level fallback
        // active at the same time would deliver every press twice and make a
        // toggle shortcut open then close the console immediately.
        if (g_state->keyboard_hook) {
            UnhookWindowsHookEx(g_state->keyboard_hook);
            g_state->keyboard_hook = nullptr;
        }
        SetPropW(g_state->hotkey_window, L"XFXHotkeyHookInstalled",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(0)));
        return true;
    }
    const DWORD registration_error = GetLastError();
    g_state->hotkey.unregister();
    if (!g_state->keyboard_hook) {
        SetLastError(ERROR_SUCCESS);
        g_state->keyboard_hook = SetWindowsHookExW(
            WH_KEYBOARD_LL, console_low_level_keyboard_hook, plugin_module_handle(), 0);
        SetPropW(g_state->hotkey_window, L"XFXHotkeyHookInstalled",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(g_state->keyboard_hook ? 1 : 0)));
        SetPropW(g_state->hotkey_window, L"XFXHotkeyHookLastError",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(
                     g_state->keyboard_hook ? ERROR_SUCCESS : GetLastError())));
    }
    if (!g_state->keyboard_hook) {
        record_registration(g_state->hotkey_window, false, registration_error);
        return false;
    }
    record_registration(g_state->hotkey_window, false, registration_error);
    g_state->hotkey_spec = spec;
    return true;
}

bool ensure_hotkey_window() {
    if (!g_state) return false;
    if (g_state->hotkey_window && IsWindow(g_state->hotkey_window)) return true;
    WNDCLASSEXW hotkey_class{sizeof(hotkey_class)};
    hotkey_class.hInstance = GetModuleHandleW(nullptr);
    hotkey_class.lpfnWndProc = hotkey_window_proc;
    hotkey_class.lpszClassName = kHotkeyWindowClassName;
    RegisterClassExW(&hotkey_class);
    g_state->hotkey_window = CreateWindowExW(
        0, kHotkeyWindowClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
        hotkey_class.hInstance, nullptr);
    if (!g_state->hotkey_window) return false;
    return register_configured_hotkey();
}


void update_trace(const std::string& text, bool always);
void export_diagnostic_log();

void set_status(const std::wstring& message, bool success = false) {
    if (g_state && g_state->status) {
        SetWindowTextW(g_state->status, message.c_str());
        g_state->status_important=message.find(L"失败")!=std::wstring::npos || message.find(L"不可用")!=std::wstring::npos || message.find(L"部分完成")!=std::wstring::npos;
        SendMessageW(g_state->status,WM_SETFONT,reinterpret_cast<WPARAM>(g_state->status_important?g_state->font:g_state->status_font),TRUE);
        // Call sites classify actual outcomes; message wording never decides expiry.
        g_state->status_expires = success ? GetTickCount64() + 2000 : 0;
        if (g_state->window) { KillTimer(g_state->window,0xB003); if(success) SetTimer(g_state->window,0xB003,2000,nullptr); }
    }
}

void set_font(HWND control) {
    if (g_state && g_state->font && control) SendMessageW(control, WM_SETFONT,
                                                            reinterpret_cast<WPARAM>(g_state->font), TRUE);
    if (g_state && control && control == g_state->search)
        SendMessageW(control, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
}

HWND current_host_input_window() {
    const HWND foreground = GetForegroundWindow();
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(foreground, &process);
    if (!thread || process != GetCurrentProcessId()) return nullptr;
    GUITHREADINFO info{sizeof(info)};
    if (!GetGUIThreadInfo(thread, &info)) return nullptr;
    const HWND focus = info.hwndFocus ? info.hwndFocus : foreground;
    if (g_state->window && (focus == g_state->window || IsChild(g_state->window, focus))) return nullptr;
    return focus;
}

bool ensure_search_english_mode() {
    // Disabled leaves the input method completely alone.
    if (g_state->settings.ime_mode == xfx::ImeMode::Disabled) return true;
    // Chinese-on-open actively turns Chinese ON, which is the inverse of the others.
    if (g_state->settings.ime_mode == xfx::ImeMode::ChineseOnOpen) {
        if (!g_state->ime_session_active)
            g_state->ime_session_active = g_state->ime.capture_original(g_state->search, g_state->ime_source_window);
        if (g_state->ime_session_active && g_state->ime.enable_chinese_mode()) {
            set_status(L"已切换为中文输入法", true);
            return true;
        }
        g_state->ime_session_active = false;
        set_status(L"当前输入法未能切换为中文模式，保持原状态");
        return false;
    }
    if (!g_state->ime_session_active)
        g_state->ime_session_active = g_state->ime.capture_original(g_state->search, g_state->ime_source_window);
    if (g_state->ime_session_active && g_state->ime.enable_english_mode()) return true;
    // A rejected switch may have rolled its backend snapshot back already.
    // Let the next focus retry capture a fresh session instead of skipping it.
    g_state->ime_session_active = false;
    set_status(L"当前输入法未能切换为英文模式，保持原状态");
    return false;
}

void begin_window_drag(HWND hwnd) {
    if (!hwnd) return;
    POINT cursor{}; GetCursorPos(&cursor);
    ReleaseCapture();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(cursor.x, cursor.y));
}

bool point_is_drag_edge(HWND hwnd, POINT point) {
    RECT client{};
    GetClientRect(hwnd, &client);
    if (point.x < -1 || point.y < -1 || point.x > client.right || point.y > client.bottom) return false;
    return point.x <= kDragEdge || point.y <= kDragEdge ||
        point.x >= client.right - kDragEdge || point.y >= client.bottom - kDragEdge;
}

bool point_is_window_edge(HWND hwnd, POINT screen_point) {
    RECT window{};
    if (!GetWindowRect(hwnd, &window)) return false;
    return screen_point.x >= window.left && screen_point.x <= window.right &&
        screen_point.y >= window.top && screen_point.y <= window.bottom &&
        (screen_point.x <= window.left + kDragEdge || screen_point.y <= window.top + kDragEdge ||
         screen_point.x >= window.right - kDragEdge || screen_point.y >= window.bottom - kDragEdge);
}

bool point_in_search_surface(HWND hwnd, POINT client_point);

bool forward_edge_drag(HWND child, UINT message, WPARAM wparam, LPARAM lparam) {
    if (!g_state || !g_state->window || g_state->modal_depth || message != WM_LBUTTONDOWN) return false;
    POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    ClientToScreen(child, &point);
    ScreenToClient(g_state->window, &point);
    if (point_in_search_surface(g_state->window, point) ||
        !point_is_drag_edge(g_state->window, point)) return false;
    SendMessageW(g_state->window, WM_LBUTTONDOWN, wparam, MAKELPARAM(point.x, point.y));
    return true;
}

constexpr UINT_PTR kEdgeDragSubclassId = 0xB00A;
LRESULT CALLBACK edge_drag_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                UINT_PTR id, DWORD_PTR) {
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, edge_drag_proc, id);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    if (forward_edge_drag(hwnd, message, wparam, lparam)) return 0;
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

bool drag_button_is_held() {
#ifdef XFX_CONSOLE_TEST
    if (g_test_drag_button_held) return *g_test_drag_button_held;
#endif
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

// Shows the stored summon shortcut in its dropdown.
void select_hotkey_choice(const std::wstring& value);
// Re-registers the configured shortcut after recording released it.
void restore_suspended_hotkey();
// Recording is hold-based: the modifier must still be down when the final key is
// pressed. This tracks only the key pressed last, for the live prompt text.
void toggle_console();
void save_settings();
bool execute_selected();
bool defer_result_click(UINT message, int row, bool modal) {
    return !modal && row >= 0 &&
        (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP);
}
void toggle_favorite();
void toggle_favorite_key(const std::wstring& key);
void toggle_pinned(const std::wstring& key);
void assign_next_slot();
ResultAction result_action_at_point(POINT point);
void update_result_tooltip(ResultAction action, int row);
bool shortcut_list_mode();
std::wstring slot_display_label(int slot);
std::wstring item_key(const xfx::CatalogItem& item);
void assign_slot(int slot);
xfx::ScriptReply execute_slot(int slot);
void populate_results(bool resize_window = true, bool update_status = true);
void rebuild_customized_catalog();
void show_shortcut_menu();
void adjust_console_height();
void queue_native_snapshot();
xfx::BridgeCompletion prepare_current_snapshot(fs::path& target);
xfx::BridgeCompletion render_current_snapshot(const fs::path& target);
void refresh_update_ui();
void tick_updates();
xfx::BridgeCompletion perform_update(bool check_only = false);
void clear_result_data();
bool add_tooltip_for_control(HWND parent, HWND tooltip, HWND control, UINT_PTR id, const wchar_t* text);

// The two shortcut groups (favourites strip and the nth-result keys) share the digit
// row, so they may never resolve to the same combination. This table is the single
// source of truth for the dropdown text and for conflict detection.
struct ShortcutChoice {
    const wchar_t* label;   // shown in the dropdown
    const wchar_t* modifier; // "" = no modifier
};
inline constexpr std::array<ShortcutChoice, 6> kShortcutChoices{{
    {L"关闭", L""},
    {L"不设（直接按 1-9）", L""},
    {L"Ctrl + 数字", L"Ctrl"},
    {L"Alt + 数字", L"Alt"},
    {L"Shift + 数字", L"Shift"},
    {L"Ctrl + Alt + 数字", L"Ctrl+Alt"},
}};

// Ready-made summon shortcuts. Recording a chord into a hidden control depended on the
// host, the IME and whether RegisterHotKey still owned the combination, which is why the
// shortcut could be changed on one attempt and silently rejected on the next. A list has
// one code path and states its outcome immediately.
inline constexpr std::array<const wchar_t*, 10> kHotkeyChoices{{
    L"Ctrl+Space",
    L"Ctrl+Shift+Space",
    L"Ctrl+Alt+Space",
    L"Alt+Space",
    L"Ctrl+Shift+A",
    L"Ctrl+Alt+A",
    L"Ctrl+Shift+E",
    L"Alt+E",
    L"Ctrl+Shift+F",
    L"Ctrl+Alt+F",
}};

// True when both groups are enabled and would answer the same key combination.
bool shortcut_groups_conflict(const xfx::Settings& settings) {
    if (!settings.show_slot_row || !settings.result_digit_shortcut) return false;
    return settings.slot_row_modifier == settings.result_digit_modifier;
}

// Rewrites the dropdown labels so a combination the other group already owns is marked.
// The user sees the conflict where the choice is made, not after the fact.
void refresh_shortcut_choice_labels();

// Parses one configured modifier name ("Ctrl", "Alt", "Shift", "Win", "Ctrl+Alt").
// Returns 0 when the text is empty, which means "no modifier".
// Dropdown index for a stored modifier, matching kShortcutChoices exactly:
// 0 = 关闭, 1 = 不设（无修饰键）, 2 = Ctrl, 3 = Alt, 4 = Shift, 5 = Ctrl+Alt.
int modifier_combo_index(const std::wstring& text) {
    if (text.empty()) return 1;
    if (text == L"Ctrl") return 2;
    if (text == L"Alt") return 3;
    if (text == L"Shift") return 4;
    if (text == L"Ctrl+Alt") return 5;
    return 1;
}

UINT modifier_from_text(const std::wstring& text) {
    UINT modifiers = 0;
    size_t start = 0;
    while (start <= text.size()) {
        auto end = text.find(L'+', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring token = text.substr(start, end - start);
        while (!token.empty() && iswspace(token.front())) token.erase(token.begin());
        while (!token.empty() && iswspace(token.back())) token.pop_back();
        for (auto& character : token) character = static_cast<wchar_t>(std::towupper(character));
        if (token == L"CTRL" || token == L"CONTROL") modifiers |= MOD_CONTROL;
        else if (token == L"ALT") modifiers |= MOD_ALT;
        else if (token == L"SHIFT") modifiers |= MOD_SHIFT;
        else if (token == L"WIN" || token == L"WINDOWS") modifiers |= MOD_WIN;
        start = end + 1;
        if (end == text.size()) break;
    }
    return modifiers;
}

#ifdef XFX_CONSOLE_TEST
std::optional<UINT> g_test_keyboard_modifiers;
#endif

UINT current_modifiers() {
#ifdef XFX_CONSOLE_TEST
    if (g_test_keyboard_modifiers) return *g_test_keyboard_modifiers;
#endif
    UINT modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) modifiers |= MOD_WIN;
    return modifiers;
}

bool try_digit_shortcut(WPARAM key, LPARAM flags, bool in_search) {
    // Runs the favourites strip. The modifier is user configurable so it cannot clash
    // with the plain-digit "apply Nth result" shortcut.
    if (!g_state || !g_state->settings.show_slot_row) return false;
    const int slot = (key >= '1' && key <= '9') ? static_cast<int>(key - '1') : -1;
    if (slot < 0) return false;
    const UINT required = modifier_from_text(g_state->settings.slot_row_modifier);
    if (current_modifiers() != required) return false;
    if (g_state->settings.row_slots[static_cast<size_t>(slot)].empty()) return false;
    // Inside the search box the user may be typing; only a non-empty modifier makes
    // this an unambiguous shortcut.
    if (in_search && required == 0 && GetWindowTextLengthW(g_state->search) != 0) return false;
    if (!(flags & (1L << 30))) execute_row_slot(slot);
    return true;
}

// Applies the Nth visible search result with the number keys 1-9.
// Deliberately ignores the numeric keypad (VK_NUMPAD1..9): those keys are for typing
// numbers into the search box, and applying an effect on a stray keypad press is not
// what the user meant. Modifier keys also disqualify the press.
// Returns true when the key was consumed.
bool try_result_digit_shortcut(WPARAM key, LPARAM flags, bool in_search) {
    if (!g_state || !g_state->settings.result_digit_shortcut) return false;
    // Only the main-row digits 1-9. The numeric keypad is deliberately excluded so it
    // always types plain numbers. The digit itself is fixed: only the modifier prefix
    // is configurable, because the numbering drawn on screen is the contract.
    if (key < '1' || key > '9') return false;
    const UINT required = modifier_from_text(g_state->settings.result_digit_modifier);
    if (current_modifiers() != required) return false;
    // The favourites strip and the settings page own their keys.
    if (shortcut_list_mode() || g_state->settings_visible) return false;
    // The digits address the rows the user can SEE, not the absolute head of the
    // list. Scrolling changes which items the same digits apply, which is what the
    // numbering drawn on screen promises.
    const LRESULT top = SendMessageW(g_state->results, LB_GETTOPINDEX, 0, 0);
    const int offset = top == LB_ERR ? 0 : static_cast<int>(top);
    const int row = offset + static_cast<int>(key - '1');
    const LRESULT count = SendMessageW(g_state->results, LB_GETCOUNT, 0, 0);
    if (count == LB_ERR || row >= static_cast<int>(count)) {
        // No such result: consume the key anyway so the number never leaks into the
        // search box, and tell the user why nothing happened.
        if (in_search) {
            set_status(L"搜索结果不足 " + std::to_wstring(row - offset + 1) + L" 条");
            return true;
        }
        return false;
    }
    if (flags & (1L << 30)) return true; // auto-repeat: consume, apply once
    SendMessageW(g_state->results, LB_SETCURSEL, static_cast<WPARAM>(row), 0);
    execute_selected();
    return true;
}

template<typename Callback> LRESULT guarded_callback(Callback action) {
    try { return action(); }
    catch (...) { set_status(L"操作未保存，请检查文档目录的写入权限"); return 0; }
}

struct ToolbarLayout {
    RECT search_surface{}, search_edit{}, filter{};
    RECT buttons[3]{};
};

int search_line_height(HWND hwnd) {
    const HWND search = g_state ? g_state->search : nullptr;
    HDC dc = GetDC(search ? search : hwnd);
    if (!dc) return MulDiv(xfx::kUiBodyPixels, GetDpiForWindow(hwnd), 96);
    const HFONT state_font = g_state ? g_state->font : nullptr;
    const auto font = search ? reinterpret_cast<HFONT>(SendMessageW(search, WM_GETFONT, 0, 0)) : state_font;
    // A device context always has a usable font: fall back to the stock one rather than
    // selecting a null handle, which SelectObject rejects.
    const HFONT measured_font = font ? font : state_font ? state_font
        : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const auto previous = SelectObject(dc, measured_font);
    TEXTMETRICW metrics{};
    const bool measured = GetTextMetricsW(dc, &metrics) != FALSE;
    SelectObject(dc, previous);
    ReleaseDC(search ? search : hwnd, dc);
    return measured ? metrics.tmHeight : MulDiv(xfx::kUiBodyPixels, GetDpiForWindow(hwnd), 96);
}

ToolbarLayout toolbar_layout(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const UINT dpi = (std::max)(1u, GetDpiForWindow(hwnd));
    const int width = MulDiv(client.right, 96, static_cast<int>(dpi));
    const int button_size = kToolbarButtonSize;
    const int button_gap = kToolbarGap;
    const int right_margin = 8;
    const int tools_left = width - (button_size * 3 + button_gap * 2 + right_margin);
    // Keep the toolbar rectangles ordered even if the host restores a window
    // below the normal minimum during a DPI switch or a saved-position replay.
    // An inverted search rect makes the EDIT child collapse and leaves only a
    // thin, apparently missing search bar.
    const int filter_left = (std::max)(40, tools_left - 68);
    const int center_y = kToolbarCenterY;
    const auto px = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    const auto rect = [&](int left, int top, int right, int bottom) {
        return RECT{px(left), px(top), px(right), px(bottom)};
    };
    ToolbarLayout result;
    const int search_surface_right = (std::max)(34 + 8, filter_left - 4);
    result.search_surface = rect(8, center_y - 18, search_surface_right, center_y + 18);
    const int line_height = search_line_height(hwnd);
    const int bounded_line_height = (std::max)(1, (std::min)(line_height, px(32)));
    const int line_top = px(center_y) - bounded_line_height / 2;
    const int edit_right = (std::max)(px(35), px(search_surface_right - 4));
    result.search_edit = {px(34), line_top, edit_right, line_top + bounded_line_height};
    result.filter = rect(filter_left, center_y - 18, filter_left + 64, center_y + 18);
    for (int index = 0; index < 3; ++index) {
        const int left = tools_left + index * (button_size + button_gap);
        result.buttons[index] = rect(left, center_y - button_size / 2,
            left + button_size, center_y + button_size / 2);
    }
    return result;
}

bool point_in_search_surface(HWND hwnd, POINT point) {
    return g_state && hwnd == g_state->window && IsWindow(g_state->search) &&
        IsWindowEnabled(g_state->search) && PtInRect(&g_state->search_surface_bounds,point);
}

// How many polls in a row a key must stay down before it counts, and how often the
// poller runs. 15 ms is fast enough to feel instant and slow enough to cost nothing.
constexpr int kHotkeyPollTimerId = 0xB010;
constexpr UINT kHotkeyPollIntervalMs = 15;

void cycle_filter() {
    const LRESULT selected = SendMessageW(g_state->filter, CB_GETCURSEL, 0, 0);
    SendMessageW(g_state->filter, CB_SETCURSEL, (selected + 1) % 3, 0);
    populate_results();
}

LRESULT CALLBACK search_edit_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
    if (message == WM_ERASEBKGND) return TRUE;
    if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
        SetCursor(LoadCursorW(nullptr,IDC_IBEAM));
        return TRUE;
    }
    if (forward_edge_drag(hwnd, message, wparam, lparam)) return 0;
    if(message==WM_IME_STARTCOMPOSITION) {
        g_state->search_composing=true;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if(message==WM_IME_ENDCOMPOSITION || message==WM_NCDESTROY)g_state->search_composing=false;
    if(message==WM_SETFOCUS || message==WM_KILLFOCUS || message==WM_IME_ENDCOMPOSITION) {
        InvalidateRect(hwnd,nullptr,FALSE);
        const HWND parent = GetParent(hwnd);
        if (IsWindow(parent)) {
            const auto layout = toolbar_layout(parent);
            InvalidateRect(parent, &layout.search_surface, FALSE);
        }
    }
    if((message==WM_PAINT || message==WM_PRINTCLIENT) && !GetWindowTextLengthW(hwnd) &&
       !g_state->search_composing) {
#ifdef XFX_CONSOLE_TEST
        ++g_test_search_hint_paint_count;
#endif
        PAINTSTRUCT paint{};HDC dc=message==WM_PAINT?BeginPaint(hwnd,&paint):reinterpret_cast<HDC>(wparam);
        RECT rect{};GetClientRect(hwnd,&rect);FillRect(dc,&rect,g_state->search_brush);
        const int saved=SaveDC(dc);
        const auto font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
        SelectObject(dc,font ? font : g_state->font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,colors().hint);
        wchar_t cue[256]{};
        if(!SendMessageW(hwnd,EM_GETCUEBANNER,reinterpret_cast<WPARAM>(cue),256))
            wcscpy_s(cue,L"输入中文、拼音、英文或 matchName");
        DrawTextW(dc,cue,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        if(saved)RestoreDC(dc,saved);if(message==WM_PAINT)EndPaint(hwnd,&paint);return 0;
    }
    if (message == WM_CHAR) {
        wchar_t dbg[96]{};
        swprintf_s(dbg, L"wmchar wparam=%u scan=0x%X ctrl=%d", static_cast<unsigned>(wparam),
            static_cast<unsigned>((lparam >> 16) & 0xFF), (GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0);
        update_trace(wide_to_utf8(dbg));
    }
    if (message == WM_CHAR && wparam >= 1 && wparam <= 9) {
        // Ctrl + main-row digit arrives as a CONTROL character (Ctrl+3 sends 0x03), never
        // the character '3'. Insert the literal digit so typing numbers always works.
        // This deliberately does NOT depend on the result-digit setting: it is a plain
        // text-input path, so it must behave the same whether the shortcut is on or off.
        const UINT scan = static_cast<UINT>((lparam >> 16) & 0xFF);
        if (scan >= 0x02 && scan <= 0x0A) {
            const wchar_t text[2] = { static_cast<wchar_t>(L'0' + wparam), L'\0' };
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text));
            return 0;
        }
    }
    if (message == WM_CHAR && wparam >= L'1' && wparam <= L'9') {
        // A digit reached the character stage. Whether it may be inserted depends on
        // WHERE it came from, and the scan code is the only reliable discriminator:
        //   main row 0x02..0x0A  -> application shortcut only, never text
        //   keypad   0x47..0x53  -> always plain text, never a shortcut
        // Earlier attempts used a one-shot flag, which could leak across keystrokes and
        // is exactly why the behaviour looked intermittent.
        const UINT scan = static_cast<UINT>((lparam >> 16) & 0xFF);
        const bool from_main_row = scan >= 0x02 && scan <= 0x0A;
        const bool from_keypad = scan >= 0x47 && scan <= 0x53;
        if (from_main_row && !g_state->settings_visible) {
            // The digit has already been routed by WM_KEYDOWN. Whichever shortcut owns it
            // in the current mode - the favourites strip or the Nth-result shortcut - the
            // character must never reach the text, or the search box fills with numbers.
            const int slot = static_cast<int>(wparam - L'1');
            const bool strip_owns = g_state->settings.show_slot_row && slot < 9 &&
                current_modifiers() == modifier_from_text(g_state->settings.slot_row_modifier) &&
                !g_state->settings.row_slots[static_cast<size_t>(slot)].empty();
            const bool results_own = !shortcut_list_mode() &&
                g_state->settings.result_digit_shortcut &&
                current_modifiers() == modifier_from_text(g_state->settings.result_digit_modifier);
            if (strip_owns || results_own) return 0;
        }
        if (from_keypad) {
            // Explicitly let the edit control receive it; nothing may consume it.
            return CallWindowProcW(g_search_proc, hwnd, message, wparam, lparam);
        }
    }
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        // Ctrl + main-row digit has to be handled HERE: an edit control produces no
        // WM_CHAR at all for Ctrl+digit (confirmed by apply-trace.log, which shows no
        // character message for that combination), so any character-stage handling is
        // dead code. Insert the digit directly instead.
        // Ctrl + digit types the digit, unless the user made Ctrl the result-apply
        // modifier, in which case it is a shortcut instead.
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wparam >= '1' && wparam <= '9' &&
            (modifier_from_text(g_state->settings.result_digit_modifier) & MOD_CONTROL) == 0) {
            const wchar_t digit[2] = { static_cast<wchar_t>(wparam), L'\0' };
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(digit));
            return 0;
        }
        // Shift + digit binds the highlighted result to that cell of the strip on screen.
        // This is the keyboard route to the second row: type a few letters, press
        // Shift + the cell number, and the cell is bound without dragging.
        {
            const UINT held_now = current_modifiers();
            const bool shift_down = (held_now & MOD_SHIFT) != 0;
            const bool control_down = (held_now & MOD_CONTROL) != 0;
            const bool shift_is_a_shortcut =
                (g_state->settings.show_slot_row &&
                 (modifier_from_text(g_state->settings.slot_row_modifier) & MOD_SHIFT) != 0) ||
                (g_state->settings.result_digit_shortcut &&
                 (modifier_from_text(g_state->settings.result_digit_modifier) & MOD_SHIFT) != 0);
            if (shift_down && !control_down && !shift_is_a_shortcut &&
                wparam >= '1' && wparam <= '9') {
                const int slot = static_cast<int>(wparam - '1');
                if (g_state->settings.show_slot_row) assign_row_slot(slot);
                else assign_slot(slot);
                return 0;
            }
        }
        // Favourites slots win when the search box is empty; otherwise the digits
        // apply the Nth search result (configurable, keypad excluded).
        if (try_digit_shortcut(wparam, lparam, true)) return 0;
        if (try_result_digit_shortcut(wparam, lparam, true)) return 0;

        if (wparam == VK_RETURN) {
            execute_selected();
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            PostMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
            return 0;
        }
        if (wparam == VK_DOWN) {
            SetFocus(g_state->results);
            return 0;
        }
        if (wparam == VK_TAB) {
            cycle_filter();
            return 0;
        }
    }
    if (message == WM_PAINT) {
#ifdef XFX_CONSOLE_TEST
        ++g_test_search_default_paint_count;
#endif
    }
    const LRESULT result = CallWindowProcW(g_search_proc, hwnd, message, wparam, lparam);
    if (message == WM_SETFOCUS && IsWindowVisible(GetParent(hwnd))) ensure_search_english_mode();
    return result;
    });
}

// One tick of the drag poll. Owned by the main window so it survives the results list
// being recreated, but the work is identical wherever it is called from.
void handle_drag_poll() {
    if (!g_state) return;
    if (!g_state->result_drag_armed) { stop_drag_poll(); return; }
    // Sample the real cursor before deciding anything: the release can arrive before
    // any WM_MOUSEMOVE has been delivered, and the target must be known by then.
    update_result_drag_target();
    const HWND list = g_state->results;
    if (!drag_button_is_held()) {
        // The button came up without the list seeing WM_LBUTTONUP (the release went to
        // another window). Finish the drop here instead of losing the drag. When the
        // pointer never reached a cell this is an ordinary click, so leave the click
        // state untouched for WM_LBUTTONUP to handle.
        stop_drag_poll();
        if (g_state->result_drag_target < 0) return;
        const int drop_target = g_state->result_drag_target;
        const int drag_row = g_state->result_drag_row;
        g_state->result_drag_armed = false;
        g_state->result_drag_row = -1;
        g_state->result_drag_target = -1;
        repaint_slot_row_cells();
        if (list && GetCapture() == list) ReleaseCapture();
        if (g_state->result_click_redraw_held) {
            g_state->result_click_redraw_held = false;
            if (list && IsWindow(list)) SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        }
        g_state->result_click_pending = false;
        g_state->result_click_row = -1;
        g_state->result_click_action = ResultAction::None;
        update_trace("drag-bind-release-away target=" + std::to_string(drop_target));
        if (drop_target >= 0 && drag_row >= 0) assign_row_slot_dragged(drop_target, drag_row);
        return;
    }
    // Capture was stolen while the button is still down (AE can activate its own window
    // mid-gesture). The drop must bind as soon as the pointer is over a cell, otherwise
    // the gesture silently does nothing.
    if ((!list || GetCapture() != list) && g_state->result_drag_target >= 0) {
        const int drop_target = g_state->result_drag_target;
        const int drag_row = g_state->result_drag_row;
        g_state->result_drag_armed = false;
        g_state->result_drag_row = -1;
        g_state->result_drag_target = -1;
        stop_drag_poll();
        repaint_slot_row_cells();
        if (g_state->result_click_redraw_held) {
            g_state->result_click_redraw_held = false;
            if (list && IsWindow(list)) SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        }
        g_state->result_click_pending = false;
        g_state->result_click_row = -1;
        g_state->result_click_action = ResultAction::None;
        update_trace("drag-bind-lost-capture target=" + std::to_string(drop_target));
        if (drop_target >= 0 && drag_row >= 0) assign_row_slot_dragged(drop_target, drag_row);
    }
}

LRESULT CALLBACK results_list_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
    if (message == WM_ERASEBKGND) {
#ifdef XFX_CONSOLE_TEST
        ++g_test_result_erase_count;
#endif
        return TRUE;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width > 0 && height > 0) {
            HDC buffer = CreateCompatibleDC(target);
            HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
            if (buffer && bitmap) {
                const HGDIOBJ previous = SelectObject(buffer, bitmap);
                FillRect(buffer, &client, g_state->input_brush);
                CallWindowProcW(g_results_proc, hwnd, WM_PRINTCLIENT,
                    reinterpret_cast<WPARAM>(buffer), PRF_CLIENT);
                BitBlt(target, paint.rcPaint.left, paint.rcPaint.top,
                    paint.rcPaint.right - paint.rcPaint.left,
                    paint.rcPaint.bottom - paint.rcPaint.top,
                    buffer, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
                SelectObject(buffer, previous);
                DeleteObject(bitmap);
                DeleteDC(buffer);
#ifdef XFX_CONSOLE_TEST
                ++g_test_result_buffered_paint_count;
#endif
            } else {
                if (bitmap) DeleteObject(bitmap);
                if (buffer) DeleteDC(buffer);
                CallWindowProcW(g_results_proc, hwnd, WM_PRINTCLIENT,
                    reinterpret_cast<WPARAM>(target), PRF_CLIENT);
            }
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    bool result_action_click = false;
    if (message == WM_LBUTTONDOWN) {
        const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
        if (!HIWORD(hit)) {
            result_action_click = result_action_at_point(
                {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}) != ResultAction::None;
        }
    }
    if (!result_action_click && forward_edge_drag(hwnd, message, wparam, lparam)) return 0;
    if (message == WM_MOUSEMOVE && g_state->result_drag_armed) {
        // Dragging a result towards the strip: follow the real cursor instead of the
        // reported position, and repaint the cells directly. They are ownerdraw child
        // windows, so invalidating the parent never repainted them.
        update_result_drag_target();
        // A slow drag can move without generating another WM_MOUSEMOVE for the list
        // (the pointer crosses a child window and the moves go there). Poll while the
        // button is held so the highlight keeps up and the release resolves.
        start_drag_poll();
        return 0;
    }
    if (message == WM_TIMER && wparam == kResultDragTimerId) {
        handle_drag_poll();
        return 0;
    }
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tracking);
        const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
        const int row = HIWORD(hit) ? -1 : static_cast<int>(LOWORD(hit));
        const auto action = row >= 0 ? result_action_at_point({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}) : ResultAction::None;
        const auto previous_action = g_state->result_hover_action;
        const auto previous_row = g_state->result_hover_row;
        update_result_tooltip(action, row);
        bool selection_changed = false;
        RECT previous_selection_rect{};
        RECT next_selection_rect{};
        if (row >= 0) {
            const LRESULT current_selection = SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
            if (current_selection != row) {
                const bool has_previous_selection = current_selection >= 0 &&
                    SendMessageW(hwnd, LB_GETITEMRECT, current_selection,
                        reinterpret_cast<LPARAM>(&previous_selection_rect)) != LB_ERR;
                SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
                SendMessageW(hwnd, LB_SETCURSEL, row, 0);
                if (!g_state->result_click_redraw_held)
                    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
                const bool has_next_selection = SendMessageW(hwnd, LB_GETITEMRECT, row,
                    reinterpret_cast<LPARAM>(&next_selection_rect)) != LB_ERR;
                if (has_previous_selection) InvalidateRect(hwnd, &previous_selection_rect, FALSE);
                if (has_next_selection) InvalidateRect(hwnd, &next_selection_rect, FALSE);
                selection_changed = true;
            }
        }
        if (previous_action != action || previous_row != row) {
            RECT dirty{};
            if (previous_row >= 0 && SendMessageW(hwnd, LB_GETITEMRECT, previous_row,
                reinterpret_cast<LPARAM>(&dirty)) != LB_ERR) InvalidateRect(hwnd, &dirty, FALSE);
            if (row >= 0 && row != previous_row && SendMessageW(hwnd, LB_GETITEMRECT, row,
                reinterpret_cast<LPARAM>(&dirty)) != LB_ERR) InvalidateRect(hwnd, &dirty, FALSE);
        }
        if (selection_changed && !g_state->result_click_redraw_held) UpdateWindow(hwnd);
        // The owner-drawn list already updates selection and action hover
        // state above. Letting LISTBOX process every mouse move causes a full
        // row repaint and visible flashing over the search results.
        return 0;
    }
    if (message == WM_MOUSELEAVE) {
        const int previous_row = g_state->result_hover_row;
        update_result_tooltip(ResultAction::None, -1);
        g_state->result_hover_action = ResultAction::None;
        g_state->result_hover_row = -1;
        RECT dirty{};
        if (previous_row >= 0 && SendMessageW(hwnd, LB_GETITEMRECT, previous_row,
            reinterpret_cast<LPARAM>(&dirty)) != LB_ERR) InvalidateRect(hwnd, &dirty, FALSE);
        return 0;
    }
    if (message == WM_MOUSEWHEEL || message == WM_VSCROLL || message == WM_HSCROLL) {
        // LISTBOX scrolls by invalidating and blitting several intermediate
        // regions. Hold redraw while the native control updates its index,
        // then present one complete owner-drawn frame.
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        const LRESULT result = CallWindowProcW(g_results_proc, hwnd, message, wparam, lparam);
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
        return result;
    }
    if (message == WM_LBUTTONDOWN) {
        const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
        const int row = HIWORD(hit) ? -1 : static_cast<int>(LOWORD(hit));
        if (defer_result_click(message, row,
            g_state->modal_depth != 0 || g_state->applying_item)) {
            g_state->result_click_pending = true;
            g_state->result_click_row = row;
            g_state->result_click_action = result_action_at_point(
                {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            // Preserve LISTBOX's normal focus behavior without dispatching its
            // default click painting.
            SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
            g_state->result_click_redraw_held = true;
            g_state->result_drag_armed = row >= 0;
            g_state->result_drag_row = row;
            g_state->result_drag_target = -1;
            update_trace("drag-arm row=" + std::to_string(row));
            // Poll from the first moment. Waiting for a WM_MOUSEMOVE is what lost the
            // drop whenever the pointer left the list without generating one.
            start_drag_poll();
            SetFocus(hwnd);
            SetCapture(hwnd);
            return 0;
        }
    }
    if (message == WM_LBUTTONUP) {
        // Drop resolves before the click gate: a drag that started on a result is a bind,
        // not an apply, and it must not be lost if the click state was already cleared.
        if (g_state->result_drag_armed && !g_state->result_click_pending) {
            update_result_drag_target();
            const int drop_target = g_state->result_drag_target;
            const int drag_row = g_state->result_drag_row;
            g_state->result_drag_armed = false;
            g_state->result_drag_row = -1;
            g_state->result_drag_target = -1;
            stop_drag_poll();
            repaint_slot_row_cells();
            if (drop_target >= 0 && drag_row >= 0) assign_row_slot_dragged(drop_target, drag_row);
            else if (drop_target < 0 && drag_row >= 0 && !IsWindowVisible(g_state->slot_row_buttons[0]))
                set_status(L"第二行快捷栏已关闭，请先在设置里打开它再拖动绑定");
            return 0;
        }
        if (g_state->result_click_pending) {
            const int pending_row = g_state->result_click_row;
            const auto pending_action = g_state->result_click_action;
            g_state->result_click_pending = false;
            g_state->result_click_row = -1;
            g_state->result_click_action = ResultAction::None;
            // A drop on the second strip binds the dragged result there. This is the
            // documented way to fill the strip, and it must win over the apply path.
            // The target MUST be read before ReleaseCapture: that call sends
            // WM_CAPTURECHANGED straight back into this window, whose handler resolves
            // and clears the drag, so reading afterwards always found -1 and the bind
            // silently never happened.
            update_result_drag_target();
            const int drop_target = g_state->result_drag_target;
            const int drop_row = g_state->result_drag_row >= 0 ?
                g_state->result_drag_row : pending_row;
            update_trace("drag-drop target=" + std::to_string(drop_target) +
                " row=" + std::to_string(drop_row));
            g_state->result_drag_armed = false;
            g_state->result_drag_row = -1;
            g_state->result_drag_target = -1;
            stop_drag_poll();
            if (GetCapture() == hwnd) ReleaseCapture();

            const auto restore_redraw = [&](bool update) {
                if (!g_state->result_click_redraw_held) return;
                g_state->result_click_redraw_held = false;
                if (!IsWindow(hwnd)) return;
                SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
                if (update) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                    UpdateWindow(hwnd);
                }
            };

            repaint_slot_row_cells();
            if (drop_target >= 0) {
                SendMessageW(hwnd, LB_SETCURSEL, drop_row, 0);
                assign_row_slot_dragged(drop_target, drop_row);
                restore_redraw(true);
                RedrawWindow(g_state->window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
                return 0;
            }

            const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
            if (HIWORD(hit) || static_cast<int>(LOWORD(hit)) != pending_row) {
                restore_redraw(true);
                return 0;
            }
            const auto release_action = result_action_at_point(
                {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            if (release_action != pending_action) {
                restore_redraw(true);
                return 0;
            }

            // Commit the selection while redraw is held. The next operation is
            // either an action refresh or ApplyPresentation, so no native
            // pressed/selected frame can escape between the click and action.
            SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
            SendMessageW(hwnd, LB_SETCURSEL, pending_row, 0);

            const auto action = release_action;
            if (action != ResultAction::None) {
                const auto data = SendMessageW(hwnd, LB_GETITEMDATA, pending_row, 0);
                if (data != LB_ERR && data != 0) {
                    const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
                    if (item->item_type != L"empty") {
                        const auto key = item_key(*item);
                        if (action == ResultAction::Pin) toggle_pinned(key);
                        if (action == ResultAction::Favorite) toggle_favorite_key(key);
                        if (action == ResultAction::Shortcut) assign_next_slot();
                    }
                }
                restore_redraw(true);
                return 0;
            }
            execute_selected();
            restore_redraw(true);
            return 0;
        }
        const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
        const auto action = HIWORD(hit) ? ResultAction::None :
            result_action_at_point({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        const LRESULT result = CallWindowProcW(g_results_proc, hwnd, message, wparam, lparam);
        if (!HIWORD(hit)) {
            SendMessageW(hwnd, LB_SETCURSEL, LOWORD(hit), 0);
            if (action != ResultAction::None) {
                const auto data = SendMessageW(hwnd, LB_GETITEMDATA, LOWORD(hit), 0);
                if (data != LB_ERR && data != 0) {
                    const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
                    if (item->item_type == L"empty") return result;
                    const auto key = item_key(*item);
                    if (action == ResultAction::Pin) toggle_pinned(key);
                    if (action == ResultAction::Favorite) toggle_favorite_key(key);
                    if (action == ResultAction::Shortcut) assign_next_slot();
                }
                return result;
            }
            execute_selected();
        }
        return result;
    }
    if ((message == WM_CAPTURECHANGED || message == WM_CANCELMODE) &&
        (g_state->result_click_pending || g_state->result_drag_armed)) {
        if (g_state->result_drag_armed) {
            // Another window stole the capture mid-drag. That happens for the taskbar,
            // IME windows and automation tools - and dropping on the strip at that moment
            // must still bind, so resolve the drop before clearing the state.
            update_result_drag_target();
            const int stolen_target = g_state->result_drag_target;
            const int stolen_row = g_state->result_drag_row;
            update_trace("drag-capture-changed target=" + std::to_string(stolen_target) +
                " row=" + std::to_string(stolen_row) +
                " pending=" + (g_state->result_click_pending ? "1" : "0"));
            if (stolen_target >= 0 && stolen_row >= 0) {
                g_state->result_drag_armed = false;
                g_state->result_drag_row = -1;
                g_state->result_drag_target = -1;
                repaint_slot_row_cells();
                assign_row_slot_dragged(stolen_target, stolen_row);
            }
            g_state->result_drag_armed = false;
            g_state->result_drag_row = -1;
            if (g_state->result_drag_target >= 0) {
                g_state->result_drag_target = -1;
                repaint_slot_row_cells();
            }
        }
        g_state->result_click_pending = false;
        if (g_state->result_click_redraw_held) {
            g_state->result_click_redraw_held = false;
            if (IsWindow(hwnd)) SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        }
        g_state->result_click_row = -1;
        g_state->result_click_action = ResultAction::None;
        if (message == WM_CANCELMODE && GetCapture() == hwnd) ReleaseCapture();
        return 0;
    }
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        const UINT held = current_modifiers();
        const bool control = (held & MOD_CONTROL) != 0;
        const bool shift = (held & MOD_SHIFT) != 0;
        // Shift + digit assigns the selected result to that cell. It is decided before the
        // configurable digit shortcuts so the gesture keeps working even when a group is
        // configured to use Shift; otherwise the key would apply the effect instead. When
        // either enabled group really is configured to use Shift, that shortcut wins and
        // the assignment stays on the context menu.
        const bool shift_is_a_shortcut =
            (g_state->settings.show_slot_row &&
             (modifier_from_text(g_state->settings.slot_row_modifier) & MOD_SHIFT) != 0) ||
            (g_state->settings.result_digit_shortcut &&
             (modifier_from_text(g_state->settings.result_digit_modifier) & MOD_SHIFT) != 0);
        if (shift && !control && !shift_is_a_shortcut &&
            wparam >= '1' && wparam <= '9') {
            const int slot = static_cast<int>(wparam - '1');
            if (g_state->settings.show_slot_row) assign_row_slot(slot);
            else assign_slot(slot);
            return 0;
        }
        if (try_digit_shortcut(wparam, lparam, false)) return 0;
        if (try_result_digit_shortcut(wparam, lparam, false)) return 0;
        if (wparam == VK_RETURN) {
            execute_selected();
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            PostMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
            return 0;
        }
        if (wparam == VK_TAB) {
            cycle_filter();
            return 0;
        }
    }
    return CallWindowProcW(g_results_proc, hwnd, message, wparam, lparam);
    });
}

void clear_result_data() {
    if (!g_state || !g_state->results) return;
    const LRESULT count = SendMessageW(g_state->results, LB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; ++i) {
        const LRESULT data = SendMessageW(g_state->results, LB_GETITEMDATA, i, 0);
        if (data != LB_ERR && data != 0) delete reinterpret_cast<xfx::CatalogItem*>(data);
    }
}

struct ListRedrawGuard {
    HWND window = nullptr;
    bool outer_transaction = false;
    explicit ListRedrawGuard(HWND target) : window(target) {
        outer_transaction = g_state && g_state->redraw_transaction;
        if (IsWindow(window)) SendMessageW(window, WM_SETREDRAW, FALSE, 0);
    }
    ~ListRedrawGuard() {
        if (!IsWindow(window)) return;
        if (outer_transaction) {
            // SearchRedrawGuard owns the whole subtree's redraw state. Do not
            // briefly re-enable this child while rows are being rebuilt.
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
        SendMessageW(window, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
    }
};

// Batch child updates without hiding the top-level palette from the Windows
// compositor. DefWindowProc removes WS_VISIBLE on WM_SETREDRAW(FALSE); testing
// that bit at release time therefore misses precisely the controls we froze.
// Keep the original handles instead, and leave the EDIT live so native typing
// and caret updates never disappear while the search engine is running.
struct SearchRedrawGuard {
    HWND parent = nullptr;
    HWND original_results = nullptr;
    std::vector<HWND> children;
    bool active = false;

    explicit SearchRedrawGuard(HWND target) : parent(target) {
        if (!g_state || !IsWindow(parent) || g_state->redraw_transaction) return;
        original_results = g_state->results;
        EnumChildWindows(parent, [](HWND child, LPARAM data) -> BOOL {
            auto* self = reinterpret_cast<SearchRedrawGuard*>(data);
            if (child != g_state->search && (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE))
                self->children.push_back(child);
            return TRUE;
        }, reinterpret_cast<LPARAM>(this));
        g_state->redraw_transaction = true;
        active = true;
        for (HWND child : children) SendMessageW(child, WM_SETREDRAW, FALSE, 0);
    }

    void finish() {
        if (!active) return;
        active = false;
        for (HWND child : children)
            if (IsWindow(child) && IsChild(parent, child)) SendMessageW(child, WM_SETREDRAW, TRUE, 0);
        // Switching between the shortcut strip and results creates a new
        // LISTBOX, which was not present in the entry snapshot.
        if (g_state && g_state->results != original_results && IsWindow(g_state->results))
            SendMessageW(g_state->results, WM_SETREDRAW, TRUE, 0);
        if (g_state) g_state->redraw_transaction = false;
        if (IsWindow(parent))
            RedrawWindow(parent, nullptr, nullptr,
                RDW_INVALIDATE | RDW_NOERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }

    SearchRedrawGuard(const SearchRedrawGuard&) = delete;
    SearchRedrawGuard& operator=(const SearchRedrawGuard&) = delete;
    ~SearchRedrawGuard() { finish(); }
};

std::wstring filter_name() {
    const LRESULT selected = SendMessageW(g_state->filter, CB_GETCURSEL, 0, 0);
    if (selected == 1) return L"效果";
    if (selected == 2) return L"预设";
    if (selected == 3) return L"收藏";
    if (selected == 4) return L"最近";
    return L"全部";
}

std::wstring item_key(const xfx::CatalogItem& item) {
    return xfx::catalog_item_key(item);
}

std::wstring item_display_label(const xfx::CatalogItem& item) {
    const auto original = xfx::catalog_display_name(item);
    return item.translated_name.empty()
        ? original
        : xfx::bilingual_effect_label(original, item.translated_name,item.translated_name_custom);
}

ResultAction result_action_at_point(POINT point) {
    if (!g_state || !g_state->results) return ResultAction::None;
    if (shortcut_list_mode()) return ResultAction::None;
    const LRESULT hit = SendMessageW(g_state->results, LB_ITEMFROMPOINT, 0, MAKELPARAM(point.x, point.y));
    if (HIWORD(hit)) return ResultAction::None;
    RECT row{};
    if (SendMessageW(g_state->results, LB_GETITEMRECT, LOWORD(hit), reinterpret_cast<LPARAM>(&row)) == LB_ERR)
        return ResultAction::None;
    const int right = row.right;
    if (point.x >= right - ui_px(kResultActionWidth) * 3 && point.x < right - ui_px(kResultActionWidth) * 2) return ResultAction::Pin;
    if (point.x >= right - ui_px(kResultActionWidth) * 2 && point.x < right - ui_px(kResultActionWidth)) return ResultAction::Favorite;
    if (point.x >= right - ui_px(kResultActionWidth) && point.x < right) return ResultAction::Shortcut;
    return ResultAction::None;
}

void update_result_tooltip(ResultAction action, int row) {
    if (!g_state || !g_state->tooltip) return;
    if (action == g_state->result_hover_action && row == g_state->result_hover_row) return;
    g_state->result_hover_action = action;
    g_state->result_hover_row = row;
    switch (action) {
    case ResultAction::Pin: g_state->result_tooltip = L"置顶排序 / 取消置顶排序"; break;
    case ResultAction::Favorite: g_state->result_tooltip = L"收藏 / 取消收藏"; break;
    case ResultAction::Shortcut: g_state->result_tooltip = L"添加到设置中的下一个快捷槽"; break;
    case ResultAction::None: g_state->result_tooltip = L"鼠标左键应用；右键打开更多操作"; break;
    }
    if (shortcut_list_mode() && row >= 0 && row < 9) {
        g_state->result_tooltip = L"快捷槽 " + std::to_wstring(row + 1) + L"：" + slot_display_label(row) +
            L"\n左键应用；右键设置快捷槽";
    }
    g_state->tooltip_texts[kResultTooltipId] = g_state->result_tooltip;
    TOOLINFOW tool{TTTOOLINFOW_V1_SIZE};
    tool.hwnd = g_state->window;
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.uId = reinterpret_cast<UINT_PTR>(g_state->results);
    tool.lpszText = g_state->tooltip_texts[kResultTooltipId].data();
    SendMessageW(g_state->tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    SendMessageW(g_state->tooltip, TTM_UPDATE, 0, 0);
}

bool contains_key(const std::vector<std::wstring>& values, const std::wstring& key) {
    return std::find(values.begin(), values.end(), key) != values.end();
}

bool shortcut_list_mode() {
    wchar_t query[512] = {};
    GetWindowTextW(g_state->search, query, 512);
    return !g_state->settings_visible && !*query && filter_name() == L"全部";
}

void update_results_list_style(bool shortcut_mode) {
    if (!g_state || !g_state->results) return;
    const HWND parent_window = GetParent(g_state->results);
    const bool was_shortcut = (GetWindowLongPtrW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) != 0;
    // LBS_MULTICOLUMN is a creation-time style: recreate when entering/leaving the shortcut strip.
    if (was_shortcut != shortcut_mode) {
        const HWND old = g_state->results;
        const bool focused = GetFocus() == old;
        if (GetCapture() == old) ReleaseCapture();
        // The poll timer is owned by this window, so it dies with it. Clear the drag state
        // too, or the next window would think a drag from the old one is still running.
        stop_drag_poll();
        g_state->result_drag_armed = false;
        g_state->result_drag_row = -1;
        g_state->result_drag_target = -1;
        g_state->result_click_pending = false;
        g_state->result_click_redraw_held = false;
        g_state->result_click_row = -1;
        g_state->result_click_action = ResultAction::None;
        RECT bounds{}; GetWindowRect(old, &bounds);
        MapWindowPoints(nullptr, parent_window, reinterpret_cast<POINT*>(&bounds), 2);
        if (g_state->tooltip) {
            TOOLINFOW tool{TTTOOLINFOW_V1_SIZE}; tool.hwnd = parent_window;
            tool.uId = reinterpret_cast<UINT_PTR>(old);
            SendMessageW(g_state->tooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
        clear_result_data();
        DestroyWindow(old);
        g_state->results = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
            (shortcut_mode ? LBS_MULTICOLUMN : WS_VSCROLL),
            bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            parent_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResultsId)), nullptr, nullptr);
        if (!g_state->results) throw std::runtime_error("Unable to create result list");
        if (g_state->redraw_transaction)
            SendMessageW(g_state->results, WM_SETREDRAW, FALSE, 0);
        set_font(g_state->results);
        SendMessageW(g_state->results, LB_SETITEMHEIGHT, 0, shortcut_mode ? kShortcutHeight : kResultHeight);
        g_results_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_state->results,
            GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(results_list_proc)));
        add_tooltip_for_control(parent_window, g_state->tooltip, g_state->results,
            kResultTooltipId, L"鼠标左键应用；右键打开更多操作");
        g_state->result_hover_row = -1;
        g_state->result_hover_action = ResultAction::None;
        if (focused) SetFocus(g_state->results);
    }
    RECT parent{};
    GetClientRect(parent_window, &parent);
    const int column_width = shortcut_mode ? (std::max)(1,(static_cast<int>(parent.right)-MulDiv(16,GetDpiForWindow(parent_window),96))/9) : (std::max)(1,static_cast<int>(parent.right)-16);
    if (shortcut_mode) SendMessageW(g_state->results, LB_SETCOLUMNWIDTH, column_width, 0);
    SetWindowPos(g_state->results, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

int recent_rank(const std::wstring& key) {
    const auto found = std::find(g_state->settings.recent.begin(), g_state->settings.recent.end(), key);
    return found == g_state->settings.recent.end()
        ? 10000
        : static_cast<int>(std::distance(g_state->settings.recent.begin(), found));
}

int pinned_rank(const std::wstring& key) {
    const auto found = std::find(g_state->settings.pinned.begin(), g_state->settings.pinned.end(), key);
    return found == g_state->settings.pinned.end()
        ? 10000
        : static_cast<int>(std::distance(g_state->settings.pinned.begin(), found));
}

void remember_recent(const xfx::CatalogItem& item) {
    const std::wstring key = item_key(item);
    auto& recent = g_state->settings.recent;
    recent.erase(std::remove(recent.begin(), recent.end(), key), recent.end());
    recent.insert(recent.begin(), key);
    if (recent.size() > 20) recent.resize(20);
}

void populate_results(bool resize_window, bool update_status) {
    // Applying a menu command can synchronously close the palette before the
    // popup handler returns and requests a refresh. Its old HWNDs own no rows.
    if (!g_state || !IsWindow(g_state->search) || !IsWindow(g_state->results)) return;
    wchar_t query[512] = {};
    GetWindowTextW(g_state->search, query, 512);
    const std::wstring filter = filter_name();
    const bool desired_shortcut_mode = shortcut_list_mode();
    const bool was_shortcut_mode = (GetWindowLongW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) != 0;
    // Every population path can change parent height and move the footer,
    // including native combo notifications (not only EDIT search changes).
    // Own one complete subtree redraw after all controls reach final bounds.
    SearchRedrawGuard surface_redraw(g_state->window);
    update_results_list_style(desired_shortcut_mode);
    const bool list_style_changed = was_shortcut_mode != desired_shortcut_mode;
    ListRedrawGuard redraw(g_state->results);
    auto matches = g_state->engine.search(query);
    std::stable_sort(matches.begin(), matches.end(), [](const xfx::SearchResult& left,
                                                        const xfx::SearchResult& right) {
        const int left_pinned = pinned_rank(item_key(left.item));
        const int right_pinned = pinned_rank(item_key(right.item));
        if ((left_pinned < 10000) != (right_pinned < 10000)) return left_pinned < right_pinned;
        if (left_pinned != right_pinned) return left_pinned < right_pinned;
        if (left.score != right.score) return left.score < right.score;
        const bool left_favorite = contains_key(g_state->settings.favorites, item_key(left.item));
        const bool right_favorite = contains_key(g_state->settings.favorites, item_key(right.item));
        if (left_favorite != right_favorite) return left_favorite;
        const int left_recent = recent_rank(item_key(left.item));
        const int right_recent = recent_rank(item_key(right.item));
        if (left_recent != right_recent) return left_recent < right_recent;
        return left.score < right.score;
    });
    std::vector<int> match_slots(matches.size(), -1);
    if (shortcut_list_mode()) {
        const auto all = matches;
        matches.clear();
        match_slots.clear();
        for (size_t slot = 0; slot < g_state->settings.slots.size(); ++slot) {
            const auto& key = g_state->settings.slots[slot];
            if (key.empty()) {
                xfx::SearchResult empty;
                empty.item.display_name = L"空";
                empty.item.category = L"快捷槽";
                empty.item.item_type = L"empty";
                matches.push_back(std::move(empty));
                match_slots.push_back(static_cast<int>(slot));
                continue;
            }
            const auto found = std::find_if(all.begin(), all.end(), [&](const auto& result) { return item_key(result.item) == key; });
            if (found != all.end()) { matches.push_back(*found); match_slots.push_back(static_cast<int>(slot)); }
            else {
                xfx::SearchResult stale;
                stale.item.display_name = L"项目已失效";
                stale.item.category = L"快捷槽";
                stale.item.item_type = L"empty";
                matches.push_back(std::move(stale));
                match_slots.push_back(static_cast<int>(slot));
            }
        }
    }

    clear_result_data();
    SendMessageW(g_state->results, LB_RESETCONTENT, 0, 0);
    g_state->visible_slot_numbers.clear();
    size_t match_index = 0;
    for (const auto& result : matches) {
        const int slot = match_slots[match_index++];
        const auto key = item_key(result.item);
        if (!shortcut_list_mode()) {
            if (filter == L"收藏" && !contains_key(g_state->settings.favorites, key)) continue;
            if (filter == L"最近" && !contains_key(g_state->settings.recent, key)) continue;
            if ((filter == L"效果" || filter == L"预设") && result.item.category != filter) continue;
        }
        const std::wstring display = item_display_label(result.item);
        const LRESULT row = SendMessageW(g_state->results, LB_ADDSTRING, 0,
                                         reinterpret_cast<LPARAM>(display.c_str()));
        SendMessageW(g_state->results, LB_SETITEMDATA, row,
                     reinterpret_cast<LPARAM>(new xfx::CatalogItem(result.item)));
        g_state->visible_slot_numbers.push_back(slot);
    }
    const LRESULT result_count = SendMessageW(g_state->results, LB_GETCOUNT, 0, 0);
    if (result_count > 0) SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
    if (resize_window || list_style_changed) adjust_console_height();
    if (update_status)
        set_status(result_count == 0 ? L"没有找到匹配项目" : L"Enter 应用 · Tab 切换效果/预设 · 右键编辑");
}

void rebuild_customized_catalog() {
    g_state->catalog = g_state->base_catalog;
    xfx::apply_catalog_customizations(g_state->catalog, g_state->settings);
    for (auto& item : g_state->catalog) xfx::enrich_catalog_item(item);
    g_state->engine = xfx::SearchEngine(g_state->catalog);
}

void update_catalog_view() {
    size_t effects = 0;
    size_t presets = 0;
    for (const auto& item : g_state->catalog) {
        if (item.item_type == L"effect") ++effects;
        if (item.item_type == L"preset") ++presets;
    }
    wchar_t count_text[128] = {};
    swprintf_s(count_text, L"%zu 个效果  ·  %zu 个预设", effects, presets);
    SetWindowTextW(g_state->count, count_text);
    populate_results();
}

void refresh_catalog() {
    BusyFeedback feedback(L"正在扫描效果和预设…");
    set_status(L"正在扫描 AE 效果和预设...");
    g_state->base_catalog = scan_catalog();
    rebuild_customized_catalog();
    // Optional support diagnostic, useful for validating a host without touching user data.
    auto report_path = wide_buffer();
    const DWORD report_length = GetEnvironmentVariableW(L"XFX_NATIVE_SMOKE_REPORT", report_path.data(),
        static_cast<DWORD>(report_path.size()));
    if (report_length && report_length < report_path.size()) {
        std::ofstream report(fs::path(report_path.data()), std::ios::binary);
        report << "catalog=" << g_state->catalog.size() << "\n";
        for (const auto* query : {L"高斯模糊", L"gaosimohu", L"gsmh", L"saoguang", L"sg"}) {
            const auto matches = g_state->engine.search(query);
            report << wide_to_utf8(query) << "=" << (matches.empty() ? "" : matches.front().item.match_name) << "\n";
        }
    }
    g_state->catalog_initialized = true;
    update_catalog_view();
    set_status(L"扫描完成",true);
}

// Detects whether AE is currently waiting on a modal dialog. AEGP_ExecuteScript
// fails outright in that state ("cannot run a script while a modal dialog is
// waiting"), which surfaced as a spurious error dialog on the first use after every
// AE restart. Detection is by window state, not by our own depth counter:
//   * a thread-modal dialog belongs to the AE UI thread and disables its owner, or
//   * AE's main window is disabled while some popup it owns is up.
// Returns false when AE is genuinely idle, so normal scripting is unaffected.
#ifdef XFX_CONSOLE_TEST
std::optional<bool> g_test_force_modal;
std::optional<bool> g_test_scripting_available = true;
int g_test_message_box_count = 0;
#endif

bool ae_modal_pending() {
#ifdef XFX_CONSOLE_TEST
    if (g_test_force_modal) return *g_test_force_modal;
#endif
    // AE runs every modal dialog on its main UI thread and DISABLES its main window
    // while that loop is active. That disabled state is the one signal the host itself
    // maintains, so it is used as the authority here. AEGP_IsScriptingAvailable is not
    // trustworthy for this: it reports "available" even while a modal loop is running
    // (confirmed by apply-trace.log, which recorded success status with an error text).
    struct Probe {
        DWORD pid;
        HWND console;
        bool disabled_top_level = false;
        bool disabled_owner = false;
    } probe{GetCurrentProcessId(), g_state ? g_state->window : nullptr, false, false};
    EnumWindows([](HWND window, LPARAM raw) -> BOOL {
        auto* state = reinterpret_cast<Probe*>(raw);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid != state->pid) return TRUE;
        if (!IsWindowVisible(window)) return TRUE;
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if ((style & WS_CHILD) != 0) return TRUE;
        if (window == state->console) return TRUE;
        // Any disabled visible top-level window of this process means a modal loop owns
        // the thread; AE does exactly this to its main window.
        if (!IsWindowEnabled(window)) state->disabled_top_level = true;
        const HWND owner = GetWindow(window, GW_OWNER);
        if (owner && owner != state->console && !IsWindowEnabled(owner) && IsWindowVisible(owner))
            state->disabled_owner = true;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&probe));
    return probe.disabled_top_level || probe.disabled_owner;
}

// AE's own wording when a script is refused because a modal dialog is up. Matched
// case-insensitively on both the English and Simplified Chinese host strings so the
// check works on either UI language.
bool is_modal_script_rejection(const std::string& message) {
    if (message.empty()) return false;
    std::string lower = message;
    for (auto& ch : lower) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    // AE's Simplified-Chinese wording is "当模式对话框正在等待回应时，无法运行脚本".
    // The previous needle spelled it 模态对话框 - one character apart - so it matched
    // nothing and the retry never ran. Keep both spellings plus the action clause:
    // matching any one of them is enough, and a false positive only delays an apply
    // that would have failed anyway.
    for (const char* needle : {
             "modal dialog", "while a modal", "modal loop", "cannot run a script",
             // 模式对话框 (what AE 25.4 actually prints)
             "\xe6\xa8\xa1\xe5\xbc\x8f\xe5\xaf\xb9\xe8\xaf\x9d\xe6\xa1\x86",
             // 模态对话框 (the other spelling)
             "\xe6\xa8\xa1\xe6\x80\x81\xe5\xaf\xb9\xe8\xaf\x9d\xe6\xa1\x86",
             // 无法运行脚本 (the action clause)
             "\xe6\x97\xa0\xe6\xb3\x95\xe8\xbf\x90\xe8\xa1\x8c\xe8\x84\x9a\xe6\x9c\xac"}) {
        if (lower.find(needle) != std::string::npos) return true;
    }
    return false;
}

xfx::ScriptReply run_ae_script_now(const std::string& script, bool show_feedback);
xfx::ScriptReply run_ae_script(const std::string& script, bool show_feedback = true);
void show_console(bool visible);

// Asks the host whether scripting may run right now. Cheap, side-effect free, and the
// only reliable signal: AEGP_ExecuteScript fails (and AE shows its own error dialog)
// exactly when this returns false.
// Diagnostic log for the intermittent modal-script failure. Written next to the
// settings file so a real session can be inspected afterwards instead of guessing.
// One log file, always available, so a failure on a machine we cannot inspect still
// produces something to read. Errors are always written; verbose detail only when the
// user turns diagnostic logging on in the settings page.
std::wstring trace_log_path() {
    try { return (settings_path().parent_path() / L"effect-console.log").wstring(); }
    catch (...) { return {}; }
}

int show_message_box(HWND owner, const wchar_t* text, const wchar_t* caption, UINT type) {
#ifdef XFX_CONSOLE_TEST
    ++g_test_message_box_count;
#endif
    return MessageBoxW(owner, text, caption, type);
}

void update_trace(const std::string& text, bool always) {
    // Always record. Diagnostics are not optional: a problem on someone else's machine
    // is only debuggable if the evidence was captured before anyone noticed.
    (void)always;
    const std::wstring path = trace_log_path();
    if (path.empty()) return;
    try {
        // Keep the file bounded: a log that grows forever is worse than no log.
        const auto file = fs::path(path);
        std::error_code size_error;
        const auto bytes = fs::exists(file, size_error) ? fs::file_size(file, size_error) : 0;
        if (!size_error && bytes > 1024 * 1024) fs::remove(file, size_error);
        SYSTEMTIME now{};
        GetLocalTime(&now);
        char stamp[32]{};
        sprintf_s(stamp, "%04d-%02d-%02d %02d:%02d:%02d ", now.wYear, now.wMonth, now.wDay,
                  now.wHour, now.wMinute, now.wSecond);
        std::ofstream out(file, std::ios::app | std::ios::binary);
        out << stamp << text << "\r\n";
        out.flush();
        if (!out) {
            // The primary location is not writable. A log nobody can read is the same as no
            // log at all, so try the user's local application data once.
            out.close();
            wchar_t local[MAX_PATH]{};
            if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local) == S_OK) {
                const auto fallback = fs::path(local) / L"XFXConsole" / L"effect-console.log";
                std::error_code make_error;
                fs::create_directories(fallback.parent_path(), make_error);
                std::ofstream alternate(fallback, std::ios::app | std::ios::binary);
                alternate << stamp << text << "\r\n";
            }
        }
    } catch (...) {}
}

bool host_scripting_available() {
#ifdef XFX_CONSOLE_TEST
    if (g_test_scripting_available) return *g_test_scripting_available;
#endif
    if (!g_basic) return false;
    try {
        AEGP_SuiteHandler suites(g_basic);
        A_Boolean available = FALSE;
        const A_Err probe_error = suites.UtilitySuite6()->AEGP_IsScriptingAvailable(&available);
        if (probe_error != A_Err_NONE) {
            update_trace("probe-error err=" + std::to_string(static_cast<int>(probe_error)));
            return true;
        }
        if (!available) update_trace("probe-unavailable");
        return available != FALSE;
    } catch (...) {
        // Fail open: if the probe itself cannot run, let the normal path try.
        return true;
    }
}

// Host-affecting work is staged here and executed from the AEGP idle hook, never from
// a window message handler. A window message can arrive while AE is inside its own
// modal loop, and calling AEGP_ExecuteScript from there is exactly what produced the
// intermittent "cannot run a script while a modal dialog is waiting" error.
struct PendingApply {
    std::string script;
    std::wstring item_key;      // remembered for the recents list; empty for none
    bool remember = false;
    bool close_console = true;  // hide the palette while AE renders, as before
};
std::deque<PendingApply> g_pending_applies;
// When the oldest staged request started waiting, so a host that never reports
// readiness cannot stall the queue forever.
ULONGLONG g_pending_apply_since = 0;
// First moment the idle hook ran. AE's scripting engine is not fully usable in the very
// first moments after startup, which is why the error appeared only on the first apply
// after a restart. Host work is held back for a short settle window.
ULONGLONG g_first_idle_tick = 0;
// Number of consecutive idle callbacks where both readiness signals were clear.
int g_apply_clear_streak = 0;
constexpr int kRequiredClearStreak = 3;
constexpr ULONGLONG kStartupSettleMs = 1200;
// How long a modal-script refusal is treated as "wait and retry". After a restart AE
// can hold its own dialog open for a few seconds while the project loads; the apply
// must survive that quietly instead of showing an error the user never asked for.
constexpr ULONGLONG kModalRetryBudgetMs = 20000;

// Stages an apply for the next idle callback. Returns immediately.
void stage_apply(std::string script, std::wstring item_key, bool remember, bool close_console) {
    if (g_pending_applies.size() >= 8) g_pending_applies.pop_front();
    g_pending_applies.push_back({std::move(script), std::move(item_key), remember, close_console});
}

// Public entry point for host work. Everything goes through the idle queue so no host
// call can ever happen inside a window message (which is where AE's modal-script error
// came from). The direct path is kept only for the idle runner itself.
xfx::ScriptReply run_ae_script(const std::string& script, bool show_feedback) {
    if (!host_scripting_available() || ae_modal_pending()) {
        stage_apply(script, L"", false, show_feedback);
        xfx::ScriptReply deferred;
        deferred.pending = true;
        deferred.message = "AE 正在等待对话框关闭，操作已排队";
        return deferred;
    }
    return run_ae_script_now(script, show_feedback);
}

// Executes one staged apply. Only called from the AEGP idle hook, which is the one
// context where AE is guaranteed not to be inside a modal loop.
bool run_pending_apply_on_idle() {
    if (g_pending_applies.empty()) return false;
    // Hold host work briefly after startup: AE's scripting engine is not ready in the
    // first moments, and reaching it too early is what produced the modal error on the
    // first apply of a session.
    if (g_first_idle_tick && GetTickCount64() - g_first_idle_tick < kStartupSettleMs) return false;
    // Both signals are required: the host probe alone reported "available" during a real
    // modal loop (see apply-trace.log), and the window check alone can be fooled by our
    // own hidden palette. Waiting for both to be clear is what actually prevents AE from
    // refusing the call.
    // Require a few consecutive clear idles before acting: AE briefly reports a usable
    // scripting state while a modal loop is still unwinding, and acting on that single
    // sample is what produced the intermittent refusal.
    if (!host_scripting_available() || ae_modal_pending()) {
        g_apply_clear_streak = 0;
        const ULONGLONG now = GetTickCount64();
        if (!g_pending_apply_since) {
            g_pending_apply_since = now;
            // Say once, in our own status line, that the work is waiting. This replaces
            // the old "run it anyway after 20s" valve: running it during a modal loop is
            // exactly the call AE answers with its own error dialog, which is the popup
            // the user saw on the first apply after starting AE.
            set_status(L"AE 正在等待对话框关闭，操作已排队");
        }
        // No timeout. A modal loop either ends (and the apply runs) or the request stays
        // queued; forcing it produces the host dialog, which is worse than waiting.
        return false;
    }
    // Both signals clear on this idle; require a short streak so a transient "ready"
    // report during modal teardown cannot slip through.
    if (++g_apply_clear_streak < kRequiredClearStreak) return false;
    g_apply_clear_streak = 0;
    g_pending_apply_since = 0;
    auto job = std::move(g_pending_applies.front());
    g_pending_applies.pop_front();
    // Hand keyboard state back before touching the host. AE refuses scripting while it
    // believes a dialog of its own owns the modal loop; leaving our topmost palette as
    // the foreground window is exactly the condition that reproduces the failure. This
    // is released, not closed, so the palette returns afterwards.
    const HWND console_window = g_state ? g_state->window : nullptr;
    const bool console_was_visible = console_window && IsWindowVisible(console_window);
    // Mark the apply as in flight BEFORE hiding the palette and pumping messages. The
    // teardown path treats "hidden" as "the user closed it", so without this the queued
    // close destroys the palette mid-apply and a failed apply loses the console.
    if (g_state) g_state->applying_item = true;
    if (console_was_visible) ShowWindow(console_window, SW_HIDE);
    if (GetCapture() == console_window || (console_window && IsChild(console_window, GetCapture())))
        ReleaseCapture();
    // Give the host a moment to process its own message queue without our window.
    for (int spin = 0; spin < 3; ++spin) { MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(1); }
    ModalGuard modal;
    struct Presentation {
        HWND window = g_state ? g_state->window : nullptr;
        // Visibility must be the state from BEFORE the runner hid the palette for the
        // host call, otherwise a failed apply never brings the console back.
        bool was_visible = false;
        bool close_console = true;
        bool applied = false;
        // True while the work was re-queued (the host was mid-dialog). The palette must
        // stay hidden until the real outcome arrives, otherwise it flashes once per idle
        // tick for as long as the retry budget lasts.
        bool deferred = false;
        ~Presentation() {
            if (!g_state) return;
            g_state->applying_item = false;
            // Close only when the apply actually succeeded. A failure keeps the palette
            // open so the user still sees the search and the reason it failed.
            // Close here rather than posting WM_CLOSE: a queued close can outlive this
            // window and destroy a palette opened later in the same session.
            if (close_console && applied && IsWindow(window)) SendMessageW(window, WM_CLOSE, 0, 0);
            else if (deferred && IsWindow(window)) return;
            else if (was_visible && IsWindow(window)) show_console(true);
        }
    } presentation;
    presentation.was_visible = console_was_visible;
    presentation.close_console = job.close_console;
    if (g_state) g_state->applying_item = true;
    if (job.close_console && presentation.was_visible) {
        if (g_state->tooltip) SendMessageW(g_state->tooltip, TTM_POP, 0, 0);
        ShowWindow(presentation.window, SW_HIDE);
    }
    if (g_state) { g_state->ime.restore(); g_state->ime_session_active = false; }
    const auto reply = run_ae_script_now(job.script, false);
    presentation.applied = reply.succeeded;
    if (reply.pending) {
        // The precheck inside run_ae_script_now parked it again; nothing to report yet.
        // Keep the palette hidden until the retried attempt produces a real outcome.
        presentation.deferred = job.close_console;
        return true;
    }
    if ((reply.succeeded || reply.count) && job.remember && !job.item_key.empty() && g_state) {
        auto& recent = g_state->settings.recent;
        recent.erase(std::remove(recent.begin(), recent.end(), job.item_key), recent.end());
        recent.insert(recent.begin(), job.item_key);
        if (recent.size() > 20) recent.resize(20);
    }
    if (reply.succeeded && !reply.partial) {
        set_status(L"已应用到 " + std::to_wstring(reply.count) + L" 个所选图层 · Ctrl+Z 撤销", true);
    } else {
        // Partial is not success: keep the palette open and name what failed.
        set_status((reply.partial ? L"部分完成：" : L"操作失败：") + utf8_to_wide(reply.message.c_str()));
    }
    return true;
}

xfx::ScriptReply run_ae_script_now(const std::string& script, bool show_feedback = true) {
    ModalGuard modal;
    std::unique_ptr<BusyFeedback> feedback;
    if (show_feedback) feedback = std::make_unique<BusyFeedback>(L"正在处理 AE 操作…");
    xfx::ScriptReply reply;
#ifdef XFX_CONSOLE_TEST
    g_test_last_script = script;
    if (g_test_before_script) g_test_before_script();
    if (g_test_script_reply) return *g_test_script_reply;
#endif
    if (!g_basic) { reply.message = "AE host unavailable"; return reply; }
    AEGP_MemHandle result_handle = nullptr, error_handle = nullptr;
    try {
        AEGP_SuiteHandler suites(g_basic);

        const auto read_handle = [&](AEGP_MemHandle handle) {
            std::string text;
            if (!handle) return text;
            void* data = nullptr;
            AEGP_MemSize size = 0;
            if (!suites.MemorySuite1()->AEGP_GetMemHandleSize(handle, &size) &&
                !suites.MemorySuite1()->AEGP_LockMemHandle(handle, &data)) {
                if (data && size) {
                    const char* bytes = static_cast<const char*>(data);
                    size_t length = 0;
                    while (length < size && bytes[length]) ++length;
                    text.assign(bytes, length);
                }
                suites.MemorySuite1()->AEGP_UnlockMemHandle(handle);
            }
            return text;
        };
        update_trace("execute-begin");
        const A_Err error = suites.UtilitySuite6()->AEGP_ExecuteScript(
            g_plugin_id, script.c_str(), FALSE, &result_handle, &error_handle);
        const std::string result = read_handle(result_handle);
        const std::string script_error = read_handle(error_handle);
        update_trace("execute-end err=" + std::to_string(static_cast<int>(error)) +
            " msg=" + script_error.substr(0, 160));
        // AE refuses scripting while its own modal dialog is up. Detect that exact
        // failure and park the script for the next idle callback instead of letting
        // the host error dialog reach the user. Window-state probing alone cannot be
        // trusted (the console may not exist yet on the very first use), so the
        // rejection itself is the authoritative signal.
        // Retry once on ANY scripting failure during the startup settle window: the host
        // can reject the first call even when its probe said it was ready, and a single
        // retry turns that into a brief delay instead of an error dialog.
        const bool early_session = g_first_idle_tick &&
            GetTickCount64() - g_first_idle_tick < 15000;
        // The host reports this failure with a SUCCESS status and only the text says
        // what happened, so the status code alone can never detect it. Check the text
        // first; that is the only signal that ever fires for this case.
        const bool modal_text = is_modal_script_rejection(script_error) ||
            is_modal_script_rejection(result);
        if (modal_text) update_trace("modal-rejection: " + script_error.substr(0, 200), true);
        // Retry while the host is still unwinding its own dialog. This is the popup the
        // user saw on the very first apply after starting AE: AE refuses the script,
        // returns the refusal as text, and a single retry was not enough because the
        // modal loop can outlive it by seconds. Retry on a wall-clock budget instead of
        // a count - the idle hook fires many times per second.
        static thread_local ULONGLONG g_modal_retry_since = 0;
        const ULONGLONG retry_now = GetTickCount64();
        if (modal_text) { if (!g_modal_retry_since) g_modal_retry_since = retry_now; }
        else g_modal_retry_since = 0;
        const bool retry_modal = modal_text && g_modal_retry_since &&
            retry_now - g_modal_retry_since < kModalRetryBudgetMs;
        if (retry_modal || (error && early_session)) {
            if (result_handle) { try { suites.MemorySuite1()->AEGP_FreeMemHandle(result_handle); } catch (...) {} result_handle = nullptr; }
            if (error_handle) { try { suites.MemorySuite1()->AEGP_FreeMemHandle(error_handle); } catch (...) {} error_handle = nullptr; }
            if (g_pending_applies.size() < 8) g_pending_applies.push_back({script, L"", false, true});
            xfx::ScriptReply deferred;
            deferred.pending = true;
            deferred.message = "AE 正在等待对话框关闭，操作已排队";
            return deferred;
        }
        if (result_handle) { suites.MemorySuite1()->AEGP_FreeMemHandle(result_handle); result_handle = nullptr; }
        if (error_handle) { suites.MemorySuite1()->AEGP_FreeMemHandle(error_handle); error_handle = nullptr; }
        reply = xfx::parse_script_reply(result);
        if (error) {
            reply.succeeded = false;
            reply.message = script_error.empty() ? "AE script execution failed" : script_error;
        }
    } catch (...) {
        try {
            AEGP_SuiteHandler suites(g_basic);
            if (result_handle) suites.MemorySuite1()->AEGP_FreeMemHandle(result_handle);
            if (error_handle) suites.MemorySuite1()->AEGP_FreeMemHandle(error_handle);
        } catch (...) {}
        reply.message = "AE scripting unavailable";
    }
    return reply;
}

void show_console(bool visible);

struct ApplyPresentation {
    HWND window = g_state->window;
    bool was_visible = IsWindowVisible(window) != FALSE;
    bool completed = false;
    ApplyPresentation() {
        g_state->applying_item = true;
        if (was_visible) {
            if (g_state->tooltip) SendMessageW(g_state->tooltip, TTM_POP, 0, 0);
            if (GetCapture() == window || IsChild(window, GetCapture())) ReleaseCapture();
            // Hide once before AE can pump messages or redraw its composition.
            ShowWindow(window, SW_HIDE);
        }
        g_state->ime.restore();
        g_state->ime_session_active = false;
    }
    ~ApplyPresentation() {
        if (g_state) g_state->applying_item = false;
        if (!completed && was_visible && IsWindow(window)) show_console(true);
    }
    void finish() {
        completed = true;
        if (IsWindow(window)) SendMessageW(window, WM_CLOSE, 0, 0);
    }
};

void execute_layer_style(size_t index) {
    if (!g_state || index >= xfx::layer_styles.size() || g_state->applying_item) return;
    // Same staging rule as effects: never call the host from a window message.
    stage_apply(xfx::script_apply_layer_style(xfx::layer_styles[index].match_name), L"", false, true);
}

xfx::ScriptReply execute_item(const xfx::CatalogItem& selected_item) {
    if (g_state->applying_item)
        return xfx::ScriptReply{false, false, 0, "An effect is already being applied"};
    // Copy before executing: AE may pump messages while running ExtendScript.
    const auto item = selected_item;
    const auto override = g_state->settings.overrides.find(item_key(item));
    const auto script = override != g_state->settings.overrides.end()
        ? xfx::script_apply_preset(override->second) : (item.item_type == L"preset"
        ? xfx::script_apply_preset(item.file_path) : xfx::script_apply_effect(item.match_name));
    // Stage the apply for the next idle callback. Running AEGP_ExecuteScript straight
    // from this window message is what intermittently hit AE's modal-script error.
    stage_apply(script, item_key(item), true, true);
    xfx::ScriptReply staged;
    staged.succeeded = true;
    staged.message = "已排队应用";
    return staged;
}

fs::path capture_directory() {
    if (g_state && !g_state->settings.capture_directory.empty()) return fs::path(g_state->settings.capture_directory);
    return settings_path().parent_path() / L"Capture";
}

void update_gallery_selection();
bool handle_gallery_key(WPARAM key);

void refresh_gallery() {
    if (!g_state || !g_state->gallery_list) return;
    g_state->captures = xfx::scan_captures(capture_directory());
    SendMessageW(g_state->gallery_list, LB_RESETCONTENT, 0, 0);
    for (const auto& path : g_state->captures) {
        SendMessageW(g_state->gallery_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(path.filename().c_str()));
    }
    if (!g_state->captures.empty()) {
        SendMessageW(g_state->gallery_list, LB_SETSEL, TRUE, 0);
        SendMessageW(g_state->gallery_list, LB_SETCARETINDEX, 0, 0);
    }
    update_gallery_selection();
}

void clear_thumbnail_cache() {
    ModalGuard modal;
    const auto result = xfx::clear_thumbnail_cache();
    if (result.failed) {
        set_status(L"缩略图缓存清理失败，请检查 LocalAppData 写入权限");
        return;
    }
    if (g_state->gallery_window) refresh_gallery();
    set_status(L"已清除缩略图缓存 " + std::to_wstring(result.deleted) + L" 个文件；图库已重建",true);
}

void validate_capture_directory(const fs::path& directory) {
    if (!directory.empty() && (!directory.is_absolute() || !fs::is_directory(directory)))
        throw std::invalid_argument("请选择一个已存在的快照保存文件夹。");
}

void set_capture_directory(const fs::path& directory) {
    validate_capture_directory(directory);
    auto updated = g_state->settings;
    updated.capture_directory = directory.wstring();
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    SetWindowTextW(g_state->capture_location, capture_directory().c_str());
    refresh_gallery();
    set_status(L"快照保存位置已更新；原目录中的图片保留不变",true);
}

void choose_capture_directory() {
    ModalGuard modal;
    struct ComSession {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ~ComSession() { if (SUCCEEDED(result)) CoUninitialize(); }
    } session;
    if (FAILED(session.result) && session.result != RPC_E_CHANGED_MODE)
        throw std::runtime_error("无法打开文件夹选择窗口。");
    const auto check = [](HRESULT result) {
        if (FAILED(result)) throw std::runtime_error("无法选择快照保存文件夹。");
    };
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    check(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    FILEOPENDIALOGOPTIONS options{};
    check(dialog->GetOptions(&options));
    check(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM));
    check(dialog->SetTitle(L"选择快照保存文件夹"));
    Microsoft::WRL::ComPtr<IShellItem> initial;
    if (SUCCEEDED(SHCreateItemFromParsingName(capture_directory().c_str(), nullptr, IID_PPV_ARGS(&initial))))
        check(dialog->SetFolder(initial.Get()));
    const HRESULT shown = dialog->Show(g_state->window);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
    check(shown);
    Microsoft::WRL::ComPtr<IShellItem> selected;
    check(dialog->GetResult(&selected));
    PWSTR path = nullptr;
    check(selected->GetDisplayName(SIGDN_FILESYSPATH, &path));
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> chosen(path, CoTaskMemFree);
    if (!path || !*path) return;
    set_capture_directory(fs::path(path));
}

xfx::ScriptReply capture_frame(fs::path* saved_path = nullptr) {
    ModalGuard modal;
    std::error_code error;
    fs::create_directories(capture_directory(), error);
    if (error) {
        set_status(L"无法创建快照目录");
        xfx::ScriptReply failure; failure.message = wide_to_utf8(L"无法创建快照目录"); return failure;
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[128] = {};
    swprintf_s(name, L"快照-%04u%02u%02u-%02u%02u%02u-%03u-%llu.png", now.wYear, now.wMonth,
        now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetTickCount64());
    const auto file = capture_directory() / name;
    const auto reply = xfx::capture_comp_png(g_basic, g_plugin_id, file, true);
    set_status(reply.succeeded ? L"当前合成帧已保存为 PNG · 点击图库查看" :
        L"快照失败：" + utf8_to_wide(reply.message.c_str()),reply.succeeded);
    if (reply.succeeded) {
        if (saved_path) *saved_path = file;
        refresh_gallery();
    }
    return reply;
}

std::vector<fs::path> selected_captures() {
    std::vector<fs::path> files;
    if (!g_state || !g_state->gallery_list) return files;
    const LRESULT count = SendMessageW(g_state->gallery_list, LB_GETSELCOUNT, 0, 0);
    if (count <= 0) return files;
    std::vector<int> rows(static_cast<size_t>(count));
    const LRESULT received = SendMessageW(g_state->gallery_list, LB_GETSELITEMS, count,
        reinterpret_cast<LPARAM>(rows.data()));
    for (LRESULT i = 0; i < received; ++i) {
        const int row = rows[static_cast<size_t>(i)];
        if (row >= 0 && static_cast<size_t>(row) < g_state->captures.size()) files.push_back(g_state->captures[row]);
    }
    return files;
}

fs::path selected_capture() {
    const auto files = selected_captures();
    return files.size() == 1 ? files.front() : fs::path{};
}

void gallery_status(const std::wstring& text) {
    if (g_state->gallery_status) SetWindowTextW(g_state->gallery_status, text.c_str());
}

void rebuild_gallery_preview(const std::vector<fs::path>& files) {
    g_state->gallery_drag_view = -1;
    if (GetCapture() == g_state->gallery_canvas) ReleaseCapture();
    std::vector<xfx::GallerySource> sources;
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> images;
    const size_t count = (std::min)(files.size(), size_t{4});
    sources.reserve(count); images.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        xfx::GallerySource source{files[index], 0, 0};
        std::unique_ptr<Gdiplus::Bitmap> decoded;
        {
            // Decode into a new pixel buffer; Bitmap::Clone can retain the file decoder.
            Gdiplus::Bitmap file(files[index].c_str());
            if (file.GetLastStatus() == Gdiplus::Ok && file.GetWidth() && file.GetHeight() &&
                file.GetWidth() <= INT_MAX && file.GetHeight() <= INT_MAX) {
                decoded = std::make_unique<Gdiplus::Bitmap>(static_cast<INT>(file.GetWidth()),
                    static_cast<INT>(file.GetHeight()), PixelFormat32bppARGB);
                Gdiplus::Status copied = Gdiplus::GenericError;
                if (decoded->GetLastStatus() == Gdiplus::Ok) {
                    Gdiplus::Graphics pixels(decoded.get());
                    pixels.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                    copied = pixels.DrawImage(&file, Gdiplus::Rect(0, 0,
                        static_cast<INT>(file.GetWidth()), static_cast<INT>(file.GetHeight())),
                        0, 0, static_cast<INT>(file.GetWidth()), static_cast<INT>(file.GetHeight()), Gdiplus::UnitPixel);
                }
                if (copied == Gdiplus::Ok) {
                    source.width = decoded->GetWidth(); source.height = decoded->GetHeight();
                } else decoded.reset();
            }
        }
        sources.push_back(std::move(source)); images.push_back(std::move(decoded));
    }
    auto next = g_state->gallery_model;
    next.set_images(sources);
    g_state->gallery_model = std::move(next);
    g_state->gallery_images.swap(images);
    if (g_state->gallery_canvas) InvalidateRect(g_state->gallery_canvas, nullptr, FALSE);
}

void layout_gallery(HWND hwnd) {
    RECT client{}; GetClientRect(hwnd, &client);
    const int width = (std::max)(0L, client.right), height = (std::max)(0L, client.bottom);
    const int body_top = 40, body_bottom = (std::max)(body_top, height - 8);
    const int body_width = (std::max)(0, width - 16);
    if (g_state->gallery_split < 0) g_state->gallery_split = body_width / 4;
    const int split = (std::min)(std::clamp(g_state->gallery_split, 100, (std::max)(100, body_width - 103)), body_width);
    g_state->gallery_split = split;
    MoveWindow(g_state->gallery_list, 8, body_top, split, body_bottom - body_top, TRUE);
    const int thumbnail_height = (std::max)(24, static_cast<int>((std::max)(0, split - 19) * 0.5625) + 22);
    SendMessageW(g_state->gallery_list, LB_SETITEMHEIGHT, 0, thumbnail_height);
    g_state->gallery_splitter = {8 + split, body_top, 11 + split, body_bottom};
    MoveWindow(g_state->gallery_canvas, 11 + split, body_top, (std::max)(0, body_width - split - 3),
        body_bottom - body_top, TRUE);
    MoveWindow(g_state->gallery_status, 256, 12, (std::max)(0, width - 264), 20, TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool route_gallery_wheel(HWND receiver, WPARAM wparam, LPARAM lparam) {
    if (!g_state) return false;
    const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    for (HWND target : {g_state->gallery_canvas, g_state->gallery_list}) {
        RECT bounds{};
        if (IsWindow(target) && GetWindowRect(target, &bounds) && PtInRect(&bounds, point)) {
            if (target == receiver) return false;
            SendMessageW(target, WM_MOUSEWHEEL, wparam, lparam);
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK gallery_canvas_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
        if (!g_state) return DefWindowProcW(hwnd, message, wparam, lparam);
        if (message == WM_SIZE) {
            RECT client{}; GetClientRect(hwnd, &client);
            g_state->gallery_model.set_bounds({0, 0, double(client.right), double(client.bottom)});
            InvalidateRect(hwnd, nullptr, FALSE); return 0;
        }
        if (message == WM_LBUTTONDOWN) {
            SetFocus(hwnd);
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            const auto& views = g_state->gallery_model.views();
            for (size_t index = 0; index < views.size(); ++index) {
                const auto& cell = views[index].cell;
                if (point.x >= cell.x && point.x < cell.x + cell.width && point.y >= cell.y &&
                    point.y < cell.y + cell.height && views[index].source.width && views[index].source.height) {
                    g_state->gallery_drag_view = static_cast<int>(index); g_state->gallery_drag_start = point;
                    g_state->gallery_drag_offset_x = views[index].offset_x;
                    g_state->gallery_drag_offset_y = views[index].offset_y;
                    SetCapture(hwnd); break;
                }
            }
            return 0;
        }
        if (message == WM_MOUSEMOVE && g_state->gallery_drag_view >= 0 && GetCapture() == hwnd) {
            g_state->gallery_model.set_offset(static_cast<size_t>(g_state->gallery_drag_view),
                g_state->gallery_drag_offset_x + GET_X_LPARAM(lparam) - g_state->gallery_drag_start.x,
                g_state->gallery_drag_offset_y + GET_Y_LPARAM(lparam) - g_state->gallery_drag_start.y);
            InvalidateRect(hwnd, nullptr, FALSE); return 0;
        }
        if (message == WM_LBUTTONUP && g_state->gallery_drag_view >= 0) {
            g_state->gallery_drag_view = -1; if (GetCapture() == hwnd) ReleaseCapture(); return 0;
        }
        if (message == WM_CAPTURECHANGED) g_state->gallery_drag_view = -1;
        if (message == WM_MOUSEWHEEL) {
            if (route_gallery_wheel(hwnd, wparam, lparam)) return 0;
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}; ScreenToClient(hwnd, &point);
            const auto delta = GET_WHEEL_DELTA_WPARAM(wparam);
            const auto& views = g_state->gallery_model.views();
            for (size_t index = 0; delta && index < views.size(); ++index) {
                const auto& cell = views[index].cell;
                if (point.x >= cell.x && point.x < cell.x + cell.width && point.y >= cell.y && point.y < cell.y + cell.height) {
                    g_state->gallery_model.zoom_at(index, delta > 0 ? 1.1 : 1.0 / 1.1, point.x, point.y);
                    InvalidateRect(hwnd, nullptr, FALSE); break;
                }
            }
            return 0;
        }
        if (message == WM_KEYDOWN && handle_gallery_key(wparam)) return 0;
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_PAINT || message == WM_PRINTCLIENT) {
            PAINTSTRUCT paint{};
            HDC dc = message == WM_PAINT ? BeginPaint(hwnd, &paint) : reinterpret_cast<HDC>(wparam);
            RECT client{}; GetClientRect(hwnd, &client);
            FillRect(dc, &client, g_state->background_brush);
            const auto& views = g_state->gallery_model.views();
            if (views.empty()) {
                SelectObject(dc, g_state->font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, colors().muted);
                DrawTextW(dc, L"选择左侧快照进行预览", -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            for (size_t index = 0; index < views.size(); ++index) {
                const auto& cell = views[index].cell;
                if (cell.width <= 0 || cell.height <= 0) continue;
                const auto bounds = g_state->gallery_model.image_rect(index);
                Gdiplus::Graphics graphics(dc);
                graphics.SetClip(Gdiplus::RectF(static_cast<float>(cell.x), static_cast<float>(cell.y),
                    static_cast<float>(cell.width), static_cast<float>(cell.height)));
                if (index < g_state->gallery_images.size() && g_state->gallery_images[index]) {
                    graphics.SetInterpolationMode(views[index].scale == 1.0 ?
                        Gdiplus::InterpolationModeNearestNeighbor : Gdiplus::InterpolationModeHighQualityBicubic);
                    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                    graphics.DrawImage(g_state->gallery_images[index].get(),
                        Gdiplus::RectF(static_cast<float>(bounds.x), static_cast<float>(bounds.y),
                            static_cast<float>(bounds.width), static_cast<float>(bounds.height)),
                        0, 0, static_cast<float>(views[index].source.width),
                        static_cast<float>(views[index].source.height), Gdiplus::UnitPixel);
                } else {
                    Gdiplus::SolidBrush ink(Gdiplus::Color(255, GetRValue(colors().muted),
                        GetGValue(colors().muted), GetBValue(colors().muted)));
                    Gdiplus::Font font(dc, g_state->font);
                    Gdiplus::StringFormat format; format.SetAlignment(Gdiplus::StringAlignmentCenter);
                    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    graphics.DrawString(L"图片无法读取", -1, &font,
                        Gdiplus::RectF(static_cast<float>(cell.x), static_cast<float>(cell.y),
                            static_cast<float>(cell.width), static_cast<float>(cell.height)), &format, &ink);
                }
            }
            if (message == WM_PAINT) EndPaint(hwnd, &paint);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    });
}

void update_gallery_selection() {
    if (!g_state->gallery_list) return;
    const auto files = selected_captures();
    const HWND parent = GetParent(g_state->gallery_list);
    for (int id : {kPreviewId, kCopyCaptureId}) EnableWindow(GetDlgItem(parent, id), files.size() == 1);
    for (int id : {kSaveCaptureId, kImportCaptureId, kDeleteCaptureId}) EnableWindow(GetDlgItem(parent, id), !files.empty());
    rebuild_gallery_preview(files);
    gallery_status(L"共 " + std::to_wstring(g_state->captures.size()) + L" 张 · 已选 " +
        std::to_wstring(files.size()) + L" 张 · Z 原尺寸 · A 方向");
}

fs::path g_preview_path;
RECT g_preview_bounds{};
LRESULT CALLBACK preview_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE && g_state &&
        g_state->settings.auto_close && !g_state->settings.fixed_window && g_state->modal_depth == 1) {
        const HWND next = reinterpret_cast<HWND>(lparam);
        if (!next || GetAncestor(next, GA_ROOTOWNER) != g_state->window) PostMessageW(g_state->window, WM_CLOSE, 0, 0);
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        GetClientRect(hwnd, &g_preview_bounds);
        FillRect(dc, &g_preview_bounds, g_state->background_brush);
        InflateRect(&g_preview_bounds, -12, -12);
        xfx::draw_capture(dc, g_preview_bounds, g_preview_path);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_LBUTTONDOWN) {
        Gdiplus::Bitmap image(g_preview_path.c_str());
        if (!image.GetWidth() || !image.GetHeight()) return 0;
        const double scale = (std::min)(double(g_preview_bounds.right - g_preview_bounds.left) / image.GetWidth(),
            double(g_preview_bounds.bottom - g_preview_bounds.top) / image.GetHeight());
        const int left = g_preview_bounds.left + (g_preview_bounds.right - g_preview_bounds.left - int(image.GetWidth() * scale)) / 2;
        const int top = g_preview_bounds.top + (g_preview_bounds.bottom - g_preview_bounds.top - int(image.GetHeight() * scale)) / 2;
        const int x = GET_X_LPARAM(lparam) - left, y = GET_Y_LPARAM(lparam) - top;
        unsigned int argb = 0;
        if (x >= 0 && y >= 0 && xfx::sample_image_pixel(g_preview_path,
            static_cast<unsigned int>(x / scale), static_cast<unsigned int>(y / scale), argb)) {
            wchar_t color[16] = {};
            swprintf_s(color, L"#%06X", argb & 0xFFFFFF);
            if (OpenClipboard(hwnd)) {
                HGLOBAL text = GlobalAlloc(GMEM_MOVEABLE, sizeof(color));
                if (text) {
                    if (void* bytes = GlobalLock(text)) {
                        memcpy(bytes, color, sizeof(color)); GlobalUnlock(text);
                        EmptyClipboard();
                        if (!SetClipboardData(CF_UNICODETEXT, text)) GlobalFree(text);
                    } else GlobalFree(text);
                }
                CloseClipboard();
            }
            SetWindowTextW(hwnd, (std::wstring(L"颜色已复制：") + color + L" · 点击图片取色").c_str());
        }
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) { g_state->preview_window = nullptr; return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void preview_capture() {
    const auto file = selected_capture();
    if (file.empty()) return;
    g_preview_path = file;
    const wchar_t* name = L"XFXCapturePreview";
    WNDCLASSW klass{};
    klass.hInstance = GetModuleHandleW(nullptr); klass.lpfnWndProc = preview_proc;
    klass.lpszClassName = name; klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&klass);
    HWND existing = g_state->preview_window;
    if (existing) { InvalidateRect(existing, nullptr, FALSE); SetForegroundWindow(existing); return; }
    HWND window = CreateWindowExW(0, name, L"快照预览 · 点击图片取色并复制 RGB · Esc 关闭",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 900, 620,
        g_state->gallery_window, nullptr, klass.hInstance, nullptr);
    g_state->preview_window = window;
    ShowWindow(window, SW_SHOW);
}

void save_captures_as_batch(const std::vector<fs::path>& files) {
    ModalGuard modal;
    HMENU formats = CreatePopupMenu();
    AppendMenuW(formats, MF_STRING, 1, L"批量另存 PNG（保留透明通道）");
    AppendMenuW(formats, MF_STRING, 2, L"批量另存 JPG（白色背景）");
    RECT button{}; GetWindowRect(GetDlgItem(g_state->gallery_window, kSaveCaptureId), &button);
    const auto format = xfx::track_themed_popup(formats, TPM_RETURNCMD | TPM_NONOTIFY,
        button.left, button.bottom, g_state->gallery_window, colors(), g_state->font);
    DestroyMenu(formats);
    if (format != 1 && format != 2) return;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BROWSEINFOW chooser{};
    chooser.hwndOwner = g_state->gallery_window;
    chooser.lpszTitle = L"选择批量导出的图片目录";
    chooser.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE chosen = SHBrowseForFolderW(&chooser);
    wchar_t folder[MAX_PATH]{};
    const bool selected = chosen && SHGetPathFromIDListW(chosen, folder);
    CoTaskMemFree(chosen);
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (!selected) return;
    size_t saved = 0, skipped = 0, failed = 0;
    for (const auto& source : files) {
        auto target = fs::path(folder) / source.filename();
        target.replace_extension(format == 2 ? L".jpg" : L".png");
        std::error_code error;
        if (fs::equivalent(source, target, error) && !error) { ++skipped; continue; }
        if (fs::exists(target) && show_message_box(g_state->gallery_window,
            (L"覆盖已存在的文件？\n" + target.wstring()).c_str(), L"确认替换图片",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) { ++skipped; continue; }
        if (xfx::export_capture(source, target, format == 2)) ++saved;
        else ++failed;
    }
    gallery_status(L"已导出 " + std::to_wstring(saved) + L" 张 · 跳过 " +
        std::to_wstring(skipped) + L" 张 · 失败 " + std::to_wstring(failed) + L" 张");
}

void save_capture_as() {
    const auto files = selected_captures();
    if (files.size() > 1) { save_captures_as_batch(files); return; }
    const auto source = selected_capture();
    if (source.empty()) return;
    ModalGuard modal;
    auto filename = wide_buffer();
    wcscpy_s(filename.data(), filename.size(), L"快照.png");
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = g_state->gallery_window;
    dialog.lpstrFile = filename.data(); dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrFilter = L"PNG 透明图片 (*.png)\0*.png\0JPEG 白底图片 (*.jpg)\0*.jpg\0";
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) return;
    const bool jpeg = dialog.nFilterIndex == 2;
    fs::path target(filename.data());
    target.replace_extension(jpeg ? L".jpg" : L".png");
    std::error_code error;
    if (fs::equivalent(source, target, error) && !error) {
        show_message_box(g_state->gallery_window, L"这张图片已经保存在该路径。", L"快照", MB_OK);
        return;
    }
    if (target != fs::path(filename.data()) && fs::exists(target, error) &&
        show_message_box(g_state->gallery_window, (L"覆盖已存在的文件？\n" + target.wstring()).c_str(),
            L"确认替换图片", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    const bool success = xfx::export_capture(source, target, jpeg);
    show_message_box(g_state->gallery_window, success ? L"图片已保存。" : L"图片保存失败，请检查路径。",
        L"快照导出", MB_OK | (success ? MB_ICONINFORMATION : MB_ICONERROR));
}

void copy_selected_capture() {
    const auto file = selected_capture();
    if (file.empty()) return;
    gallery_status(xfx::copy_capture_to_clipboard(g_state->gallery_window, file)
        ? L"图片已复制到剪贴板" : L"图片复制失败，请稍后重试");
}

void delete_selected_captures() {
    const auto files = selected_captures();
    if (files.empty()) return;
    ModalGuard modal;
    const auto question = L"永久删除所选的 " + std::to_wstring(files.size()) + L" 张快照？\n" +
        capture_directory().wstring() + L"\n此操作无法撤销。";
    if (show_message_box(g_state->gallery_window, question.c_str(), L"删除快照",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    const auto results = xfx::delete_captures(capture_directory(), files);
    size_t deleted = 0;
    for (const auto& result : results) if (result.deleted) ++deleted;
    refresh_gallery();
    gallery_status(L"已删除 " + std::to_wstring(deleted) + L" 张" + (deleted == files.size()
        ? L"" : L"；其余图片未删除，请检查文件权限后重试"));
}

WNDPROC g_gallery_list_proc = nullptr;
bool handle_gallery_key(WPARAM key) {
    if (!g_state || !IsWindow(g_state->gallery_list)) return false;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (key == VK_DELETE) { delete_selected_captures(); return true; }
    if (control && key == 'C') { copy_selected_capture(); return true; }
    if (control && key == 'A') {
        SendMessageW(g_state->gallery_list, LB_SETSEL, TRUE, -1); update_gallery_selection(); return true;
    }
    if (!control && (key == 'A' || key == 'Z')) {
        if (key == 'A') {
            g_state->gallery_drag_view = -1;
            if (GetCapture() == g_state->gallery_canvas) ReleaseCapture();
            g_state->gallery_model.toggle_direction();
        }
        else g_state->gallery_model.toggle_actual_size();
        InvalidateRect(g_state->gallery_canvas, nullptr, FALSE);
        gallery_status(key == 'A' ? L"已切换排列方向 · Z 原尺寸切换" : L"已切换预览缩放 · A 排列方向");
        return true;
    }
    if (key == VK_ESCAPE) { DestroyWindow(GetParent(g_state->gallery_list)); return true; }
    return false;
}

void select_gallery_row(int row, bool control, bool shift, bool right_alt) {
    HWND list = g_state->gallery_list;
    if (row < 0 || static_cast<size_t>(row) >= g_state->captures.size()) return;
    if (control && !right_alt) {
        const bool selected = SendMessageW(list, LB_GETSEL, row, 0) > 0;
        SendMessageW(list, LB_SETSEL, !selected, row);
        SendMessageW(list, LB_SETANCHORINDEX, row, 0);
    } else if (shift) {
        const auto anchor = SendMessageW(list, LB_GETANCHORINDEX, 0, 0);
        const int start = anchor >= 0 && static_cast<size_t>(anchor) < g_state->captures.size() ?
            static_cast<int>(anchor) : row;
        SendMessageW(list, LB_SETSEL, FALSE, -1);
        for (int index = (std::min)(start, row); index <= (std::max)(start, row); ++index)
            SendMessageW(list, LB_SETSEL, TRUE, index);
    } else {
        SendMessageW(list, LB_SETSEL, FALSE, -1);
        SendMessageW(list, LB_SETSEL, TRUE, row);
        SendMessageW(list, LB_SETANCHORINDEX, row, 0);
    }
    SendMessageW(list, LB_SETCARETINDEX, row, FALSE);
    update_gallery_selection();
}

LRESULT CALLBACK gallery_list_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
        if (message == WM_MOUSEWHEEL && route_gallery_wheel(hwnd, wparam, lparam)) return 0;
        if (message == WM_LBUTTONDOWN) {
            const auto hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
            if (!HIWORD(hit)) {
                SetFocus(hwnd);
                select_gallery_row(LOWORD(hit), (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                    (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_RMENU) & 0x8000) != 0);
                return 0;
            }
        }
        if (message == WM_KEYDOWN) {
            if (wparam == VK_RETURN) { preview_capture(); return 0; }
            if (handle_gallery_key(wparam)) return 0;
        }
        return CallWindowProcW(g_gallery_list_proc, hwnd, message, wparam, lparam);
    });
}

LRESULT CALLBACK gallery_button_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR id, DWORD_PTR) {
    return guarded_callback([&]() -> LRESULT {
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, gallery_button_proc, id);
        if (message == WM_MOUSEWHEEL && route_gallery_wheel(hwnd, wparam, lparam)) return 0;
        if (message == WM_KEYDOWN && g_state && handle_gallery_key(wparam)) return 0;
        return DefSubclassProc(hwnd, message, wparam, lparam);
    });
}

LRESULT CALLBACK gallery_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
    if (message == WM_MOUSEWHEEL && route_gallery_wheel(hwnd, wparam, lparam)) return 0;
    if (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE && g_state &&
        g_state->settings.auto_close && !g_state->settings.fixed_window && g_state->modal_depth == 1) {
        const HWND next = reinterpret_cast<HWND>(lparam);
        if (!next || GetAncestor(next, GA_ROOTOWNER) != g_state->window) PostMessageW(g_state->window, WM_CLOSE, 0, 0);
    }
    if (message == WM_CREATE) {
        g_state->gallery_model = xfx::GalleryModel{};
        g_state->gallery_split = -1;
        g_state->gallery_list = xfx::create_thumbnail_list(hwnd, kGalleryListId, GetModuleHandleW(nullptr));
        if (!g_state->gallery_list) return -1;
        set_font(g_state->gallery_list);
        g_gallery_list_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_state->gallery_list,
            GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gallery_list_proc)));
        WNDCLASSW canvas{}; canvas.hInstance = GetModuleHandleW(nullptr);
        canvas.lpfnWndProc = gallery_canvas_proc; canvas.lpszClassName = L"XFXGalleryComparison";
        canvas.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); RegisterClassW(&canvas);
        g_state->gallery_canvas = CreateWindowExW(0, canvas.lpszClassName, L"快照比较预览",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 194, 44, 550, 350, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGalleryCanvasId)), canvas.hInstance, nullptr);
        if (!g_state->gallery_canvas) return -1;
        const wchar_t* labels[] = {L"导出", L"删除", L"复制"};
        const int ids[] = {kSaveCaptureId, kDeleteCaptureId, kCopyCaptureId};
        for (int i = 0; i < 3; ++i) {
            HWND button = CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                8 + 80 * i, 8, 72, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])),
                GetModuleHandleW(nullptr), nullptr);
            if (!button) return -1;
            set_font(button);
            SetWindowSubclass(button, gallery_button_proc, 1, 0);
        }
        g_state->gallery_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS | SS_LEFTNOWORDWRAP,
            10, 416, 730, 24, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_state->gallery_status) return -1;
        set_font(g_state->gallery_status);
        layout_gallery(hwnd);
        refresh_gallery();
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize = {300, 240}; return 0;
    }
    if (message == WM_SIZE) { layout_gallery(hwnd); return 0; }
    if (message == WM_LBUTTONDOWN && PtInRect(&g_state->gallery_splitter,
        POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)})) {
        g_state->gallery_dragging_splitter = true; SetCapture(hwnd); return 0;
    }
    if (message == WM_MOUSEMOVE && g_state->gallery_dragging_splitter && GetCapture() == hwnd) {
        g_state->gallery_split = GET_X_LPARAM(lparam) - 8; layout_gallery(hwnd); return 0;
    }
    if (message == WM_LBUTTONUP && g_state->gallery_dragging_splitter) {
        g_state->gallery_dragging_splitter = false; if (GetCapture() == hwnd) ReleaseCapture(); return 0;
    }
    if (message == WM_CAPTURECHANGED) g_state->gallery_dragging_splitter = false;
    if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
        POINT point{}; GetCursorPos(&point); ScreenToClient(hwnd, &point);
        if (PtInRect(&g_state->gallery_splitter, point)) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32644))); return TRUE;
        }
    }
    if (message == WM_KEYDOWN && handle_gallery_key(wparam)) return 0;
    if (message == WM_CONTEXTMENU) {
        UINT selected = 0;
        {
            ModalGuard modal;
            const auto files = selected_captures();
            HMENU menu = CreatePopupMenu();
            const UINT any = files.empty() ? MF_GRAYED : MF_ENABLED;
            const UINT single = files.size() == 1 ? MF_ENABLED : MF_GRAYED;
            AppendMenuW(menu, MF_STRING | any, kSaveCaptureId, L"导出 PNG / JPG");
            AppendMenuW(menu, MF_STRING | any, kDeleteCaptureId, L"删除所选");
            AppendMenuW(menu, MF_STRING | single, kCopyCaptureId, L"复制图片\tCtrl+C");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING | any, kImportCaptureId, L"导回 AE 项目");
            AppendMenuW(menu, MF_STRING | single, kPreviewId, L"单独预览 / 取色");
            AppendMenuW(menu, MF_STRING, kCaptureFolderId, L"打开图片目录");
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (point.x == -1 && point.y == -1) {
                point = {12, 44}; ClientToScreen(hwnd, &point);
            }
            selected = xfx::track_themed_popup(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                point.x, point.y, hwnd, colors(), g_state->font);
            DestroyMenu(menu);
        }
        if (selected && IsWindow(hwnd)) SendMessageW(hwnd, WM_COMMAND, selected, 0);
        return 0;
    }
    if (message == WM_MEASUREITEM) { reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight = 116; return TRUE; }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
        SetDCBrushColor(dc, colors().divider);
        FillRect(dc, &g_state->gallery_splitter, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        EndPaint(hwnd, &paint); return 0;
    }
    if (message == WM_DRAWITEM) {
        const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw->CtlType == ODT_BUTTON) { paint_text_button(*draw); return TRUE; }
        if (draw->CtlType != ODT_LISTBOX) return FALSE;
        if (draw->itemID >= g_state->captures.size()) return TRUE;
        const int saved_dc = SaveDC(draw->hDC);
        SelectObject(draw->hDC, g_state->font);
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        HBRUSH brush = CreateSolidBrush(selected ? colors().selected : colors().background);
        FillRect(draw->hDC, &draw->rcItem, brush); DeleteObject(brush);
        RECT image = draw->rcItem; InflateRect(&image, -6, -6); image.bottom -= 22;
        if (!xfx::draw_cached_thumbnail(draw->hDC, image, g_state->captures[draw->itemID]))
            xfx::draw_capture(draw->hDC, image, g_state->captures[draw->itemID]);
        RECT label = draw->rcItem; label.top = label.bottom - 22; label.left += 6; label.right -= 6;
        SetTextColor(draw->hDC, selected ? colors().selected_text : colors().text); SetBkMode(draw->hDC, TRANSPARENT);
        DrawTextW(draw->hDC, g_state->captures[draw->itemID].filename().c_str(), -1, &label,
            DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        if (saved_dc) RestoreDC(draw->hDC, saved_dc);
        return TRUE;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORBTN)
        return theme_control_color(message, reinterpret_cast<HDC>(wparam),reinterpret_cast<HWND>(lparam));
    if (message == WM_ERASEBKGND) {
        RECT client{}; GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wparam), &client, g_state->background_brush); return TRUE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == kGalleryListId && HIWORD(wparam) == LBN_SELCHANGE) update_gallery_selection();
        if (LOWORD(wparam) == kCopyCaptureId) copy_selected_capture();
        if (LOWORD(wparam) == kDeleteCaptureId) delete_selected_captures();
        if (LOWORD(wparam) == kPreviewId || (LOWORD(wparam) == kGalleryListId && HIWORD(wparam) == LBN_DBLCLK)) preview_capture();
        if (LOWORD(wparam) == kSaveCaptureId) save_capture_as();
        if (LOWORD(wparam) == kCaptureFolderId) ShellExecuteW(hwnd, L"open", capture_directory().c_str(), nullptr, nullptr, SW_SHOW);
        if (LOWORD(wparam) == kImportCaptureId) {
            const auto files = selected_captures();
            size_t imported = 0;
            std::wstring error;
            for (const auto& file : files) {
                const auto reply = run_ae_script(xfx::script_import_image(file.wstring()));
                if (reply.succeeded) ++imported;
                else { error = utf8_to_wide(reply.message.c_str()); break; }
            }
            gallery_status(L"已导入 " + std::to_wstring(imported) + L" 张到 AE 项目" +
                (error.empty() ? L"" : L"；" + error));
        }
        return 0;
    }
    if (message == WM_DESTROY) {
        g_state->gallery_window = nullptr; g_state->gallery_list = nullptr; g_state->gallery_status = nullptr;
        g_state->gallery_canvas = nullptr; g_state->gallery_images.clear();
        g_state->gallery_model = xfx::GalleryModel{};
        g_state->gallery_dragging_splitter = false; g_state->gallery_drag_view = -1;
        if (g_state->modal_depth) --g_state->modal_depth;
        if (g_state->window && !IsWindowVisible(g_state->window) && !g_state->modal_depth)
            PostMessageW(g_state->window, WM_APP + 3, 0, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
    });
}

void show_gallery() {
    if (g_state->gallery_window) { SetForegroundWindow(g_state->gallery_window); refresh_gallery(); return; }
    std::error_code error; fs::create_directories(capture_directory(), error);
    const wchar_t* name = L"XFXCaptureGallery";
    WNDCLASSW klass{}; klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpfnWndProc = gallery_proc; klass.lpszClassName = name;
    klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&klass);
    const unsigned int previous_modal_depth = g_state->modal_depth;
    ++g_state->modal_depth;
    g_state->gallery_window = CreateWindowExW(0, name, L"XFX 快照图库",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 620, g_state->window, nullptr, klass.hInstance, nullptr);
    if (!g_state->gallery_window && g_state->modal_depth > previous_modal_depth) --g_state->modal_depth;
}

void edit_dictionary() {
    ModalGuard modal;
    const auto file = settings_path().parent_path() / L"aliases.user.tsv";
    if (!fs::exists(file)) {
        fs::create_directories(file.parent_path());
        std::ofstream output(file, std::ios::binary);
        output << "\xEF\xBB\xBF# matchName\tEnglishName\tChineseName\tPinyinFull(optional)\tInitials(optional)\tAliases(use |)\n"
            "# UTF-8 TSV. Save, then click Refresh. Tabs separate columns.\n"
            "# Example (remove # to enable):\n"
            "# ADBE Gaussian Blur 2\tGaussian Blur\t高斯模糊\t\t\t柔化|高斯\n";
    }
    ShellExecuteW(g_state->window, L"open", L"notepad.exe", (L"\"" + file.wstring() + L"\"").c_str(), nullptr, SW_SHOW);
    set_status(L"编辑中文译名/别名后保存，再点击刷新索引 · 拼音自动生成");
}

void apply_imported_settings(const xfx::Settings& imported) {
    auto normalized = imported;
    normalized.full_resolution_screenshots = true;
    const auto palette = xfx::resolve_theme(normalized.theme_mode, g_state->host_background);
    ThemeBrushes brushes(palette);
    xfx::SettingsStore::save(settings_path(), normalized);
    g_state->settings = std::move(normalized);
    g_state->palette = palette;
    brushes.install();
    rebuild_customized_catalog();
    select_hotkey_choice(g_state->settings.hotkey);
    SendMessageW(g_state->ime_mode_combo, CB_SETCURSEL,
                 static_cast<int>(g_state->settings.ime_mode), 0);
    SendMessageW(g_state->result_digit_key, CB_SETCURSEL,
                 g_state->settings.result_digit_shortcut
                     ? modifier_combo_index(g_state->settings.result_digit_modifier) : 0, 0);
    refresh_shortcut_choice_labels();
    SendMessageW(g_state->light_theme, CB_SETCURSEL, static_cast<int>(g_state->settings.theme_mode), 0);
    const bool hotkey_ok = register_configured_hotkey();
    populate_results();
    set_status(hotkey_ok ? L"配置已导入" : L"配置已导入；快捷键被占用，请重新设置",hotkey_ok);
}

void import_settings() {
    ModalGuard modal;
    wchar_t file_name[MAX_PATH] = {};
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = g_state->window;
    dialog.lpstrFilter = L"XFX 配置 (*.ini)\0*.ini\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = file_name;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) return;
    apply_imported_settings(xfx::SettingsStore::load(file_name, true));
}

// Writes a self-contained diagnostic bundle the user can send when something goes
// wrong on a machine nobody can inspect: the log, the settings, and host details.
void export_diagnostic_log() {
    ModalGuard modal;
    wchar_t file_name[MAX_PATH] = L"effect-console-diagnostics.txt";
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = g_state->window;
    dialog.lpstrFilter = L"诊断报告 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = file_name;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&dialog)) return;
    try {
        std::ofstream out(fs::path(file_name), std::ios::binary);
        out << "\xEF\xBB\xBF";
        out << "效果控制台 诊断报告\r\n";
        out << "生成时间: ";
        SYSTEMTIME now{}; GetLocalTime(&now);
        char stamp[64]{};
        sprintf_s(stamp, "%04d-%02d-%02d %02d:%02d:%02d", now.wYear, now.wMonth, now.wDay,
                  now.wHour, now.wMinute, now.wSecond);
        out << stamp << "\r\n\r\n";
        out << "== 版本 ==\r\n";
        out << "插件版本: " << kProductVersion << "\r\n";
        out << "诊断日志开关: " << (g_state->settings.diagnostic_logging ? "开启" : "关闭") << "\r\n";
        auto exe = wide_buffer(); GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        out << "宿主程序: " << wide_to_utf8(exe.data()) << "\r\n";
        out << "\r\n== 系统 ==\r\n";
        // RtlGetVersion reports the real build even when the app has no manifest entry;
        // GetVersionEx would report a compatibility-shimmed value.
        std::string build = "unknown";
        if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
            using RtlGetVersionFn = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
            if (auto fn = reinterpret_cast<RtlGetVersionFn>(
                    GetProcAddress(ntdll, "RtlGetVersion"))) {
                RTL_OSVERSIONINFOW version{};
                version.dwOSVersionInfoSize = sizeof(version);
                if (fn(&version) == 0) {
                    build = std::to_string(version.dwMajorVersion) + "." +
                            std::to_string(version.dwMinorVersion) + "." +
                            std::to_string(version.dwBuildNumber);
                }
            }
        }
        out << "Windows: " << build << "\r\n";
        out << "DPI: " << GetDpiForWindow(g_state->window) << "\r\n";
        out << "临时目录: " << wide_to_utf8(settings_path().parent_path().wstring()) << "\r\n";
        out << "\r\n== 设置 ==\r\n";
        out << "快捷键: " << wide_to_utf8(g_state->settings.hotkey) << "\r\n";
        out << "数字键应用结果: " << (g_state->settings.result_digit_shortcut ? "开" : "关") << "\r\n";
        out << "自动切换英文: " << (g_state->settings.use_english_mode ? "开" : "关") << "\r\n";
        out << "主题: " << static_cast<int>(g_state->settings.theme_mode) << "\r\n";
        out << "目录项目数: " << g_state->catalog.size() << "\r\n";
        out << "\r\n== 日志 ==\r\n";
        // Always include the log, even when verbose logging is off, because the error
        // lines are exactly what is needed.
        const std::wstring log = trace_log_path();
        std::ifstream input(fs::path(log), std::ios::binary);
        if (input) {
            std::string line;
            while (std::getline(input, line)) { out << line << "\r\n"; }
        } else {
            out << "（尚无日志记录）\r\n";
        }
        if (!out) throw std::runtime_error("write failed");
    } catch (...) {
        set_status(L"诊断报告导出失败");
        return;
    }
    set_status(L"诊断报告已导出，可发送给开发者", true);
}

void export_settings() {
    ModalGuard modal;
    wchar_t file_name[MAX_PATH] = L"XFXNativeConsole.settings.ini";
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = g_state->window;
    dialog.lpstrFilter = L"XFX 配置 (*.ini)\0*.ini\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = file_name;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&dialog)) return;
    save_settings();
    try {
        xfx::SettingsStore::export_file(settings_path(), file_name);
    } catch (...) {
        set_status(L"配置导出失败");
        return;
    }
    set_status(L"配置已导出",true);
}

bool execute_selected() {
    const LRESULT selected = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        set_status(L"请先选择一个项目");
        return false;
    }
    const LRESULT data = SendMessageW(g_state->results, LB_GETITEMDATA, selected, 0);
    if (data == LB_ERR || data == 0) return false;
    const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
    if (item->item_type == L"empty") {
        set_status(L"该快捷槽为空，请先绑定一个效果或预设");
        return false;
    }
    return execute_item(*item).succeeded;
}

void assign_row_slot_dragged(int slot, int row) {
    if (!g_state || slot < 0 || slot >= 9 || row < 0) return;
    const LRESULT data = SendMessageW(g_state->results, LB_GETITEMDATA, row, 0);
    if (data == LB_ERR || data == 0) { set_status(L"请先选择要绑定的效果"); return; }
    const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
    if (!item || item->item_type == L"empty") { set_status(L"这一行是空位，请先搜索并选中一个效果"); return; }
    auto updated = g_state->settings;
    updated.row_slots[static_cast<size_t>(slot)] = item_key(*item);
    try { xfx::SettingsStore::save(settings_path(), updated); }
    catch (...) { set_status(L"第二行快捷栏绑定未能保存，请检查文档目录写入权限"); return; }
    g_state->settings = std::move(updated);
    const auto label = item_display_label(*item);
    update_trace("drag-bind slot=" + std::to_string(slot) + " row=" + std::to_string(row));
    set_status(L"已把「" + label + L"」绑定到第二行快捷栏 " + std::to_wstring(slot + 1), true);
    // The cell is ownerdraw and reads the settings when it paints. Saving alone changes
    // nothing on screen, which is why a successful drag looked like no bind at all.
    repaint_slot_row_cells();
    if (IsWindow(g_state->window))
        RedrawWindow(g_state->window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

xfx::ScriptReply execute_row_slot(int slot) {
    xfx::ScriptReply failure;
    failure.message = wide_to_utf8(L"第二行快捷栏中的项目已失效，请重新绑定");
    if (slot < 0 || slot >= static_cast<int>(g_state->settings.row_slots.size())) return failure;
    const std::wstring key = g_state->settings.row_slots[static_cast<size_t>(slot)];
    if (key.empty()) {
        set_status(L"第二行快捷栏这一格是空的：把搜索结果拖过来，或右键绑定");
        failure.message = wide_to_utf8(L"第二行快捷栏这一格是空的，请先绑定一个效果或预设");
        return failure;
    }
    for (const auto& item : g_state->catalog) {
        if (item_key(item) == key) return execute_item(item);
    }
    set_status(L"第二行快捷栏中的项目已失效，请重新绑定");
    return failure;
}

void assign_row_slot(int slot) {
    if (!g_state) return;
    // Binding needs the strip on screen; otherwise the user clicks a hidden control and
    // nothing appears to happen.
    if (!g_state->settings.show_slot_row) {
        set_status(L"第二行快捷栏已关闭，请先在设置里打开它再绑定");
        return;
    }
    const LRESULT selected = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) { set_status(L"请先在搜索结果里选中一个效果，再绑定到第二行"); return; }
    const LRESULT data = SendMessageW(g_state->results, LB_GETITEMDATA, selected, 0);
    if (data == LB_ERR || data == 0) { set_status(L"请先选择一个项目"); return; }
    const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
    if (!item || slot < 0 || slot >= 9) return;
    // The placeholder row that fills the shortcut strip is not a real item; binding it
    // would store a key that can never resolve.
    if (item->item_type == L"empty") { set_status(L"这一行是空位，请先搜索并选中一个效果"); return; }
    auto updated = g_state->settings;
    updated.row_slots[static_cast<size_t>(slot)] = item_key(*item);
    try { xfx::SettingsStore::save(settings_path(), updated); }
    catch (...) { set_status(L"第二行快捷栏绑定未能保存，请检查文档目录写入权限"); return; }
    g_state->settings = std::move(updated);
    set_status(L"已绑定到第二行快捷栏 " + std::to_wstring(slot + 1), true);
    InvalidateRect(g_state->window, nullptr, FALSE);
    if (IsWindow(g_state->window))
        RedrawWindow(g_state->window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

xfx::ScriptReply execute_slot(int slot) {
    xfx::ScriptReply failure;
    failure.message = wide_to_utf8(L"快捷槽中的项目已失效，请重新绑定");
    if (slot < 0 || slot >= static_cast<int>(g_state->settings.slots.size())) return failure;
    const std::wstring key = g_state->settings.slots[static_cast<size_t>(slot)];
    if (key.empty()) {
        set_status(L"该快捷槽为空，请使用 Ctrl+Shift+数字 绑定当前项目");
        failure.message = wide_to_utf8(L"该快捷槽为空，请先绑定一个效果或预设"); return failure;
    }
    for (const auto& item : g_state->catalog) {
        if (item_key(item) == key) {
            return execute_item(item);
        }
    }
    set_status(L"快捷槽中的项目已失效，请重新绑定");
    return failure;
}

void assign_slot(int slot) {
    const LRESULT selected = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        set_status(L"请先选择一个项目");
        return;
    }
    const LRESULT data = SendMessageW(g_state->results, LB_GETITEMDATA, selected, 0);
    if (data == LB_ERR || data == 0 || slot < 0 || slot >= 9) return;
    const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
    if (item->item_type == L"empty") { set_status(L"请先搜索并选择要绑定的效果或预设"); return; }
    auto updated = g_state->settings;
    const auto key = item_key(*item);
    if (updated.slots[static_cast<size_t>(slot)] != key) updated.slot_labels[static_cast<size_t>(slot)].clear();
    updated.slots[static_cast<size_t>(slot)] = key;
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    wchar_t message[128] = {};
    swprintf_s(message, L"已将当前项目绑定到快捷槽 Ctrl+%d", slot + 1);
    populate_results();
    set_status(message,true);
}

void assign_next_slot() {
    for (size_t index = 0; index < g_state->settings.slots.size(); ++index) {
        if (g_state->settings.slots[index].empty()) {
            assign_slot(static_cast<int>(index));
            return;
        }
    }
    set_status(L"快捷槽已全部占用，请使用 Ctrl+Shift+数字 覆盖");
}

void toggle_favorite_key(const std::wstring& key) {
    if (key.empty()) return;
    auto updated = g_state->settings;
    auto& favorites = updated.favorites;
    const auto found = std::find(favorites.begin(), favorites.end(), key);
    const bool adding = found == favorites.end();
    if (adding) {
        favorites.push_back(key);
    } else {
        favorites.erase(found);
    }
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    populate_results();
    set_status(adding?L"已加入收藏":L"已取消收藏",true);
}

void toggle_favorite() {
    const LRESULT selected = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        set_status(L"请先选择一个项目");
        return;
    }
    const LRESULT data = SendMessageW(g_state->results, LB_GETITEMDATA, selected, 0);
    if (data == LB_ERR || data == 0) return;
    toggle_favorite_key(item_key(*reinterpret_cast<const xfx::CatalogItem*>(data)));
}

void toggle_pinned(const std::wstring& key) {
    if (key.empty()) return;
    auto updated = g_state->settings;
    auto& pinned = updated.pinned;
    const auto found = std::find(pinned.begin(), pinned.end(), key);
    const bool adding = found == pinned.end();
    if (adding) {
        pinned.push_back(key);
    } else {
        pinned.erase(found);
    }
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    populate_results();
    set_status(adding?L"已置顶排序":L"已取消置顶排序",true);
}

void save_settings() {
    auto updated = g_state->settings;
    // The combo is the authority; keep the legacy flag in sync for older readers.
    const auto ime_choice = SendMessageW(g_state->ime_mode_combo, CB_GETCURSEL, 0, 0);
    if (ime_choice >= 0 && ime_choice <= 3) updated.ime_mode = static_cast<xfx::ImeMode>(ime_choice);
    updated.use_english_mode = updated.ime_mode == xfx::ImeMode::Restore ||
                               updated.ime_mode == xfx::ImeMode::EnglishAlways;
    // The summon shortcut is owned by its dropdown; save_settings must not overwrite it
    // from a control that no longer exists.
    updated.fixed_window = false;
    updated.auto_close = true;
    updated.full_resolution_screenshots = true;
    // Diagnostics are always on: a failure on a machine we cannot inspect must leave
    // evidence behind, so there is no user switch for this.
    updated.diagnostic_logging = true;
    // The two shortcut dropdowns are authoritative; keep the flags in sync so an
    // imported file cannot enable a group the dropdown shows as off.
    const auto slot_choice = SendMessageW(g_state->slot_row_key, CB_GETCURSEL, 0, 0);
    if (slot_choice >= 0) updated.show_slot_row = slot_choice != 0;
    const auto digit_choice = SendMessageW(g_state->result_digit_key, CB_GETCURSEL, 0, 0);
    if (digit_choice >= 0) updated.result_digit_shortcut = digit_choice != 0;
    try { xfx::SettingsStore::save(settings_path(), updated); }
    catch (...) {
        throw;
    }
    g_state->settings = std::move(updated);
}

void restore_suspended_hotkey() {
    if (!g_state || !g_state->hotkey_window) return;
    if (!GetPropW(g_state->hotkey_window, L"XFXHotkeySuspended")) return;
    RemovePropW(g_state->hotkey_window, L"XFXHotkeySuspended");
    register_configured_hotkey();
}

// Shows the stored shortcut in the list. A custom value that is not one of the ready
// choices is added so it stays visible and selectable.
void select_hotkey_choice(const std::wstring& value) {
    if (!g_state || !g_state->hotkey_combo) return;
    if (value.empty()) { SendMessageW(g_state->hotkey_combo, CB_SETCURSEL, -1, 0); return; }
    const LRESULT count = SendMessageW(g_state->hotkey_combo, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index) {
        wchar_t label[128]{};
        if (SendMessageW(g_state->hotkey_combo, CB_GETLBTEXT, index,
                reinterpret_cast<LPARAM>(label)) == CB_ERR) continue;
        if (value == label) { SendMessageW(g_state->hotkey_combo, CB_SETCURSEL, index, 0); return; }
    }
    SendMessageW(g_state->hotkey_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
    const LRESULT added = SendMessageW(g_state->hotkey_combo, CB_GETCOUNT, 0, 0);
    SendMessageW(g_state->hotkey_combo, CB_SETCURSEL, added - 1, 0);
}

// Applies the picked summon shortcut. Returns immediately on success; on failure the
// list is put back on the stored value, so a rejected pick never lingers as the shown
// state - that mismatch used to be read back later and applied anyway.
void apply_hotkey_choice() {
    if (!g_state || !g_state->hotkey_combo) return;
    const LRESULT choice = SendMessageW(g_state->hotkey_combo, CB_GETCURSEL, 0, 0);
    if (choice < 0) return;
    wchar_t picked[128]{};
    if (SendMessageW(g_state->hotkey_combo, CB_GETLBTEXT, choice,
            reinterpret_cast<LPARAM>(picked)) == CB_ERR) return;
    const std::wstring candidate = picked;
    if (candidate == g_state->settings.hotkey) {
        set_status(L"快捷键未改变", true);
        return;
    }
    const std::wstring previous = g_state->settings.hotkey;
    g_state->settings.hotkey = candidate;
    if (!register_configured_hotkey()) {
        g_state->settings.hotkey = previous;
        register_configured_hotkey();
        select_hotkey_choice(previous);
        set_status(L"这个快捷键已被其它程序占用，未生效。请换一个（原快捷键保持不变）");
        return;
    }
    if (g_state->hotkey_window) RemovePropW(g_state->hotkey_window, L"XFXHotkeySuspended");
    try { xfx::SettingsStore::save(settings_path(), g_state->settings); }
    catch (...) {
        g_state->settings.hotkey = previous;
        register_configured_hotkey();
        select_hotkey_choice(previous);
        set_status(L"快捷键未能保存，请检查文档目录写入权限");
        return;
    }
    set_status(L"快捷键已改为 " + candidate, true);
}

// Applies the closing half of the selected input-method mode.
void apply_ime_on_close() {
    switch (g_state->settings.ime_mode) {
        case xfx::ImeMode::Disabled:
            break;
        case xfx::ImeMode::Restore:
            // Undo the switch so the user gets their previous state back.
            g_state->ime.restore();
            break;
        case xfx::ImeMode::ChineseOnOpen:
            // User asked for English on close.
            g_state->ime.commit_english();
            break;
        case xfx::ImeMode::EnglishAlways:
            // English before and after; there is nothing to restore.
            g_state->ime.commit_english();
            break;
    }
    g_state->ime_session_active = false;
}

void save_and_restore() {
    try { save_settings(); } catch (...) { set_status(L"配置保存失败，请检查文档目录的写入权限"); }
    apply_ime_on_close();
}

std::wstring trim_text(std::wstring text) {
    const auto first = std::find_if_not(text.begin(), text.end(), iswspace);
    const auto last = std::find_if_not(text.rbegin(), text.rend(), iswspace).base();
    return first < last ? std::wstring(first, last) : L"";
}

void save_item_aliases(const std::wstring& key, const std::vector<std::wstring>& aliases) {
    auto updated = g_state->settings;
    if (aliases.empty()) updated.custom_aliases.erase(key);
    else updated.custom_aliases[key] = aliases;
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    // Keep an untouched scan so editing or clearing does not retain a previously merged alias.
    rebuild_customized_catalog();
    populate_results();
}

void edit_item_aliases(const xfx::CatalogItem& item) {
    ModalGuard modal;
    const auto key = item_key(item);
    std::wstring text;
    const auto found = g_state->settings.custom_aliases.find(key);
    if (found != g_state->settings.custom_aliases.end()) {
        for (const auto& alias : found->second) { if (!text.empty()) text += L"\r\n"; text += alias; }
    }
    if (!xfx::prompt_text_palette(g_state->window, L"编辑搜索别名",
        item_display_label(item) + L"\n每行一个别名；中文会自动支持全拼和首字母搜索。",
        true, text, g_state->font, colors())) return;
    std::vector<std::wstring> aliases;
    std::wistringstream input(text);
    std::wstring line;
    while (std::getline(input, line)) {
        line = trim_text(line);
        if (!line.empty() && std::find(aliases.begin(), aliases.end(), line) == aliases.end()) aliases.push_back(line);
    }
    save_item_aliases(key, aliases);
    set_status(aliases.empty() ? L"已清除自定义搜索别名" : L"搜索别名已保存，拼音索引已更新",true);
}

std::wstring slot_display_label(int slot) {
    if (slot < 0 || slot >= 9) return {};
    const auto index = static_cast<size_t>(slot);
    if (g_state->settings.slots[index].empty()) return L"空";
    if (!g_state->settings.slot_labels[index].empty()) return g_state->settings.slot_labels[index];
    for (const auto& item : g_state->catalog) {
        if (item_key(item) == g_state->settings.slots[index]) return item_display_label(item);
    }
    return L"项目已失效";
}

void edit_slot_label(int slot) {
    if (slot < 0 || slot >= 9 || g_state->settings.slots[static_cast<size_t>(slot)].empty()) return;
    ModalGuard modal;
    std::wstring label = g_state->settings.slot_labels[static_cast<size_t>(slot)];
    if (!xfx::prompt_text_palette(g_state->window, L"编辑快捷槽备注",
        L"Ctrl+" + std::to_wstring(slot + 1) + L" · " + slot_display_label(slot) + L"\n留空使用效果或预设名称。",
        false, label, g_state->font, colors())) return;
    auto updated = g_state->settings;
    updated.slot_labels[static_cast<size_t>(slot)] = trim_text(label);
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    set_status(L"快捷槽备注已保存",true);
}

// What a second-strip cell shows in a menu (and never blank).
std::wstring slot_row_display_label(int slot) {
    if (!g_state || slot < 0 || slot >= 9) return L"空";
    const auto& key = g_state->settings.row_slots[static_cast<size_t>(slot)];
    if (key.empty()) return L"空";
    for (const auto& item : g_state->catalog)
        if (item_key(item) == key) return item_display_label(item);
    return L"项目已失效";
}

// Whether a search result is selected that could be bound to a strip cell.
bool selected_row_can_bind() {
    if (!g_state) return false;
    const auto row = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    if (row == LB_ERR) return false;
    const auto data = SendMessageW(g_state->results, LB_GETITEMDATA, row, 0);
    if (!data || data == LB_ERR) return false;
    return reinterpret_cast<const xfx::CatalogItem*>(data)->item_type != L"empty";
}

void clear_row_slot(int slot) {
    if (!g_state || slot < 0 || slot >= 9) return;
    auto updated = g_state->settings;
    updated.row_slots[static_cast<size_t>(slot)].clear();
    try { xfx::SettingsStore::save(settings_path(), updated); }
    catch (...) { set_status(L"第二行快捷栏绑定未能保存，请检查文档目录写入权限"); return; }
    g_state->settings = std::move(updated);
    repaint_slot_row_cells();
    if (IsWindow(g_state->window))
        RedrawWindow(g_state->window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    set_status(L"已清空第二行快捷栏 " + std::to_wstring(slot + 1), true);
}

void clear_slot(int slot) {
    if (slot < 0 || slot >= 9) return;
    auto updated = g_state->settings;
    updated.slots[static_cast<size_t>(slot)].clear();
    updated.slot_labels[static_cast<size_t>(slot)].clear();
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    populate_results();
    set_status(L"快捷槽已清空",true);
}

template<typename Mutation> void persist_settings_update(Mutation mutation) {
    auto updated = g_state->settings;
    mutation(updated);
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
}

void set_theme_mode(xfx::ThemeMode mode) {
    try {
        const auto palette = xfx::resolve_theme(mode, g_state->host_background);
        ThemeBrushes brushes(palette);
        persist_settings_update([=](xfx::Settings& settings) {
            settings.theme_mode = mode; settings.light_theme = xfx::is_light_background(palette.background);
        });
        g_state->palette = palette;
        brushes.install();
        SendMessageW(g_state->light_theme, CB_SETCURSEL, static_cast<int>(mode), 0);
        set_status(mode == xfx::ThemeMode::Follow ? L"已跟随 AE 主题" : mode == xfx::ThemeMode::Light ? L"已使用浅色界面" : L"已使用深色界面",true);
    } catch (...) {
        SendMessageW(g_state->light_theme, CB_SETCURSEL, static_cast<int>(g_state->settings.theme_mode), 0);
        throw;
    }
}
void set_light_theme(bool light) { set_theme_mode(light ? xfx::ThemeMode::Light : xfx::ThemeMode::Dark); }

std::wstring menu_text(const std::wstring& value) {
    std::wstring text;
    for (wchar_t c : value) { if (c == L'&') text += L'&'; text += c == L'\t' ? L' ' : c; }
    return text;
}

bool selected_item_can_bind() {
    const auto row=SendMessageW(g_state->results,LB_GETCURSEL,0,0);
    if(row==LB_ERR)return false;
    const auto data=SendMessageW(g_state->results,LB_GETITEMDATA,row,0);
    if(!data || data==LB_ERR)return false;
    return reinterpret_cast<const xfx::CatalogItem*>(data)->item_type!=L"empty";
}

void append_slot_action_row(HMENU menu, int slot, bool binding, xfx::MenuActionRows* rows) {
    const bool occupied=!g_state->settings.slots[static_cast<size_t>(slot)].empty();
    const auto label=L"Ctrl+"+std::to_wstring(slot+1)+L" · "+menu_text(slot_display_label(slot));
    const auto position=static_cast<UINT>(GetMenuItemCount(menu));
    AppendMenuW(menu,MF_STRING,900+slot,label.c_str()); // inert label, never an execute command
    if(rows)rows->push_back({menu,position,{{
        {static_cast<UINT>(10+slot),xfx::MenuActionIcon::Bind,binding,L"绑定当前项目到槽 "+std::to_wstring(slot+1)},
        {static_cast<UINT>(30+slot),xfx::MenuActionIcon::Edit,occupied,L"编辑槽 "+std::to_wstring(slot+1)+L" 备注"},
        {static_cast<UINT>(50+slot),xfx::MenuActionIcon::Clear,occupied,L"清空槽 "+std::to_wstring(slot+1)}
    }}});
}

void append_slot_menu(HMENU menu, bool binding, xfx::MenuActionRows* rows=nullptr) {
    for(int slot=0;slot<9;++slot)append_slot_action_row(menu,slot,binding,rows);
}

void handle_slot_command(UINT command) {
    if(command>=10 && command<=18)assign_slot(command-10);
    if(command>=30 && command<=38)edit_slot_label(command-30);
    if(command>=50 && command<=58)clear_slot(command-50);
}

UINT track_slot_actions(HMENU menu, POINT point, HWND owner, const xfx::MenuActionRows& rows) {
#ifdef XFX_CONSOLE_TEST
    if(g_test_popup_command){const auto command=*g_test_popup_command;g_test_popup_command.reset();return command;}
#endif
    return xfx::track_themed_action_popup(menu,TPM_RETURNCMD|TPM_NONOTIFY,
        point.x,point.y,owner,colors(),g_state->font,rows);
}

void show_slot_menu(int slot, POINT point) {
    if(slot<0 || slot>=9)return;
    ModalGuard modal;
    HMENU menu=CreatePopupMenu();xfx::MenuActionRows rows;
    append_slot_action_row(menu,slot,selected_item_can_bind(),&rows);
    const auto command=track_slot_actions(menu,point,g_state->window,rows);
    DestroyMenu(menu);handle_slot_command(command);
}

// One cell of the second strip. Commands are numbered in their own range so they can
// never collide with the first-strip menu commands handled by handle_slot_command.
void show_slot_row_menu(int slot) {
    if (slot < 0 || slot >= 9) return;
    ModalGuard modal;
    HMENU menu = CreatePopupMenu();
    xfx::MenuActionRows rows;
    const bool occupied = !g_state->settings.row_slots[static_cast<size_t>(slot)].empty();
    const auto label = L"快捷栏 " + std::to_wstring(slot + 1) + L" · " +
        menu_text(slot_row_display_label(slot));
    AppendMenuW(menu, MF_STRING, 901 + slot, label.c_str());  // inert label
    rows.push_back({menu, 0, {{
        {static_cast<UINT>(70 + slot), xfx::MenuActionIcon::Bind, selected_row_can_bind(),
         L"把当前搜索结果绑定到这里"},
        {static_cast<UINT>(80 + slot), xfx::MenuActionIcon::Clear, occupied, L"清空这一格"}
    }}});
    RECT cell{}; GetWindowRect(g_state->slot_row_buttons[static_cast<size_t>(slot)], &cell);
    const auto command = track_slot_actions(menu, {cell.left, cell.bottom}, g_state->window, rows);
    DestroyMenu(menu);
    if (command >= 70 && command <= 78) assign_row_slot(command - 70);
    else if (command >= 80 && command <= 88) clear_row_slot(command - 80);
}

void show_shortcut_menu() {
    ModalGuard modal;
    HMENU menu=CreatePopupMenu();xfx::MenuActionRows rows;
    append_slot_menu(menu,selected_item_can_bind(),&rows);
    RECT rect{};GetWindowRect(g_state->slot,&rect);
    const auto command=track_slot_actions(menu,{rect.left,rect.bottom},g_state->window,rows);
    DestroyMenu(menu);handle_slot_command(command);
}

void show_result_menu(HWND hwnd, LPARAM location) {
    ModalGuard modal;
    POINT point{GET_X_LPARAM(location), GET_Y_LPARAM(location)};
    if (point.x == -1 && point.y == -1) { GetCursorPos(&point); }
    else {
        POINT client = point; ScreenToClient(g_state->results, &client);
        const LRESULT hit = SendMessageW(g_state->results, LB_ITEMFROMPOINT, 0, MAKELPARAM(client.x, client.y));
        if (!HIWORD(hit)) SendMessageW(g_state->results, LB_SETCURSEL, LOWORD(hit), 0);
    }
    const LRESULT selected = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) return;
    if (shortcut_list_mode() && selected >= 0 && selected < 9) {
        show_slot_menu(static_cast<int>(selected), point);
        return;
    }
    const auto data = SendMessageW(g_state->results, LB_GETITEMDATA, selected, 0);
    if (!data || data == LB_ERR) return;
    const auto item = *reinterpret_cast<const xfx::CatalogItem*>(data);
    if (item.item_type == L"empty") { show_shortcut_menu(); return; }
    const auto key = item_key(item);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"收藏 / 取消收藏");
    AppendMenuW(menu, MF_STRING, 8,
        contains_key(g_state->settings.pinned, key) ? L"取消置顶排序" : L"置顶排序");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const bool has_aliases = g_state->settings.custom_aliases.count(key) != 0;
    AppendMenuW(menu, MF_STRING, 6, has_aliases ? L"编辑搜索别名…" : L"添加搜索别名…");
    AppendMenuW(menu, MF_STRING | (has_aliases ? 0 : MF_GRAYED), 7, L"清除搜索别名");
    if (item.item_type == L"effect") {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 4, L"用动画预设覆盖此效果…");
        AppendMenuW(menu, MF_STRING, 5, L"取消预设覆盖");
    }
    HMENU slots = CreatePopupMenu();
    xfx::MenuActionRows slot_actions;
    append_slot_menu(slots,true,&slot_actions);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(slots), L"快捷槽");
    // The second strip is a separate set of nine bindings, so it gets its own submenu
    // rather than being reachable only by dragging.
    HMENU row_slots = CreatePopupMenu();
    for (int slot = 0; slot < 9; ++slot) {
        const auto label = std::to_wstring(slot + 1) + L" · " + menu_text(slot_row_display_label(slot));
        AppendMenuW(row_slots, MF_STRING, static_cast<UINT>(70 + slot), label.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(row_slots), L"第二行快捷栏");
    const UINT command=track_slot_actions(menu,point,hwnd,slot_actions);
    DestroyMenu(menu);
    if (command == 1) toggle_favorite();
    if (command == 5) persist_settings_update([&](xfx::Settings& settings) { settings.overrides.erase(key); });
    if (command == 6) edit_item_aliases(item);
    if (command == 7) { save_item_aliases(key, {}); set_status(L"已清除自定义搜索别名",true); }
    if (command == 8) toggle_pinned(key);
    handle_slot_command(command);
    if (command >= 70 && command <= 78) assign_row_slot(command - 70);
    if (command == 4) {
        auto filename = wide_buffer();
        OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner = hwnd;
        dialog.lpstrFile = filename.data(); dialog.nMaxFile = static_cast<DWORD>(filename.size());
        dialog.lpstrFilter = L"AE 动画预设 (*.ffx)\0*.ffx\0";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
        if (GetOpenFileNameW(&dialog))
            persist_settings_update([&](xfx::Settings& settings) { settings.overrides[key] = filename.data(); });
    }
    if (command) populate_results();
}

void adjust_console_height() {
    if (!g_state || !g_state->search || !g_state->results) return;
    const HWND hwnd = GetParent(g_state->search);
    const DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
    const LRESULT count = SendMessageW(g_state->results, LB_GETCOUNT, 0, 0);
    // The settings page hides the list, so it must not reserve result-row height.
    const int rows = g_state->settings_visible ? 0
        : shortcut_list_mode() ? 1
        : static_cast<int>((std::max)(static_cast<LRESULT>(1),
            (std::min)(count, static_cast<LRESULT>(8))));
    RECT client{}; GetClientRect(hwnd, &client);
    const double scale = GetDpiForWindow(hwnd) / 96.0;
    // Height budget: toolbar, list (or shortcut strip), style row, favourites strip,
    // status line, then whatever the settings page adds. While the settings page is
    // open the list and both strips are hidden, so none of them may reserve space.
    const int list_part = g_state->settings_visible ? 0
        : shortcut_list_mode() ? kShortcutHeight : rows * kResultHeight;
    const int style_part = g_state->settings_visible ? 0 : kLayerStyleRowHeight;
    const int favourites_part = (!g_state->settings_visible && g_state->settings.show_slot_row)
        ? kSlotRowHeight : 0;
    RECT desired{0, 0, client.right, static_cast<LONG>((56 + list_part + style_part +
        favourites_part + 22 + g_state->settings_extent)*scale)};
    AdjustWindowRectEx(&desired, style, FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE));
    const int height = desired.bottom - desired.top;
    RECT current{}; GetWindowRect(hwnd, &current);
    if (current.bottom - current.top == height) return;
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    const int top = (std::max)(monitor.rcWork.top, (std::min)(current.top, monitor.rcWork.bottom - height));
    SetWindowPos(hwnd, nullptr, current.left, top, current.right - current.left, height,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
}

void apply_button_region(HWND button) {
    if (!IsWindow(button)) return;
    RECT rect{};
    GetClientRect(button, &rect);
    const auto old_width = reinterpret_cast<INT_PTR>(GetPropW(button, L"XFXButtonRegionWidth"));
    const auto old_height = reinterpret_cast<INT_PTR>(GetPropW(button, L"XFXButtonRegionHeight"));
    if (old_width == rect.right && old_height == rect.bottom) return;
    const int diameter = ui_px(8);
    HRGN region = CreateRoundRectRgn(0, 0, rect.right + 1, rect.bottom + 1, diameter, diameter);
    if (region) {
        if (SetWindowRgn(button, region, FALSE)) {
            SetPropW(button, L"XFXButtonRegionWidth", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(rect.right)));
            SetPropW(button, L"XFXButtonRegionHeight", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(rect.bottom)));
        } else DeleteObject(region);
    }
}

void fit_combo_face_height(HWND combo, int target_height) {
    if (!IsWindow(combo)) return;
    RECT actual{};
    if (!GetWindowRect(combo, &actual)) return;
    const int current_height = actual.bottom - actual.top;
    if (current_height == target_height) return;
    const LRESULT selection_height = SendMessageW(combo, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
    if (selection_height == CB_ERR) return;
    // Native ComboBox ignores the requested closed height in MoveWindow. Its
    // selection height plus a native frame determines the actual HWND size.
    // Measure that frame instead of assuming six unscaled pixels for all DPI.
    const int desired_selection = (std::max)(1, static_cast<int>(selection_height) + target_height - current_height);
    SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), desired_selection);
}

void layout_controls(HWND hwnd) {
    RECT rect{}; GetClientRect(hwnd, &rect);
    const UINT dpi = GetDpiForWindow(hwnd);
    const double scale = (dpi ? dpi : 96) / 96.0;
    const int width = static_cast<int>(rect.right / scale), height = static_cast<int>(rect.bottom / scale);
    const int margin = 8, inner = width - 16;
    const auto move = [hwnd,scale](HWND child, int x, int y, int w, int h) {
        if (!IsWindow(child)) return;
        const int left = static_cast<int>(x*scale), top = static_cast<int>(y*scale);
        const int width = static_cast<int>((std::max)(1,w)*scale), height = static_cast<int>((std::max)(1,h)*scale);
        RECT current{}; GetWindowRect(child, &current);
        POINT origin{current.left,current.top}; ScreenToClient(hwnd,&origin);
        const RECT desired{left,top,left+width,top+height};
        const RECT actual{origin.x,origin.y,origin.x+current.right-current.left,origin.y+current.bottom-current.top};
        if (EqualRect(&desired,&actual)) return;
        MoveWindow(child,left,top,width,height,FALSE);
        if (child == g_state->light_theme || child == g_state->ime_mode_combo ||
            child == g_state->slot_row_key || child == g_state->result_digit_key)
            fit_combo_face_height(child, height);
        if (!g_state->redraw_transaction) InvalidateRect(child,nullptr,FALSE);
    };
    const auto toolbar_bounds = toolbar_layout(hwnd);
    g_state->search_surface_bounds = toolbar_bounds.search_surface;
    const auto place = [hwnd](HWND child, const RECT& bounds) {
        if (!IsWindow(child)) return;
        RECT current{};
        GetWindowRect(child, &current);
        POINT origin{current.left, current.top};
        ScreenToClient(hwnd, &origin);
        if (origin.x == bounds.left && origin.y == bounds.top &&
            current.right - current.left == bounds.right - bounds.left &&
            current.bottom - current.top == bounds.bottom - bounds.top) return;
        MoveWindow(child, bounds.left, bounds.top, bounds.right - bounds.left,
            bounds.bottom - bounds.top, FALSE);
        if (!g_state->redraw_transaction) InvalidateRect(child, nullptr, FALSE);
    };
    place(g_state->search, toolbar_bounds.search_edit);
    // Keep the native EDIT above the parent-painted rounded surface. AE can
    // restore child z-order during a timeline-panel resize, which otherwise
    // leaves only the surface and makes the top search field look clipped.
    if (IsWindow(g_state->search))
        SetWindowPos(g_state->search, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    place(g_state->filter, toolbar_bounds.filter);
    fit_combo_face_height(g_state->filter, toolbar_bounds.filter.bottom - toolbar_bounds.filter.top);
    // The rounded search surface belongs to the parent, while the EDIT child
    // owns the text. Repaint both after a layout move so the old child bounds
    // cannot leave a stale background over the surface.
    if (IsWindow(g_state->search)) {
        InvalidateRect(hwnd, &toolbar_bounds.search_surface, FALSE);
        InvalidateRect(g_state->search, nullptr, FALSE);
    }
    place(g_state->capture, toolbar_bounds.buttons[0]);
    place(g_state->slot, toolbar_bounds.buttons[1]);
    place(g_state->preferences, toolbar_bounds.buttons[2]);
    const int footer = g_state->settings_visible ? 56
                                                 : height - 22 - g_state->settings_extent;
    const int styles_top = footer - kLayerStyleRowHeight;
    // Optional favourites strip sits directly above the style row.
    const bool favourites_row = g_state->settings.show_slot_row && !g_state->settings_visible;
    const int slot_row_height = favourites_row ? kSlotRowHeight : 0;
    const int slot_row_top = styles_top - slot_row_height;
    if (g_state->settings_visible) {
        // The settings page is its own surface. Leaving the result list visible above it
        // makes the page look like a leftover panel stacked under a search UI.
        ShowWindow(g_state->results, SW_HIDE);
    } else {
        ShowWindow(g_state->results, SW_SHOWNA);
        move(g_state->results, margin, 56, inner,
             (std::max)(kResultHeight, slot_row_top - 56));
    }
    if (favourites_row) {
        for (size_t index = 0; index < g_state->slot_row_buttons.size(); ++index) {
            const int left = margin + static_cast<int>(index) * inner / 9;
            const int right = margin + static_cast<int>(index + 1) * inner / 9;
            move(g_state->slot_row_buttons[index], left, slot_row_top + 2, right - left, kShortcutHeight);
            ShowWindow(g_state->slot_row_buttons[index], SW_SHOWNA);
            apply_button_region(g_state->slot_row_buttons[index]);
        }
    } else {
        for (HWND button : g_state->slot_row_buttons) ShowWindow(button, SW_HIDE);
    }
    // Separate always-visible row below the shortcut/results area. Preserve
    // numeric 1-9 bindings and search results; compact labels have full tips.
    // The strip is hidden while the settings page is open: it belongs to the search
    // panel, and leaving it there makes the settings grid look like it has leftovers.
    const bool show_style_strip = !g_state->settings_visible;
    for (size_t index = 0; index < g_state->layer_style_buttons.size(); ++index) {
        const int left = margin + static_cast<int>(index) * inner / 9;
        const int right = margin + static_cast<int>(index + 1) * inner / 9;
        move(g_state->layer_style_buttons[index], left, styles_top + 4, right - left - 3, 28);
        ShowWindow(g_state->layer_style_buttons[index], show_style_strip ? SW_SHOWNA : SW_HIDE);
        apply_button_region(g_state->layer_style_buttons[index]);
    }
    const bool narrow = width < 440;
    const auto setting = [&](HWND child, int x, int y, int w, int h, int visual_height = 0) {
        move(child,x,footer+y,w,h);
        const bool should_show = g_state->settings_extent >= y+(visual_height ? visual_height : h);
        const bool is_visible = IsWindowVisible(child) != FALSE;
        if (should_show != is_visible || !g_state->settings_animation_active)
            ShowWindow(child, should_show ? SW_SHOWNA : SW_HIDE);
    };
    // Settings page. One label column, one control column, and every row filled
    // edge to edge - the gaps in the previous version were what read as "empty".
    const int row_h = 28;
    const int row_gap = 8;
    const int pad = 12;
    const int gutter = inner - pad * 2;          // usable width
    const int label_w = narrow ? 96 : 112;        // caption column
    const int control_x = pad + label_w + 8;      // control column starts here
    const int control_w = gutter - label_w - 8;   // and spans to the right edge
    const int gap = 8;
    int cursor = 10;
    const auto next_row = [&](int height) {
        const int top = cursor;
        cursor += height + row_gap;
        return top;
    };

    // 1) Library: caption + two buttons + the live count filling the rest of the row.
    int y = next_row(row_h);
    setting(g_state->library_caption, pad, y, label_w, row_h);
    setting(g_state->refresh, control_x, y, 84, row_h);
    setting(g_state->dictionary, control_x + 92, y, 84, row_h);
    setting(g_state->count, control_x + 184, y + 4,
            (std::max)(0, gutter - (control_x - pad) - 184), 20);

    // 2) Global summon shortcut: a full-width list of ready combinations.
    y = next_row(row_h);
    setting(g_state->hotkey_label, pad, y, label_w, row_h);
    setting(g_state->hotkey_combo, control_x, y, control_w, row_h, 24);

    // 3-4) Appearance: two full-width dropdowns, one per row.
    y = next_row(row_h);
    setting(g_state->slot_row_caption, pad, y, label_w, row_h);
    setting(g_state->slot_row_key, control_x, y, control_w, row_h, 24);

    y = next_row(row_h);
    setting(g_state->result_digit_caption, pad, y, label_w, row_h);
    setting(g_state->result_digit_key, control_x, y, control_w, row_h, 24);

    y = next_row(row_h);
    setting(g_state->ime_caption, pad, y, label_w, row_h);
    setting(g_state->ime_mode_combo, control_x, y, control_w, row_h, 24);

    y = next_row(row_h);
    setting(g_state->theme_caption, pad, y, label_w, row_h);
    setting(g_state->light_theme, control_x, y, control_w, row_h, 24);

    // 5) Actions: four equal buttons spanning the full width.
    const int action_w = (std::max)(60, (gutter - gap * 3) / 4);
    y = next_row(row_h);
    setting(g_state->import_settings, pad, y, action_w, row_h);
    setting(g_state->export_settings, pad + action_w + gap, y, action_w, row_h);
    setting(g_state->export_log_button, pad + (action_w + gap) * 2, y, action_w, row_h);
    // The update button belongs to this row only while the settings page is open. On the
    // home screen it lives in the footer and only when an update is waiting; otherwise it
    // must be hidden, or it stays painted where the settings row used to be.
    const bool update_in_footer = g_state->update_available;
    if (g_state->settings_visible) {
        setting(g_state->update_button, pad + (action_w + gap) * 3, y,
                (std::max)(60, gutter - (action_w + gap) * 3), row_h);
        if (IsWindow(g_state->update_button)) {
            SetWindowTextW(g_state->update_button,
                g_state->update_available ? L"立即更新" : L"检查更新");
            EnableWindow(g_state->update_button, TRUE);
        }
    } else if (update_in_footer) {
        move(g_state->update_button, inner - 132, height - 22, 124, 20);
        ShowWindow(g_state->update_button, SW_SHOWNA);
    } else if (IsWindow(g_state->update_button)) {
        ShowWindow(g_state->update_button, SW_HIDE);
    }
    const int update_width = update_in_footer ? 132 : 0;
    const int version_width = ui_px(46);
    move(g_state->status,8,height-20,inner-update_width-version_width,18);
    move(g_state->version_label,inner+8-version_width,height-20,version_width,18);
    for (HWND button : {g_state->capture, g_state->slot, g_state->preferences,
                        g_state->refresh, g_state->dictionary,
                        g_state->import_settings, g_state->export_settings, g_state->update_button,
                        g_state->export_log_button})
        apply_button_region(button);
    SendMessageW(g_state->results, LB_SETITEMHEIGHT, 0, static_cast<LPARAM>((shortcut_list_mode()?kShortcutHeight:kResultHeight)*scale));
    if(shortcut_list_mode()) SendMessageW(g_state->results,LB_SETCOLUMNWIDTH,static_cast<WPARAM>(inner*scale/9),0);
}

void draw_toolbar_icon(HDC dc,const RECT& rect,int id) {
    Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float scale=(rect.right-rect.left)/40.0f;
    g.TranslateTransform((rect.left+rect.right)*.5f,(rect.top+rect.bottom)*.5f);g.ScaleTransform(scale,scale);
    const HWND button=GetDlgItem(g_state->window,id);
    const bool disabled=!IsWindowEnabled(button),selected=id==kPreferencesId && g_state->settings_visible;
    const auto found=g_state->button_hovers.find(button);
    const bool hovered=found!=g_state->button_hovers.end() && found->second.hovered;
    const auto ink=disabled?colors().disabled:selected?colors().selected_text:hovered?colors().text:colors().icon;
    Gdiplus::Pen pen(vector_color(ink),1.5f);
    pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);pen.SetLineJoin(Gdiplus::LineJoinRound);
    // Every symbol fits the same 18px ink box, including its 1.5px stroke.
    if(id==kCaptureId) {
        g.DrawRectangle(&pen,-8.25f,-4.25f,16.5f,11.5f);g.DrawRectangle(&pen,-4.25f,-7.25f,7.5f,3.0f);
        g.DrawEllipse(&pen,-3.0f,-2.0f,6.0f,6.0f);
    } else if(id==kSlotId) {
        for(int y=0;y<3;++y)for(int x=0;x<3;++x)g.DrawRectangle(&pen,-8.25f+x*6,-8.25f+y*6,4.5f,4.5f);
    } else if(id==kPreferencesId) {
        Gdiplus::PointF points[32];
        for(int n=0;n<32;++n){const float angle=(n-.5f)*3.14159265f/16,r=(n%4==1 || n%4==2)?8.25f:6.25f;points[n]={std::cos(angle)*r,std::sin(angle)*r};}
        g.DrawPolygon(&pen,points,32);g.DrawEllipse(&pen,-2.75f,-2.75f,5.5f,5.5f);
    }
}

void create_control(HWND parent, HWND* target, const wchar_t* class_name,
                    const wchar_t* text, DWORD style, int id) {
    *target = CreateWindowExW(0, class_name, text, style, 0, 0, 0, 0, parent,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    set_font(*target);
    if(id==kStatusId && g_state->status_font) SendMessageW(*target,WM_SETFONT,reinterpret_cast<WPARAM>(g_state->status_font),TRUE);
}
struct ComboFaceContent { RECT text{}; float arrow_x=0,arrow_y=0; };
ComboFaceContent combo_face_content(HDC dc,RECT rect,int dpi,const wchar_t* label) {
    const auto px=[dpi](int n){return MulDiv(n,dpi?dpi:96,96);};
    RECT measured{};DrawTextW(dc,label,-1,&measured,DT_SINGLELINE|DT_CALCRECT|DT_NOPREFIX);
    const int gap=px(5),arrow_width=px(8),inset=px(5);
    const int available=(std::max)(0,static_cast<int>(rect.right-rect.left)-2*inset-gap-arrow_width);
    const int text_width=(std::min)(static_cast<int>(measured.right),available);
    const int group_width=text_width+gap+arrow_width;
    const int left=rect.left+(rect.right-rect.left-group_width)/2;
    return {{left,rect.top,left+text_width,rect.bottom},
        static_cast<float>(left+text_width+gap)+arrow_width*.5f,(rect.top+rect.bottom)*.5f};
}

// Sent to a combo after its list opens. Sized from the row height and the item count, so a
// list that Windows sized to nothing (the white 2-pixel bar that was reported) still shows
// its rows. Lowering the maximum only makes a long list scroll; it never hides rows.
constexpr UINT kFixComboPopupHeight = WM_APP + 0x21;
void size_combo_popup(HWND combo);

void draw_combo_face(HWND hwnd, HDC dc) {
    const int saved = SaveDC(dc);
    RECT rect{}; GetClientRect(hwnd, &rect);
    const auto& c = colors();
    // The page background fills the corners so the rounded card's own antialiasing has
    // something to blend into. Without this the arcs leave a dark square edge.
    FillRect(dc, &rect, g_state->background_brush);
    RECT face = rect;
    InflateRect(&face, -ui_px(1), -ui_px(1));
    xfx::paint_round_rect(dc, face, c.input, static_cast<float>(ui_px(5)),
        GetFocus() == hwnd ? c.accent : c.divider);
    SelectObject(dc, g_state->font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, IsWindowEnabled(hwnd) ? c.text : c.disabled);
    const auto selection = SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
    wchar_t label[128]{};
    if (selection >= 0) SendMessageW(hwnd, CB_GETLBTEXT, selection, reinterpret_cast<LPARAM>(label));
    // Both normal painting and ODS_COMBOBOXEDIT use full-client coordinates.
    // Native rcItem is a clipping boundary, never a second text alignment box.
    const int dpi=GetDpiForWindow(hwnd);
    auto content=combo_face_content(dc,rect,dpi,label);
    DrawTextW(dc,label,-1,&content.text,DT_SINGLELINE|DT_LEFT|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    {
        const float scale=(dpi?dpi:96)/96.0f;
        Gdiplus::Graphics graphics(dc); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(vector_color(IsWindowEnabled(hwnd)?c.icon:c.disabled),1.5f*scale);
        const float x=content.arrow_x,y=content.arrow_y;
        graphics.DrawLine(&pen,x-3*scale,y-scale,x,y+2*scale);
        graphics.DrawLine(&pen,x,y+2*scale,x+3*scale,y-scale);
    }
    if (saved) RestoreDC(dc, saved);
}

LRESULT CALLBACK combo_face_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (forward_edge_drag(hwnd, message, wp, lp)) return 0;
    // A closed dropdown must not react to the wheel: scrolling the settings page used to
    // silently change the selected shortcut. The open list keeps its normal scrolling.
    if (message == WM_MOUSEWHEEL && !SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0)) return TRUE;
    if (message == kFixComboPopupHeight) { size_combo_popup(hwnd); return 0; }
    if(message==WM_NCDESTROY) {RemoveWindowSubclass(hwnd,combo_face_proc,id);return DefSubclassProc(hwnd,message,wp,lp);}
    if(message==WM_ERASEBKGND) return TRUE;
    if(message==WM_PAINT || message==WM_PRINTCLIENT) {
        PAINTSTRUCT paint{}; HDC dc = message == WM_PAINT ? BeginPaint(hwnd, &paint) : reinterpret_cast<HDC>(wp);
        draw_combo_face(hwnd, dc);
        if (message == WM_PAINT) EndPaint(hwnd, &paint);
        return 0;
    }
    const auto result = DefSubclassProc(hwnd, message, wp, lp);
    // Native ComboBox draws directly during these messages, including its own arrow and
    // focus frame, so the custom face must be complete before returning. Opening the list
    // is excluded: the closed face does not change, and a synchronous full repaint at that
    // moment is exactly the flash seen when the list is opened and closed quickly.
    const bool opening_popup = message == CB_SHOWDROPDOWN && wp != 0;
    if (opening_popup && IsWindow(hwnd)) PostMessageW(hwnd, kFixComboPopupHeight, 0, 0);
    if (message == kFixComboPopupHeight) { size_combo_popup(hwnd); return 0; }
    const bool native_face_update = !opening_popup &&
        (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == CB_SETCURSEL ||
         message == CB_SHOWDROPDOWN || message == WM_ENABLE || message == WM_LBUTTONDOWN ||
         message == WM_LBUTTONUP || message == WM_KEYDOWN || message == WM_KEYUP);
    if (native_face_update && IsWindow(hwnd) && !g_state->redraw_transaction) {
        // Native ComboBox draws directly during these messages, including its
        // arrow and focus frame. Complete the custom face before returning;
        // queued WM_PAINT would leave a visibly different intermediate face.
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
    }
    return result;
}

constexpr UINT_PTR kComboPopupSubclassId = 0xB008;
void size_combo_popup(HWND combo) {
    if (!combo || !IsWindow(combo)) return;
    if (!SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0)) return;
    COMBOBOXINFO info{sizeof(info)};
    if (!GetComboBoxInfo(combo, &info) || !info.hwndList || !IsWindow(info.hwndList)) return;
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    if (count <= 0) return;
    int row = static_cast<int>(SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0));
    if (row <= 0) row = MulDiv(26, GetDpiForWindow(combo), 96);
    const int visible = (std::min)(count, (std::max)(3, (std::min)(count, 10)));
    const int padding = MulDiv(2, GetDpiForWindow(combo), 96);
    int wanted = row * visible + padding;
    RECT bounds{};
    if (!GetWindowRect(info.hwndList, &bounds)) return;
    const int height = bounds.bottom - bounds.top;
    if (height >= wanted) return;
    // Windows clamps a dropdown to the work area of its monitor. A combo near the bottom
    // of the screen would otherwise open short - indistinguishable from the reported
    // "two pixel" bug. Ask for what fits, and move the list up when the space below is not
    // enough for its rows.
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(combo, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const int available_below = monitor.rcWork.bottom - bounds.top;
        if (wanted > available_below) {
            wanted = (std::max)(row * 3 + padding, available_below);
            const int screen_height = static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top);
            const int list_height = (std::min)(wanted, screen_height);
            const int top = (std::max)(monitor.rcWork.top,
                monitor.rcWork.bottom - list_height);
            SetWindowPos(info.hwndList, nullptr, bounds.left, top, bounds.right - bounds.left,
                list_height, SWP_NOZORDER | SWP_NOACTIVATE);
            update_trace("combo-popup-clamped available=" + std::to_string(available_below) +
                " height=" + std::to_string(list_height));
            return;
        }
    }
    SetWindowPos(info.hwndList, nullptr, 0, 0, bounds.right - bounds.left, wanted,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    update_trace("combo-popup-sized from=" + std::to_string(height) +
        " to=" + std::to_string(wanted));
}


// Clips a dropdown popup to a rounded rectangle. The rows and the closed face are
// already rounded; without this the list window itself still had square corners.
void round_combo_popup(HWND popup) {
    if (!popup || !IsWindow(popup)) return;
    // SetWindowRgn can repaint the popup, which re-enters the resize path that calls this.
    static thread_local bool inside = false;
    if (inside) return;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{inside};
    inside = true;
    RECT client{};
    if (!GetClientRect(popup, &client)) return;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return;
    // A window that already has a region of the right size and shape needs no new one;
    // re-applying it on every resize message is what re-entered here.
    RECT existing{};
    if (GetWindowRgnBox(popup, &existing) != ERROR &&
        existing.right - existing.left == width &&
        existing.bottom - existing.top == height) return;
    const int radius = (std::max)(2, ui_px(6));
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius * 2, radius * 2);
    if (!region) return;
    // SetWindowRgn takes ownership of the region on success.
    if (!SetWindowRgn(popup, region, TRUE)) DeleteObject(region);
}

LRESULT CALLBACK combo_popup_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                                  UINT_PTR id, DWORD_PTR) {
#ifdef XFX_CONSOLE_TEST
    if (message == WM_ERASEBKGND) ++g_test_popup_erase_calls;
#endif

    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, combo_popup_proc, id);
        return DefSubclassProc(hwnd, message, wp, lp);
    }
    // The popup is resized every time it opens; the rounded region follows. Sizing is
    // handled by size_combo_popup, which runs after the list is up.
    if (message == WM_WINDOWPOSCHANGED || message == WM_SIZE) round_combo_popup(hwnd);
    if (message == WM_ERASEBKGND) {
        round_combo_popup(hwnd);

        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(wp), &rect, g_state->input_brush);
        return TRUE;
    }
    if (message == WM_THEMECHANGED || message == WM_SETFONT) InvalidateRect(hwnd, nullptr, FALSE);
    return DefSubclassProc(hwnd, message, wp, lp);
}

// Rebuilds both shortcut dropdown labels. A value that would collide with the other
// group is annotated in place, so the conflict is visible before it is chosen.
void refresh_shortcut_choice_labels() {
    if (!g_state || !g_state->slot_row_key || !g_state->result_digit_key) return;
    const auto fill = [](HWND combo, bool slot_row) {
        if (!combo) return;
        const LRESULT previous = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        const bool other_enabled = slot_row ? g_state->settings.result_digit_shortcut
                                            : g_state->settings.show_slot_row;
        const auto& other_modifier = slot_row ? g_state->settings.result_digit_modifier
                                              : g_state->settings.slot_row_modifier;
        for (size_t index = 0; index < kShortcutChoices.size(); ++index) {
            std::wstring label = kShortcutChoices[index].label;
            const std::wstring modifier = kShortcutChoices[index].modifier;
            // "关闭" never conflicts; the two "no modifier" entries only conflict when the
            // other group also runs without a modifier.
            const bool conflicts = index > 0 && other_enabled && modifier == other_modifier;
            if (conflicts) label += L"（已被另一组使用）";
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        if (previous >= 0) SendMessageW(combo, CB_SETCURSEL, previous, 0);
    };
    fill(g_state->slot_row_key, true);
    fill(g_state->result_digit_key, false);
    InvalidateRect(g_state->slot_row_key, nullptr, FALSE);
    InvalidateRect(g_state->result_digit_key, nullptr, FALSE);
}

void theme_combo_popup(HWND combo) {
    if (!combo) return;
    // Ask the combobox for its own list window. Scanning top-level windows for a
    // "ComboLBox" and matching it by owner/parent never matched: the popup's owner is the
    // combobox's PARENT, not the combobox, so no popup was ever themed or subclassed -
    // which is exactly the all-white dropdown list that was reported.
    COMBOBOXINFO info{sizeof(info)};
    HWND popup = GetComboBoxInfo(combo, &info) ? info.hwndList : nullptr;
    if (!popup) {
        const DWORD process_id = GetCurrentProcessId();
        for (popup = FindWindowExW(nullptr, nullptr, L"ComboLBox", nullptr);
             popup; popup = FindWindowExW(nullptr, popup, L"ComboLBox", nullptr)) {
            DWORD owner_process = 0;
            GetWindowThreadProcessId(popup, &owner_process);
            if (owner_process != process_id) continue;
            if (GetWindow(popup, GW_OWNER) != combo && GetParent(popup) != GetParent(combo) &&
                GetParent(popup) != combo) continue;
            break;
        }
        if (!popup) return;
    }
    {
        // Style the popup exactly once. Re-theming on every open threw WM_THEMECHANGED
        // into a list that was already on screen, which is the flicker seen when the
        // dropdown is opened and closed quickly.
        if (GetPropW(popup, L"XFXComboPopupThemed")) return;
        // The combo popup is a "ComboLBox" window that paints the rows itself; it does not
        // contain a child ListBox. Treating a missing child as "not ready yet" left the
        // window unthemed and unsubclassed forever, which is exactly the blank white list.
        SetWindowTheme(popup, L"", L"");
        SetWindowSubclass(popup, combo_popup_proc, kComboPopupSubclassId, 0);
        set_font(popup);
        const HWND child = FindWindowExW(popup, nullptr, L"ListBox", nullptr);
        if (child) {
            SetWindowTheme(child, L"", L"");
            SetWindowSubclass(child, combo_popup_proc, kComboPopupSubclassId, 0);
            set_font(child);
            InvalidateRect(child, nullptr, FALSE);
        }
        SetPropW(popup, L"XFXComboPopupThemed", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1)));
        round_combo_popup(popup);
        InvalidateRect(popup, nullptr, FALSE);
    }
}

LRESULT CALLBACK checkbox_face_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (forward_edge_drag(hwnd, message, wp, lp)) return 0;
    if(message==WM_NCDESTROY) {RemoveWindowSubclass(hwnd,checkbox_face_proc,id);return DefSubclassProc(hwnd,message,wp,lp);}
    if(message==WM_MOUSEMOVE && !GetPropW(hwnd,L"XFXCheckHover")) {
        SetPropW(hwnd,L"XFXCheckHover",reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};
        TrackMouseEvent(&tracking);
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    if(message==WM_MOUSELEAVE && GetPropW(hwnd,L"XFXCheckHover")) {
        RemovePropW(hwnd,L"XFXCheckHover");
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    if(message==WM_ERASEBKGND) return TRUE;
    if(message==WM_PAINT || message==WM_PRINTCLIENT) {
        PAINTSTRUCT paint{};HDC dc=message==WM_PAINT?BeginPaint(hwnd,&paint):reinterpret_cast<HDC>(wp);
        RECT rect{};GetClientRect(hwnd,&rect);const auto& c=colors();
        SetDCBrushColor(dc,c.background);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const bool enabled=IsWindowEnabled(hwnd)!=FALSE,checked=SendMessageW(hwnd,BM_GETCHECK,0,0)==BST_CHECKED;
        const int size=ui_px(13),top=(rect.bottom-size)/2;
        RECT box{0,top,size,top+size};SetDCBrushColor(dc,checked?c.selected:GetPropW(hwnd,L"XFXCheckHover")?c.button_pressed:c.input);
        FillRect(dc,&box,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetDCBrushColor(dc,GetFocus()==hwnd?c.accent:c.divider);FrameRect(dc,&box,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if(checked) {
            Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::Pen pen(vector_color(enabled?c.selected_text:c.disabled),static_cast<float>(ui_px(2)));
            graphics.DrawLine(&pen,static_cast<float>(ui_px(3)),static_cast<float>(top+ui_px(6)),static_cast<float>(ui_px(6)),static_cast<float>(top+ui_px(9)));
            graphics.DrawLine(&pen,static_cast<float>(ui_px(6)),static_cast<float>(top+ui_px(9)),static_cast<float>(ui_px(11)),static_cast<float>(top+ui_px(3)));
        }
        const auto old=SelectObject(dc,g_state->font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,enabled?c.text:c.disabled);
        wchar_t label[256]{};GetWindowTextW(hwnd,label,256);RECT text=rect;text.left+=ui_px(19);
        DrawTextW(dc,label,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);SelectObject(dc,old);
        if(message==WM_PAINT)EndPaint(hwnd,&paint);return 0;
    }
    const auto result=DefSubclassProc(hwnd,message,wp,lp);
    if(message==WM_SETFOCUS || message==WM_KILLFOCUS || message==WM_ENABLE || message==BM_SETCHECK || message==BM_SETSTATE) InvalidateRect(hwnd,nullptr,FALSE);
    return result;
}
void update_console_fonts(HWND hwnd) {
    const int dpi=GetDpiForWindow(hwnd);
    const auto make=[dpi](int pixels){return CreateFontW(-MulDiv(pixels,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,xfx::kUiFontFamily);};
    HFONT font=make(xfx::kUiBodyPixels),slot_font=make(xfx::kUiSlotNumberPixels),status_font=make(xfx::kUiStatusPixels);
    if(!font || !slot_font || !status_font){if(font)DeleteObject(font);if(slot_font)DeleteObject(slot_font);if(status_font)DeleteObject(status_font);return;}
    const auto old_font=g_state->font,old_small=g_state->slot_number_font,old_status=g_state->status_font;
    g_state->font=font;g_state->slot_number_font=slot_font;g_state->status_font=status_font;
    EnumChildWindows(hwnd,[](HWND child,LPARAM)->BOOL{set_font(child);return TRUE;},0);
    if(IsWindow(g_state->status))SendMessageW(g_state->status,WM_SETFONT,reinterpret_cast<WPARAM>(g_state->status_important?font:status_font),TRUE);
    for(HWND combo:{g_state->filter,g_state->light_theme,g_state->ime_mode_combo,
                     g_state->slot_row_key,g_state->result_digit_key})if(IsWindow(combo)){
        SendMessageW(combo,CB_SETITEMHEIGHT,0,MulDiv(26,dpi,96));
    }
    if(old_font)DeleteObject(old_font);if(old_small)DeleteObject(old_small);if(old_status)DeleteObject(old_status);
}

bool add_tooltip_for_control(HWND parent, HWND tooltip, HWND control, UINT_PTR id, const wchar_t* text) {
    if (!parent || !tooltip || !control) return false;
    TOOLINFOW tool{TTTOOLINFOW_V1_SIZE};
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = parent;
    tool.uId = reinterpret_cast<UINT_PTR>(control);
    tool.hinst = GetModuleHandleW(nullptr);
    g_state->tooltip_texts[id] = text ? std::wstring(text) : std::wstring();
    tool.lpszText = g_state->tooltip_texts[id].data();
    const auto added = SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    return added != FALSE;
}


LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    try {
    switch (message) {
    case WM_SETCURSOR: {
        POINT point{};GetCursorPos(&point);
#ifdef XFX_CONSOLE_TEST
        if(g_test_cursor_position)point=*g_test_cursor_position;
#endif
        ScreenToClient(hwnd,&point);
        const bool in_input=reinterpret_cast<HWND>(wparam)==g_state->search || point_in_search_surface(hwnd,point);
#ifdef XFX_CONSOLE_TEST
        g_test_cursor_point=point;g_test_cursor_in_surface=in_input;++g_test_cursor_calls;
#endif
        if(LOWORD(lparam)==HTCLIENT && in_input){SetCursor(LoadCursorW(nullptr,IDC_IBEAM));return TRUE;}
        break;
    }
    case WM_NCHITTEST: {
        POINT screen_point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        POINT input_point=screen_point;ScreenToClient(hwnd,&input_point);
        if(point_in_search_surface(hwnd,input_point))return HTCLIENT;
        if (point_is_window_edge(hwnd, screen_point)) return HTCAPTION;
        POINT point = screen_point;
        ScreenToClient(hwnd, &point);
        if (point_is_drag_edge(hwnd, point)) return HTCAPTION;
        break;
    }
    case WM_APP + 3:
        // Never tear the palette down while an apply is in flight: the runner hides it on
        // purpose for the host call, so "hidden" does not mean "the user closed it".
        if (!g_state->applying_item && !IsWindowVisible(hwnd) &&
            !g_state->gallery_window && !g_state->modal_depth) {
            save_and_restore(); DestroyWindow(hwnd);
        }
        return 0;
    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wparam) == g_state->results) show_result_menu(hwnd, lparam);
        return 0;
    case WM_LBUTTONDOWN:
        if (!g_state->modal_depth) {
            const auto search = toolbar_layout(hwnd).search_surface;
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (PtInRect(&search, point)) {
                SetFocus(g_state->search);
                POINT edit_point=point;MapWindowPoints(hwnd,g_state->search,&edit_point,1);
                RECT edit{};GetClientRect(g_state->search,&edit);
                edit_point.x=(std::max)(0,(std::min)(static_cast<int>(edit.right)-1,static_cast<int>(edit_point.x)));
                edit_point.y=(edit.top+edit.bottom)/2;
                SendMessageW(g_state->search,WM_LBUTTONDOWN,wparam,MAKELPARAM(edit_point.x,edit_point.y));
                return 0;
            }
        }
        if (!g_state->modal_depth && point_is_drag_edge(hwnd,
            POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)})) {
            g_state->drag_pending = false;
            g_state->drag_active = true;
            SetCapture(hwnd);
            GetCursorPos(&g_state->drag_anchor_cursor);
            RECT window{};
            GetWindowRect(hwnd, &window);
            g_state->drag_anchor_window = {window.left, window.top};
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_state->drag_pending || g_state->drag_active) {
            g_state->drag_pending = false;
            g_state->drag_active = false;
            KillTimer(hwnd, kDragTimerId);
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        g_state->drag_pending = false;
        g_state->drag_active = false;
        KillTimer(hwnd, kDragTimerId);
        break;
    case WM_MOUSEMOVE:
        if (g_state->drag_active && GetCapture() == hwnd) {
            POINT cursor{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ClientToScreen(hwnd, &cursor);
            SetWindowPos(hwnd, nullptr,
                g_state->drag_anchor_window.x + cursor.x - g_state->drag_anchor_cursor.x,
                g_state->drag_anchor_window.y + cursor.y - g_state->drag_anchor_cursor.y,
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == 0xB003) {
            if (g_state->status_expires && GetTickCount64() >= g_state->status_expires) {
                SetWindowTextW(g_state->status,L"就绪"); g_state->status_expires = 0;g_state->status_important=false;
                SendMessageW(g_state->status,WM_SETFONT,reinterpret_cast<WPARAM>(g_state->status_font),TRUE);
            }
            KillTimer(hwnd,0xB003); return 0;
        }
        if (wparam == kSettingsAnimationTimerId) {
            if (!g_state->settings_animation_active) {
                KillTimer(hwnd, kSettingsAnimationTimerId);
                return 0;
            }
            const double t = (std::min)(1.0,(GetTickCount64()-g_state->expand_started)/200.0);
            const double ease = 1-std::pow(1-t,3);
            g_state->settings_extent = static_cast<int>(g_state->expand_from+(g_state->expand_to-g_state->expand_from)*ease);
            layout_controls(hwnd);
            if (t>=1) {
                g_state->settings_extent = g_state->expand_to;
                g_state->settings_animation_active = false;
                if (g_state->expand_to == 0) adjust_console_height();
                layout_controls(hwnd);
                KillTimer(hwnd, kSettingsAnimationTimerId);
            }
            return 0;
        }
        if (wparam == kResultDragTimerId) {
            handle_drag_poll();
            return 0;
        }
        if (wparam == kDragTimerId && g_state->drag_pending) {
            if (drag_button_is_held() && GetCapture() == hwnd) {
                g_state->drag_pending = false;
                g_state->drag_active = true;
                KillTimer(hwnd, kDragTimerId);
                GetCursorPos(&g_state->drag_anchor_cursor);
                RECT window{}; GetWindowRect(hwnd, &window);
                g_state->drag_anchor_window = {window.left, window.top};
            } else {
                g_state->drag_pending = false;
                g_state->drag_active = false;
                KillTimer(hwnd, kDragTimerId);
                if (GetCapture() == hwnd) ReleaseCapture();
            }
            return 0;
        }
        break;
    case WM_CREATE: {
        g_state->window = hwnd;
        g_state->initial_background_primed = false;
        g_state->initial_frame_painted = false;
        g_state->corner_size={};
        update_console_fonts(hwnd);
        create_control(hwnd, &g_state->search, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, kSearchId);
        SendMessageW(g_state->search, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"输入中文、拼音、英文或 matchName"));
        create_control(hwnd, &g_state->filter, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
            CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, kFilterId);
        SetWindowTheme(g_state->filter, L"", L"");
        SendMessageW(g_state->filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部"));
        SendMessageW(g_state->filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"效果"));
        SendMessageW(g_state->filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"预设"));
        SendMessageW(g_state->filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"收藏"));
        SendMessageW(g_state->filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"最近"));
        SendMessageW(g_state->filter, CB_SETCURSEL, 0, 0);

        create_control(hwnd, &g_state->results, L"LISTBOX", L"",
                       WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
                           LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                       kResultsId);
        for (size_t index = 0; index < xfx::layer_styles.size(); ++index) {
            create_control(hwnd, &g_state->layer_style_buttons[index], L"BUTTON", xfx::layer_styles[index].short_label,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, kLayerStyleFirstId + static_cast<int>(index));
        }
        // Second strip: nine user favourites, bound to modifier+1..9. This row is
        // optional because it consumes vertical space the results list wants.
        for (size_t index = 0; index < g_state->slot_row_buttons.size(); ++index) {
            create_control(hwnd, &g_state->slot_row_buttons[index], L"BUTTON", L"",
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, kSlotRowFirstId + static_cast<int>(index));
        }
        create_control(hwnd, &g_state->slot, L"BUTTON", L"快捷项", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kSlotId);
        create_control(hwnd, &g_state->refresh, L"BUTTON", L"刷新索引", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kRefreshId);
        create_control(hwnd, &g_state->capture, L"BUTTON", L"合成快照", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kCaptureId);
        create_control(hwnd, &g_state->dictionary, L"BUTTON", L"编辑译名", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kDictionaryId);
        create_control(hwnd, &g_state->preferences, L"BUTTON", L"设置", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kPreferencesId);
        create_control(hwnd, &g_state->count, L"STATIC", L"正在统计...", WS_CHILD | WS_VISIBLE | SS_RIGHT, kCountId);
        create_control(hwnd, &g_state->hotkey_label, L"STATIC", L"快捷键：",
                       WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, kHotkeyLabelId);
        // The summon shortcut is a list, not a typing box: picking a value always has one
        // code path, so it cannot half-succeed depending on which key the host delivered.
        create_control(hwnd, &g_state->hotkey_combo, L"COMBOBOX", L"唤起快捷键",
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                       CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, kHotkeyComboId);
        if (!g_state->hotkey_combo) return -1;
        for (const auto* label : kHotkeyChoices)
            SendMessageW(g_state->hotkey_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SetWindowTheme(g_state->hotkey_combo, L"", L"");
        create_control(hwnd, &g_state->import_settings, L"BUTTON", L"导入", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kImportId);
        create_control(hwnd, &g_state->export_settings, L"BUTTON", L"导出", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kExportId);
        create_control(hwnd, &g_state->ime_mode_combo, L"COMBOBOX", L"输入法模式",
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                       CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, kImeModeId);
        for (const auto* label : {L"使用时切英文，关闭后恢复（默认）",
                                  L"打开中文，关闭英文",
                                  L"始终英文（开关都用英文）",
                                  L"关闭此功能（不改输入法）"})
            SendMessageW(g_state->ime_mode_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SetWindowTheme(g_state->ime_mode_combo, L"", L"");
        create_control(hwnd, &g_state->light_theme, L"COMBOBOX", L"界面主题",
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, kLightThemeId);
        for (const auto* label : {L"跟随", L"浅色", L"深色"})
            SendMessageW(g_state->light_theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SetWindowTheme(g_state->light_theme, L"", L"");
        create_control(hwnd, &g_state->export_log_button, L"BUTTON", L"导出日志",
                       WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kExportLogId);
        create_control(hwnd, &g_state->slot_row_key, L"COMBOBOX", L"快捷栏按键",
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                       CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, kSlotRowKeyId);
        for (const auto& choice : kShortcutChoices)
            SendMessageW(g_state->slot_row_key, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
        create_control(hwnd, &g_state->result_digit_key, L"COMBOBOX", L"搜索结果按键",
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                       CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, kResultDigitKeyId);
        for (const auto& choice : kShortcutChoices)
            SendMessageW(g_state->result_digit_key, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
        // Both shortcut dropdowns are created just above, so they can only be themed and
        // subclassed here. Doing it earlier silently skipped them and left native light
        // combo boxes in the middle of the dark settings page.
        SetWindowTheme(g_state->slot_row_key, L"", L"");
        SetWindowTheme(g_state->result_digit_key, L"", L"");
        for (HWND combo : {g_state->filter, g_state->light_theme, g_state->ime_mode_combo,
                           g_state->slot_row_key, g_state->result_digit_key,
                           g_state->hotkey_combo}) {
            if (!combo) continue;
            SetWindowSubclass(combo, combo_face_proc, 0xB005, 0);
            SendMessageW(combo, CB_SETITEMHEIGHT, 0, MulDiv(26, GetDpiForWindow(hwnd), 96));
        }
        // Theme every popup now. Waiting for CBN_DROPDOWN meant a list opened without a
        // real click (keyboard, automation, the first open after startup) stayed unthemed
        // and painted its rows white.
        for (HWND combo : {g_state->filter, g_state->light_theme, g_state->ime_mode_combo,
                           g_state->slot_row_key, g_state->result_digit_key,
                           g_state->hotkey_combo}) {
            theme_combo_popup(combo);
        }
        create_control(hwnd, &g_state->result_digit_hint, L"STATIC",
                       L"数字 1-9 固定，只调整修饰键",
                       WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_LEFTNOWORDWRAP, kResultDigitHintId);
        create_control(hwnd, &g_state->slot_row_caption, L"STATIC", L"第二行快捷栏",
                       WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, kSlotRowCaptionId);
        create_control(hwnd, &g_state->result_digit_caption, L"STATIC", L"数字键应用",
                       WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, kResultDigitCaptionId);
        create_control(hwnd, &g_state->library_caption, L"STATIC", L"效果库",
                       WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, kLibraryCaptionId);
        create_control(hwnd, &g_state->ime_caption, L"STATIC", L"输入法",
                       WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, kImeCaptionId);
        create_control(hwnd, &g_state->theme_caption, L"STATIC", L"界面主题",
                       WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, kThemeCaptionId);
        create_control(hwnd, &g_state->slot_row_hint, L"STATIC",
                       L"快捷键与第三行无关",
                       WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_LEFTNOWORDWRAP, kSlotRowHintId);
        create_control(hwnd, &g_state->update_button, L"BUTTON", L"检查更新", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, kUpdateId);
        create_control(hwnd, &g_state->status, L"STATIC", L"正在扫描...",
                       WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTERIMAGE | SS_ENDELLIPSIS, kStatusId);
        // Version is always visible in the bottom-right corner so the user can tell at
        // a glance which build is running without opening any dialog.
        create_control(hwnd, &g_state->version_label, L"STATIC",
                       (std::wstring(L"v") + utf8_to_wide(kProductVersion)).c_str(),
                       WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE | SS_NOPREFIX, kVersionLabelId);
        if (g_state->version_label && g_state->status) {
            SendMessageW(g_state->version_label, WM_SETFONT,
                         reinterpret_cast<WPARAM>(g_state->status_font ? g_state->status_font : g_state->font), TRUE);
        }
        SetWindowSubclass(g_state->status, edge_drag_proc, kEdgeDragSubclassId, 0);
        g_state->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (g_state->tooltip) {
            SetWindowTheme(g_state->tooltip, L"", L"");
            set_font(g_state->tooltip);
            SendMessageW(g_state->tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
            SendMessageW(g_state->tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 600);
            SendMessageW(g_state->tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);
        }
        // Select the stored shortcut, or add it if it is not one of the ready choices so
        // an existing custom value is still displayed and keeps working.
        select_hotkey_choice(g_state->settings.hotkey);
        SendMessageW(g_state->ime_mode_combo, CB_SETCURSEL,
                     static_cast<int>(g_state->settings.ime_mode), 0);
        SendMessageW(g_state->slot_row_key, CB_SETCURSEL,
                     g_state->settings.show_slot_row
                         ? modifier_combo_index(g_state->settings.slot_row_modifier) : 0, 0);
        SendMessageW(g_state->result_digit_key, CB_SETCURSEL,
                     g_state->settings.result_digit_shortcut
                         ? modifier_combo_index(g_state->settings.result_digit_modifier) : 0, 0);
        // Mark combinations the other group already owns before the page is first shown.
        refresh_shortcut_choice_labels();
        SendMessageW(g_state->light_theme, CB_SETCURSEL, static_cast<int>(g_state->settings.theme_mode), 0);
        repaint_theme_windows();
        g_search_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            g_state->search, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(search_edit_proc)));
        g_results_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            g_state->results, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(results_list_proc)));
        if (!register_configured_hotkey()) {
            set_status(L"默认快捷键不可用，请在下方修改");
        }
        layout_controls(hwnd);
        const HWND animated_buttons[] = {g_state->capture, g_state->slot, g_state->preferences,
            g_state->refresh, g_state->dictionary,
            g_state->import_settings, g_state->export_settings, g_state->export_log_button,
            g_state->slot_row_buttons[0], g_state->slot_row_buttons[1], g_state->slot_row_buttons[2],
            g_state->slot_row_buttons[3], g_state->slot_row_buttons[4], g_state->slot_row_buttons[5],
            g_state->slot_row_buttons[6], g_state->slot_row_buttons[7], g_state->slot_row_buttons[8]};
        for (HWND button : animated_buttons) {
            if (SetWindowSubclass(button, button_hover_proc, kButtonHoverSubclassId, 0))
                g_state->button_hovers.emplace(button, ButtonHoverState{});
        }
        for (size_t index = 0; index < g_state->layer_style_buttons.size(); ++index) {
            const HWND button = g_state->layer_style_buttons[index];
            if (SetWindowSubclass(button, button_hover_proc, kButtonHoverSubclassId, 0))
                g_state->button_hovers.emplace(button, ButtonHoverState{});
            const auto& style = xfx::layer_styles[index];
            const std::wstring tip = L"图层样式 · " + std::wstring(style.label) + L" (" +
                utf8_to_wide(style.english_name) + L")：添加/启用到所选图层，保留已有参数；Ctrl+Z 撤销";
            if (g_state->tooltip) add_tooltip_for_control(hwnd, g_state->tooltip, button, 500 + index, tip.c_str());
        }
        // One tip per control, in the same order. These two lists are read positionally,
        // so a single missing entry would silently shift every later tip onto the wrong
        // control - which is exactly what happened when the shortcut capture box was
        // replaced by a dropdown.
        const HWND tools[] = {
            g_state->search, g_state->filter, g_state->capture, g_state->slot, g_state->preferences,
            g_state->refresh, g_state->dictionary, g_state->hotkey_combo,
            g_state->import_settings, g_state->export_settings,
            g_state->ime_mode_combo, g_state->light_theme,
            g_state->slot_row_key, g_state->result_digit_key
        };
        const wchar_t* tips[] = {
            L"搜索效果、预设、中文、拼音、英文或 matchName",
            L"筛选全部、效果、预设、收藏或最近",
            L"保存当前合成快照",
            L"执行或编辑快捷项",
            L"展开或收起设置",
            L"重新扫描效果与预设",
            L"编辑中文译名",
            L"唤起面板的快捷键：从列表里选，选完立即生效",
            L"导入配置",
            L"导出配置",
            L"使用期间切换输入法为英文模式",
            L"界面主题：跟随 AE、浅色或深色；选择会保存",
            L"第二行快捷栏的按键：从列表里选，或直接右键格子绑定",
            L"数字键 1-9 应用搜索结果时的修饰键"
        };
        static_assert(std::size(tools) == std::size(tips), "every control needs exactly one tip");
        for (size_t index = 0; index < std::size(tools) && g_state->tooltip; ++index)
            add_tooltip_for_control(hwnd, g_state->tooltip, tools[index], 100 + index, tips[index]);
        for (size_t index = 0; index < g_state->slot_row_buttons.size(); ++index) {
            wchar_t tip[256] = {};
            swprintf_s(tip, L"第二行快捷栏 %zu：把搜索结果拖到这里，或右键绑定；单击执行",
                index + 1);
            if (g_state->tooltip)
                add_tooltip_for_control(hwnd, g_state->tooltip, g_state->slot_row_buttons[index],
                    600 + index, tip);
        }
        if (g_state->tooltip) {
            g_state->result_tooltip = L"左键应用；拖动到第二行快捷栏可绑定；右键打开更多操作";
            add_tooltip_for_control(hwnd, g_state->tooltip, g_state->results, kResultTooltipId,
                g_state->result_tooltip.c_str());
        }
        if (g_state->catalog_initialized) {
            update_catalog_view();
            set_status(L"就绪");
        } else {
            refresh_catalog();
        }
        return 0;
    }
    case WM_SIZE:
        if (g_state && g_state->search) {
            if(g_state->settings_visible && !g_state->settings_animation_active) {
                RECT client{};GetClientRect(hwnd,&client);
                const int target=client.right*96/GetDpiForWindow(hwnd)<440?330:kSettingsHeight;
                g_state->expand_to=target;
                if(g_state->settings_extent!=target) {
                    g_state->settings_extent=target;adjust_console_height();
                }
            }
            layout_controls(hwnd);
            RECT bounds{};GetWindowRect(hwnd,&bounds);
            const SIZE size{bounds.right-bounds.left,bounds.bottom-bounds.top};
            if(size.cx!=g_state->corner_size.cx || size.cy!=g_state->corner_size.cy) {
                HRGN region=CreateRoundRectRgn(0,0,size.cx+1,size.cy+1,ui_px(16),ui_px(16));
                if(region) {
                    if(SetWindowRgn(hwnd,region,FALSE))g_state->corner_size=size;
                    else DeleteObject(region);
                }
            }
            // Child MoveWindow(...,FALSE) leaves its old parent-owned footprint
            // behind. Queue the whole background; transactions present only
            // after the final resize/layout, not midway through WM_SIZE.
            InvalidateRect(hwnd,nullptr,FALSE);
        }
        return 0;
    case WM_DPICHANGED: {
        update_console_fonts(hwnd);
        const auto* suggested=reinterpret_cast<RECT*>(lparam);
        SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,
            suggested->bottom-suggested->top,SWP_NOACTIVATE|SWP_NOZORDER);
        adjust_console_height();layout_controls(hwnd);return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = MulDiv(360,GetDpiForWindow(hwnd),96);
        info->ptMinTrackSize.y = MulDiv(114+kLayerStyleRowHeight+g_state->settings_extent+
        (g_state->settings.show_slot_row ? kSlotRowHeight : 0),GetDpiForWindow(hwnd),96);
        return 0;
    }
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
#ifdef XFX_CONSOLE_TEST
        ++g_test_measure_calls;
        g_test_last_measure_type = static_cast<int>(measure->CtlType);
        g_test_last_measure_id = static_cast<int>(measure->CtlID);
        g_test_last_measure_height = static_cast<int>(measure->itemHeight);
        if (measure->CtlType < 8) ++g_test_measure_by_type[measure->CtlType];
        g_test_last_measure_id = static_cast<int>(measure->CtlID);
#endif
        if (measure->CtlType == ODT_LISTBOX && measure->CtlID == kResultsId) {
            measure->itemHeight = shortcut_list_mode() ? kShortcutHeight : kResultHeight;
            return TRUE;
        }
        // Answer by control TYPE, not by an id whitelist. The combo popup asks for its
        // row height under a request this window does not own by id; claiming it without
        // filling a height returned 0 and collapsed the whole list to a 2px white sliver.
        if (measure->CtlType == ODT_COMBOBOX) {
            measure->itemHeight = MulDiv(26, GetDpiForWindow(hwnd), 96);
            return TRUE;
        }
        return FALSE;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        struct RestoreDrawDc {HDC dc;int saved;~RestoreDrawDc(){if(saved)RestoreDC(dc,saved);}} restore{draw->hDC,SaveDC(draw->hDC)};
        SelectObject(draw->hDC,g_state->font);
        if (draw->CtlID == kFilterId || draw->CtlID == kLightThemeId ||
            draw->CtlID == kImeModeId || draw->CtlID == kSlotRowKeyId ||
            draw->CtlID == kResultDigitKeyId || draw->CtlID == kHotkeyComboId) {
            if (draw->itemState & ODS_COMBOBOXEDIT) {
                IntersectClipRect(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom);
                draw_combo_face(draw->hwndItem, draw->hDC);
                return TRUE;
            }
            const bool selected = (draw->itemState & ODS_SELECTED) && !(draw->itemState & ODS_COMBOBOXEDIT);
            // Draw the list row as a rounded card, matching every other surface in the
            // console. A flat FillRect left hard square corners in the open list.
            RECT item_card = draw->rcItem;
            InflateRect(&item_card, -ui_px(1), -ui_px(1));
            HBRUSH frame_brush = CreateSolidBrush(colors().input);
            FillRect(draw->hDC, &draw->rcItem, frame_brush); DeleteObject(frame_brush);
            xfx::paint_round_rect(draw->hDC, item_card,
                selected ? colors().selected : colors().input,
                static_cast<float>(ui_px(4)),
                selected ? std::optional<COLORREF>(colors().accent) : std::nullopt);
            if (draw->itemID != static_cast<UINT>(-1)) {
                wchar_t label[64]{}; SendMessageW(draw->hwndItem, CB_GETLBTEXT, draw->itemID, reinterpret_cast<LPARAM>(label));
                RECT text = item_card; text.left += ui_px(7); text.right -= ui_px(4);
                SetBkMode(draw->hDC, TRANSPARENT);
                SetTextColor(draw->hDC, selected ? colors().selected_text : colors().text);
                DrawTextW(draw->hDC, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            return TRUE;
        }
        if (draw->CtlID == kResultsId && draw->itemID != static_cast<UINT>(-1)) {
#ifdef XFX_CONSOLE_TEST
            ++g_test_result_draw_count;
#endif
            const bool selected = (draw->itemState & ODS_SELECTED) != 0;
            HBRUSH brush = CreateSolidBrush(colors().input);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);
            const LRESULT data = SendMessageW(draw->hwndItem, LB_GETITEMDATA, draw->itemID, 0);
            const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
            SetBkMode(draw->hDC, TRANSPARENT);
            if (shortcut_list_mode()) {
                // The shortcut strip always has nine rows, but itemID is a UINT from the
                // host and is not guaranteed to stay inside it.
                const int slot = static_cast<int>(draw->itemID);
                if (slot < 0 || slot >= 9) return TRUE;
                RECT card = draw->rcItem; InflateRect(&card, -2, -2);
                xfx::paint_round_rect(draw->hDC,card,selected?colors().selected:colors().surface,
                    static_cast<float>(ui_px(3)), selected ? std::optional<COLORREF>(colors().accent) : std::nullopt);
                const auto px=[hwnd](int v){return MulDiv(v,GetDpiForWindow(hwnd),96);};
                RECT number = card; number.left += px(3); number.top += px(1); number.bottom = number.top + px(10);
                const auto previous_font = SelectObject(draw->hDC,
                    g_state->slot_number_font ? g_state->slot_number_font : g_state->font);
                SetTextColor(draw->hDC, selected ? colors().selected_text : colors().success);
                DrawTextW(draw->hDC, std::to_wstring(slot + 1).c_str(), -1, &number, DT_LEFT | DT_SINGLELINE);
                SelectObject(draw->hDC, previous_font);
                RECT label = card; label.left += px(3); label.right -= px(3); label.top += px(10);
                auto name = slot_display_label(slot);
                SetTextColor(draw->hDC,selected?colors().selected_text:g_state->settings.slots[static_cast<size_t>(slot)].empty()?colors().quiet:colors().text);
                DrawTextW(draw->hDC, name.c_str(), -1, &label, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                return TRUE;
            }

            if (selected) {
                RECT selection = draw->rcItem;
                InflateRect(&selection, -ui_px(2), -ui_px(2));
                xfx::paint_round_rect(draw->hDC, selection, colors().selected,
                    static_cast<float>(ui_px(3)), colors().accent);
            }

            const auto px=[](int value){return ui_px(value);};
            RECT icon_rect = draw->rcItem;
            icon_rect.left += px(8);
            icon_rect.right = icon_rect.left + px(18);
            icon_rect.top += px(5);
            icon_rect.bottom = icon_rect.top + px(18);
            const bool empty = item && item->item_type == L"empty";
            const bool preset = item && item->item_type == L"preset";
            HBRUSH icon_brush = CreateSolidBrush(empty ? colors().muted :
                (preset ? colors().preset_badge : colors().effect_badge));
            FillRect(draw->hDC, &icon_rect, icon_brush);
            DeleteObject(icon_brush);
            SetTextColor(draw->hDC, colors().selected_text);
            DrawTextW(draw->hDC, empty ? L"" : (preset ? L"P" : L"E"), -1, &icon_rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SetTextColor(draw->hDC, selected ? colors().selected_text : colors().text);
            RECT primary = draw->rcItem;
            const bool shortcut_mode = shortcut_list_mode();
            primary.left += shortcut_mode ? px(28) : px(34);
            primary.right -= shortcut_mode ? px(8) : ui_px(kResultActionWidth) * 3 + 4;
            std::wstring primary_text = L"未命名项目";
            if (item) {
                primary_text = item_display_label(*item);
                // Favorite state is drawn as a vector in the action cell below.
            }
            // Show the number key that applies this row. Only rows 1-9 are reachable,
            // and only while the digit shortcut is on, so the label never promises a
            // key that would do nothing.
            // Number relative to the first visible row so the badge always matches the
            // digit that will apply it, even after the list has been scrolled.
            const int first_visible = static_cast<int>(
                SendMessageW(g_state->results, LB_GETTOPINDEX, 0, 0));
            const int visible_row = static_cast<int>(draw->itemID) -
                (first_visible == LB_ERR ? 0 : first_visible);
            const int digit_badge = (g_state->settings.result_digit_shortcut && !shortcut_mode &&
                visible_row >= 0 && visible_row < 9) ? visible_row + 1 : 0;
            if (digit_badge > 0) {
                RECT key_box = draw->rcItem;
                key_box.left += px(34);
                key_box.right = key_box.left + px(13);
                key_box.top += px(4);
                key_box.bottom -= px(4);
                xfx::paint_round_rect(draw->hDC, key_box,
                    selected ? colors().selected_text : colors().surface,
                    static_cast<float>(ui_px(2)), std::nullopt);
                const auto previous = SelectObject(draw->hDC,
                    g_state->slot_number_font ? g_state->slot_number_font : g_state->font);
                SetTextColor(draw->hDC, selected ? colors().selected : colors().quiet);
                DrawTextW(draw->hDC, std::to_wstring(digit_badge).c_str(), -1, &key_box,
                    DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                SelectObject(draw->hDC, previous);
                SetTextColor(draw->hDC, selected ? colors().selected_text : colors().text);
                primary.left += px(15);
            }
            const int slot = draw->itemID < g_state->visible_slot_numbers.size()
                ? g_state->visible_slot_numbers[draw->itemID] : -1;
            if (slot >= 0 && slot < 9) {
                const auto& label = g_state->settings.slot_labels[static_cast<size_t>(slot)];
                if (!label.empty()) primary_text = label;
                RECT number = draw->rcItem;
                if (shortcut_mode) {
                    number.left += px(4);
                    number.right = number.left + px(20);
                    primary.left = number.right + px(4);
                } else {
                    number.left = number.right - ui_px(kResultActionWidth) * 3 - 30;
                    number.right = draw->rcItem.right - ui_px(kResultActionWidth) * 3 - 4;
                }
                DrawTextW(draw->hDC, std::to_wstring(slot + 1).c_str(), -1, &number,
                    DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
            DrawTextW(draw->hDC, primary_text.c_str(), -1, &primary,
                      DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            const auto draw_action = [&](int index, ResultAction action) {
                RECT action_rect = draw->rcItem;
                action_rect.left = action_rect.right - ui_px(kResultActionWidth) * (3 - index);
                action_rect.right = action_rect.left + ui_px(kResultActionWidth);
                action_rect.top += px(3);
                action_rect.bottom -= px(3);
                const bool hovered = g_state->result_hover_row == static_cast<int>(draw->itemID) &&
                    g_state->result_hover_action == action;
                HBRUSH action_brush = CreateSolidBrush(hovered ? colors().button_pressed :
                    (selected ? colors().selected : colors().input));
                FillRect(draw->hDC, &action_rect, action_brush);
                DeleteObject(action_brush);
                const COLORREF icon_color = selected ? colors().selected_text : colors().muted;
                Gdiplus::Graphics graphics(draw->hDC);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                graphics.TranslateTransform((action_rect.left+action_rect.right)*.5f,(action_rect.top+action_rect.bottom)*.5f);
                const float scale=ui_px(100)/100.0f;graphics.ScaleTransform(scale,scale);
                Gdiplus::Pen pen(vector_color(icon_color),1.5f);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);pen.SetLineJoin(Gdiplus::LineJoinRound);
                if(action==ResultAction::Pin) {
                    graphics.DrawLine(&pen,-8.25f,-8.25f,8.25f,-8.25f);
                    graphics.DrawLine(&pen,0.0f,8.25f,0.0f,-4.0f);
                    graphics.DrawLine(&pen,-7.0f,3.0f,0.0f,-4.0f);graphics.DrawLine(&pen,0.0f,-4.0f,7.0f,3.0f);
                } else if(action==ResultAction::Favorite) {
                    Gdiplus::PointF points[10];
                    for(int n=0;n<10;++n){const float angle=-1.5707963f+n*3.14159265f/5,r=(n%2)?3.5f:8.25f;points[n]={std::cos(angle)*r,std::sin(angle)*r};}
                    if(item && contains_key(g_state->settings.favorites,item_key(*item))){Gdiplus::SolidBrush fill(vector_color(icon_color));graphics.FillPolygon(&fill,points,10);}
                    else graphics.DrawPolygon(&pen,points,10);
                } else {
                    graphics.DrawLine(&pen,-8.25f,0.0f,8.25f,0.0f);graphics.DrawLine(&pen,0.0f,-8.25f,0.0f,8.25f);
                }
            };
            if (!shortcut_mode) {
                draw_action(0, ResultAction::Pin);
                draw_action(1, ResultAction::Favorite);
                draw_action(2, ResultAction::Shortcut);
            }
            HPEN divider = CreatePen(PS_SOLID, 1, selected ? colors().selected : colors().divider);
            const HGDIOBJ previous_pen = SelectObject(draw->hDC, divider);
            MoveToEx(draw->hDC, draw->rcItem.left, draw->rcItem.bottom - 1, nullptr);
            LineTo(draw->hDC, draw->rcItem.right, draw->rcItem.bottom - 1);
            SelectObject(draw->hDC, previous_pen);
            DeleteObject(divider);
            return TRUE;
        }
        if (draw->CtlType == ODT_BUTTON &&
            draw->CtlID >= kSlotRowFirstId && draw->CtlID < kSlotRowFirstId + 9) {
            // Same construction as the first strip (the results list in shortcut mode):
            // identical inset, radius, number font and position, label inset and
            // ellipsis. Only the number caption differs - it carries the bound shortcut.
            const size_t index = static_cast<size_t>(draw->CtlID - kSlotRowFirstId);
            // Same label rules as the first strip: an unbound cell reads 空, never blank.
            std::wstring name = L"空";
            const auto& key = g_state->settings.row_slots[index];
            if (!key.empty()) {
                name = L"项目已失效";
                for (const auto& item : g_state->catalog) {
                    if (item_key(item) == key) { name = item_display_label(item); break; }
                }
            }
            const int width = draw->rcItem.right - draw->rcItem.left;
            const int height = draw->rcItem.bottom - draw->rcItem.top;
            if (width <= 0 || height <= 0) return TRUE;
            const auto hovered_entry = g_state->button_hovers.find(g_state->slot_row_buttons[index]);
            const bool hovered = hovered_entry != g_state->button_hovers.end() &&
                hovered_entry->second.hovered;
            const bool selected = (draw->itemState & ODS_SELECTED) != 0;
            // Highlight the cell under a dragged result so the drop target is obvious.
            const bool drop_target = g_state->result_drag_target == static_cast<int>(index);
            // The first strip is a list item, so it already fills its cell with the input
            // colour before drawing the card. Do the same here or the 2px padding keeps
            // stale pixels and the rows read differently.
            SetDCBrushColor(draw->hDC, colors().input);
            FillRect(draw->hDC, &draw->rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            RECT card = draw->rcItem; InflateRect(&card, -2, -2);
            xfx::paint_round_rect(draw->hDC, card,
                selected ? colors().selected : hovered ? colors().button : colors().surface,
                static_cast<float>(ui_px(3)),
                (selected || drop_target) ? std::optional<COLORREF>(colors().accent) : std::nullopt);
            const auto px = [hwnd](int value) { return MulDiv(value, GetDpiForWindow(hwnd), 96); };
            RECT number = card; number.left += px(3); number.right -= px(3);
            number.top += px(1); number.bottom = number.top + px(10);
            const auto previous_font = SelectObject(draw->hDC,
                g_state->slot_number_font ? g_state->slot_number_font : g_state->font);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, selected ? colors().selected_text : colors().success);
            DrawTextW(draw->hDC, slot_number_text(static_cast<int>(index),
                g_state->settings.slot_row_modifier).c_str(), -1, &number, DT_LEFT | DT_SINGLELINE);
            SelectObject(draw->hDC, previous_font);
            RECT label = card; label.left += px(3); label.right -= px(3); label.top += px(10);
            SetTextColor(draw->hDC, selected ? colors().selected_text
                : name.empty() ? colors().quiet : colors().text);
            DrawTextW(draw->hDC, name.c_str(), -1, &label,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            return TRUE;
        }
        if (draw->CtlType == ODT_BUTTON) {
            const auto paint_button = [](const DRAWITEMSTRUCT& item) {
                const bool icon = item.CtlID == kCaptureId ||
                    item.CtlID == kSlotId || item.CtlID == kPreferencesId;
                if (!icon) { paint_text_button(item); return; }
                paint_button_surface(item);
                draw_toolbar_icon(item.hDC, item.rcItem, item.CtlID);
            };
            const int width = draw->rcItem.right - draw->rcItem.left;
            const int height = draw->rcItem.bottom - draw->rcItem.top;
            if (width <= 0 || height <= 0) return TRUE;
            HDC buffer = CreateCompatibleDC(draw->hDC);
            HBITMAP bitmap = CreateCompatibleBitmap(draw->hDC, width, height);
            if (buffer && bitmap) {
                const auto previous_bitmap = SelectObject(buffer, bitmap);
                const auto previous_font = SelectObject(buffer, g_state->font);
                DRAWITEMSTRUCT frame = *draw;
                frame.hDC = buffer;
                frame.rcItem = {0, 0, width, height};
                paint_button(frame);
                // Present the complete face at once; hover/ripple repainting
                // must not expose the cleared background between draw calls.
                BitBlt(draw->hDC, draw->rcItem.left, draw->rcItem.top, width, height,
                    buffer, 0, 0, SRCCOPY);
                SelectObject(buffer, previous_font);
                SelectObject(buffer, previous_bitmap);
            } else {
                paint_button(*draw);
            }
            if (bitmap) DeleteObject(bitmap);
            if (buffer) DeleteDC(buffer);
            return TRUE;
        }
        break;
    }
    case WM_APP + 5:
        execute_layer_style(static_cast<size_t>(wparam));
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) >= kLayerStyleFirstId && LOWORD(wparam) < kLayerStyleFirstId + 9 &&
            HIWORD(wparam) == BN_CLICKED) {
            // Defer until the button's native notification/paint has unwound.
            PostMessageW(hwnd, WM_APP + 5, LOWORD(wparam) - kLayerStyleFirstId, 0);
            return 0;
        }
        if (LOWORD(wparam) == kCaptureId) {
            // WM_COMMAND is not an AE callback context, even on its UI thread.
            // Query/render only after the host dispatches a fresh IdleHook.
            queue_native_snapshot();
            return 0;
        }
        if (LOWORD(wparam) == kUpdateId) {
            // Update outcomes are informational: report them in the bottom-left status
            // line instead of a modal dialog the user has to dismiss.
            const auto result = perform_update();
            set_status(utf8_to_wide(result.message.c_str()), result.succeeded);
            return 0;
        }
        if (LOWORD(wparam) == kExportLogId && HIWORD(wparam) == BN_CLICKED) {
            export_diagnostic_log();
            return 0;
        }
        if (LOWORD(wparam) == kResultDigitId && HIWORD(wparam) == BN_CLICKED) {
            g_state->settings.result_digit_shortcut =
                SendMessageW(g_state->result_digit_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
            set_status(g_state->settings.result_digit_shortcut
                ? L"已开启：搜索结果中按 1-9 直接应用（小键盘 1-9 不作为快捷应用键）"
                : L"已关闭：数字键 1-9 不再应用搜索结果", true);
            return 0;
        }
        if (LOWORD(wparam) == kDictionaryId) { edit_dictionary(); return 0; }
        if (LOWORD(wparam) == kSlotRowToggleId && HIWORD(wparam) == BN_CLICKED) {
            // The strip is switched from its combo, not a checkbox. A notification from a
            // control that does not exist must not silently clear the setting.
            if (!g_state->slot_row_checkbox || !IsWindow(g_state->slot_row_checkbox)) return 0;
            g_state->settings.show_slot_row =
                SendMessageW(g_state->slot_row_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
            try { xfx::SettingsStore::save(settings_path(), g_state->settings); }
            catch (...) { set_status(L"快捷栏开关未能保存"); return 0; }
            layout_controls(hwnd);
            adjust_console_height();
            set_status(g_state->settings.show_slot_row
                ? L"已显示第二行快捷栏" : L"已隐藏第二行快捷栏", true);
            return 0;
        }
        if ((LOWORD(wparam) == kSlotRowKeyId || LOWORD(wparam) == kResultDigitKeyId) &&
            HIWORD(wparam) == CBN_SELCHANGE) {
            // One dropdown carries both the on/off switch and the modifier, so the page
            // does not need a checkbox plus a combo for the same setting. The selection is
            // read once, then written to both the struct and the control, so a second
            // notification cannot act on a stale value.
            const bool is_slot_row = LOWORD(wparam) == kSlotRowKeyId;
            const HWND combo = is_slot_row ? g_state->slot_row_key : g_state->result_digit_key;
            const auto choice = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
            if (choice < 0 || choice > static_cast<int>(kShortcutChoices.size()) - 1) return 0;

            auto updated = g_state->settings;
            if (is_slot_row) {
                updated.show_slot_row = choice != 0;
                updated.slot_row_modifier = choice ? kShortcutChoices[static_cast<size_t>(choice)].modifier : L"";
            } else {
                updated.result_digit_shortcut = choice != 0;
                updated.result_digit_modifier = choice ? kShortcutChoices[static_cast<size_t>(choice)].modifier : L"";
            }
            // The two groups must never resolve to the same combination, or one of them
            // silently never fires.
            if (shortcut_groups_conflict(updated)) {
                const auto other = is_slot_row ? L"数字键应用" : L"第二行快捷栏";
                set_status(L"这个组合已被「" + std::wstring(other) +
                    L"」占用，请换一个；冲突的组合已在下拉框里标注");
                // Put the control back to what is actually stored. Leaving the rejected
                // value on display let the next save read it back and apply it anyway,
                // which is why the setting appeared to change at random.
                const auto restore = is_slot_row
                    ? (g_state->settings.show_slot_row
                        ? modifier_combo_index(g_state->settings.slot_row_modifier) : 0)
                    : (g_state->settings.result_digit_shortcut
                        ? modifier_combo_index(g_state->settings.result_digit_modifier) : 0);
                SendMessageW(combo, CB_SETCURSEL, restore, 0);
                refresh_shortcut_choice_labels();
                SendMessageW(combo, CB_SETCURSEL, restore, 0);
                InvalidateRect(combo, nullptr, FALSE);
                return 0;
            }
            try { xfx::SettingsStore::save(settings_path(), updated); }
            catch (...) { set_status(L"快捷键设置未能保存，请检查文档目录写入权限"); return 0; }
            g_state->settings = std::move(updated);
            refresh_shortcut_choice_labels();
            layout_controls(hwnd);
            adjust_console_height();
            InvalidateRect(hwnd, nullptr, FALSE);
            if (is_slot_row && !g_state->settings.show_slot_row)
                set_status(L"已关闭第二行快捷栏", true);
            else set_status(L"快捷键设置已保存", true);
            return 0;
        }
        if (LOWORD(wparam) == kImeModeId && HIWORD(wparam) == CBN_SELCHANGE) {
            const auto choice = static_cast<int>(SendMessageW(g_state->ime_mode_combo, CB_GETCURSEL, 0, 0));
            if (choice < 0 || choice > 3) return 0;
            g_state->settings.ime_mode = static_cast<xfx::ImeMode>(choice);
            g_state->settings.use_english_mode =
                g_state->settings.ime_mode == xfx::ImeMode::Restore ||
                g_state->settings.ime_mode == xfx::ImeMode::EnglishAlways;
            // Undo whatever the previous mode did before applying the new one, so
            // switching modes never leaves a half-applied input state behind.
            g_state->ime.restore();
            g_state->ime_session_active = false;
            try { xfx::SettingsStore::save(settings_path(), g_state->settings); }
            catch (...) { set_status(L"输入法模式未能保存，请检查文档目录的写入权限"); return 0; }
            if (g_state->settings.ime_mode == xfx::ImeMode::Disabled) {
                set_status(L"已关闭输入法切换：打开和关闭插件都不改动输入法", true);
                return 0;
            }
            if (!ensure_search_english_mode()) {
                set_status(L"输入法模式已保存，但当前输入法未能切换，将在下次打开时重试");
                return 0;
            }
            switch (g_state->settings.ime_mode) {
                case xfx::ImeMode::Restore:
                    set_status(L"已保存：打开时切英文，关闭后恢复原样", true);
                    break;
                case xfx::ImeMode::ChineseOnOpen:
                    set_status(L"已保存：打开时用中文，关闭后转英文", true);
                    break;
                case xfx::ImeMode::EnglishAlways:
                    set_status(L"已保存：打开和关闭都用英文", true);
                    break;
                case xfx::ImeMode::Disabled:
                    break;
            }
            return 0;
        }
        if (LOWORD(wparam) == kPreferencesId) {
            RECT rect{}; GetWindowRect(hwnd, &rect);
            g_state->settings_visible = !g_state->settings_visible;
            RECT client{}; GetClientRect(hwnd,&client);
            const int target = g_state->settings_visible ? (client.right*96/GetDpiForWindow(hwnd)<440 ? 330 : kSettingsHeight) : 0;
            g_state->expand_from = g_state->settings_extent; g_state->expand_to = target;
            g_state->expand_started = GetTickCount64();
            SetWindowTextW(g_state->preferences, g_state->settings_visible ? L"设置" : L"设置");
            // AE can invalidate the floating native window while child controls
            // are being moved. A timer-driven expand causes visible flashing and
            // transient misalignment in the host, so apply the final layout in
            // one transaction.
            g_state->settings_animation_active = false;
            KillTimer(hwnd, kSettingsAnimationTimerId);
            const bool parent_visible = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE) != 0;
            std::vector<HWND> redraw_children;
            EnumChildWindows(hwnd, [](HWND child, LPARAM state) -> BOOL {
                // DefWindowProc's WM_SETREDRAW(TRUE) adds WS_VISIBLE. Never
                // send it to a child that was hidden before this transaction.
                if (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)
                    reinterpret_cast<std::vector<HWND>*>(state)->push_back(child);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&redraw_children));
            struct RestoreRedraw {
                HWND parent;
                bool parent_visible;
                const std::vector<HWND>& children;
                bool active = true;
                void finish() {
                    if (!active) return;
                    active = false;
                    for (HWND child : children)
                        if (IsWindow(child)) SendMessageW(child, WM_SETREDRAW, TRUE, 0);
                    if (IsWindow(parent)) {
                        // Restoring a visible child also restores WS_VISIBLE;
                        // reapply the responsive visibility before presentation.
                        layout_controls(parent);
                        if (parent_visible) SendMessageW(parent, WM_SETREDRAW, TRUE, 0);
                        if (g_state) g_state->redraw_transaction = false;
                        RedrawWindow(parent, nullptr, nullptr,
                            RDW_INVALIDATE | RDW_NOERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                    }
                    if (g_state) g_state->redraw_transaction = false;
                }
                ~RestoreRedraw() { finish(); }
            } redraw{hwnd, parent_visible, redraw_children};
            g_state->redraw_transaction = true;
            if (parent_visible) SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
            for (HWND child : redraw_children) SendMessageW(child, WM_SETREDRAW, FALSE, 0);
            g_state->settings_extent = target;
            populate_results();
            adjust_console_height();
            layout_controls(hwnd);
            redraw.finish();
            return 0;
        }
        if (LOWORD(wparam) == kSearchId && HIWORD(wparam) == EN_CHANGE) {
            // Keep the palette height and status line stable while typing.
            // Rebuilding an owner-drawn list and resizing the topmost window
            // for every character is the source of the visible AE flicker.
            SearchRedrawGuard redraw(hwnd);
            populate_results(false, false);
            return 0;
        }
        if (LOWORD(wparam) == kFilterId && HIWORD(wparam) == CBN_SELCHANGE) {
            populate_results();
            return 0;
        }
        if ((LOWORD(wparam) == kFilterId || LOWORD(wparam) == kLightThemeId ||
             LOWORD(wparam) == kImeModeId || LOWORD(wparam) == kSlotRowKeyId ||
             LOWORD(wparam) == kResultDigitKeyId) && HIWORD(wparam) == CBN_DROPDOWN) {
            theme_combo_popup(LOWORD(wparam) == kFilterId ? g_state->filter
                            : LOWORD(wparam) == kLightThemeId ? g_state->light_theme
                            : LOWORD(wparam) == kImeModeId ? g_state->ime_mode_combo
                            : LOWORD(wparam) == kSlotRowKeyId ? g_state->slot_row_key
                            : g_state->result_digit_key);
            return 0;
        }
        if (LOWORD(wparam) >= kSlotRowFirstId &&
            LOWORD(wparam) < kSlotRowFirstId + 9 && HIWORD(wparam) == BN_CLICKED) {
            const int index = LOWORD(wparam) - kSlotRowFirstId;
            const bool occupied = !g_state->settings.row_slots[static_cast<size_t>(index)].empty();
            // Clicking an EMPTY cell binds the effect selected in the results list; that is
            // the obvious gesture and previously did nothing but report the slot as empty.
            // Clicking an OCCUPIED cell runs it, and Shift+click always binds (overwrite).
            if (GetKeyState(VK_SHIFT) & 0x8000) assign_row_slot(index);
            else if (!occupied) assign_row_slot(index);
            else {
                const auto reply = execute_row_slot(index);
                if (!reply.pending) set_status(utf8_to_wide(reply.message.c_str()), reply.succeeded);
            }
            return 0;
        }
        if (LOWORD(wparam) == kSlotId && HIWORD(wparam) == BN_CLICKED) {
            show_shortcut_menu();
            return 0;
        }
        if (LOWORD(wparam) == kRefreshId && HIWORD(wparam) == BN_CLICKED) {
            refresh_catalog();
            return 0;
        }
        if (LOWORD(wparam) == kHotkeyComboId && HIWORD(wparam) == CBN_SELCHANGE) {
            apply_hotkey_choice();
            return 0;
        }
        if (LOWORD(wparam) == kHotkeyComboId && HIWORD(wparam) == CBN_DROPDOWN) {
            theme_combo_popup(g_state->hotkey_combo);
            return 0;
        }
        if (LOWORD(wparam) == kImportId && HIWORD(wparam) == BN_CLICKED) {
            import_settings();
            return 0;
        }
        if (LOWORD(wparam) == kExportId && HIWORD(wparam) == BN_CLICKED) {
            export_settings();
            return 0;
        }
        if (LOWORD(wparam) == kLightThemeId && HIWORD(wparam) == CBN_SELCHANGE) {
            const auto choice = SendMessageW(g_state->light_theme, CB_GETCURSEL, 0, 0);
            if (choice >= 0 && choice <= 2) set_theme_mode(static_cast<xfx::ThemeMode>(choice));
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (wparam == kHotkeyId) {
            toggle_console();
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && g_state && xfx::should_close_console(
            g_state->settings.auto_close, g_state->settings.fixed_window, g_state->modal_depth)) {
            const HWND next = reinterpret_cast<HWND>(lparam);
            if (next && (next == hwnd || IsChild(hwnd, next) || GetAncestor(next, GA_ROOTOWNER) == hwnd))
                return 0;
            // Hiding/re-showing or opening a native popup can transiently
            // deactivate the palette. Recheck at dispatch rather than enqueue
            // an unconditional close that destroys the newly focused palette.
            PostMessageW(hwnd, WM_APP + 4, 0, 0);
            return 0;
        }
        break;
    case WM_APP + 4: {
        const HWND foreground = GetForegroundWindow();
        const bool owns_focus = foreground && (foreground == hwnd || IsChild(hwnd, foreground) ||
            GetAncestor(foreground, GA_ROOTOWNER) == hwnd);
        if (!owns_focus && g_state && xfx::should_close_console(
            g_state->settings.auto_close, g_state->settings.fixed_window, g_state->modal_depth)) {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    case WM_CLOSE:
        // An apply hides the palette and pumps messages. A close requested in that window
        // must not destroy it underneath the running host call; the runner decides what
        // happens next, and the close is re-requested afterwards if it was a real one.
        if (g_state && g_state->applying_item) return 0;
        save_and_restore();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd,0xB003); KillTimer(hwnd,kSettingsAnimationTimerId);
        KillTimer(hwnd, kDragTimerId);
        for (const auto& entry : g_state->button_hovers) KillTimer(entry.first, kButtonHoverTimerId);
        g_state->button_hovers.clear();
        g_state->drag_pending = false;
        g_state->drag_active = false;
        g_state->result_click_pending = false;
        g_state->result_click_row = -1;
        g_state->result_click_action = ResultAction::None;
        clear_result_data();
        if (g_state->font) DeleteObject(g_state->font);
        if (g_state->slot_number_font) DeleteObject(g_state->slot_number_font);
        if(g_state->status_font)DeleteObject(g_state->status_font);
        if (g_state->background_brush) DeleteObject(g_state->background_brush);
        if (g_state->input_brush) DeleteObject(g_state->input_brush);
        if(g_state->search_brush)DeleteObject(g_state->search_brush);
        g_state->font = nullptr;
        g_state->slot_number_font = nullptr;
        g_state->status_font=nullptr;
        g_state->background_brush = nullptr;
        g_state->input_brush = nullptr;
        g_state->search_brush=nullptr;
        g_state->hotkey_combo = nullptr;
        g_state->status = nullptr;
        g_state->version_label = nullptr;
        g_state->update_button = nullptr;
        g_state->window = nullptr;
        g_state->ime_source_window = nullptr;
        g_state->tooltip = nullptr;
        g_state->initial_background_primed = false;
        g_state->initial_frame_painted = false;
        return 0;

    case WM_PAINT: {
#ifdef XFX_CONSOLE_TEST
        ++g_test_window_paint_count;
#endif
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
        RECT client{}; GetClientRect(hwnd, &client);
        FillRect(dc, &paint.rcPaint, g_state->background_brush);
        const int dpi = GetDpiForWindow(hwnd);
        const auto px = [dpi](int value){return MulDiv(value,dpi,96);};
        // Panel outline. The window uses WS_POPUP with no nonclient frame, so without
        // this stroke the palette has no edge at all and bleeds into AE's background.
        {
            RECT frame = client;
            frame.right -= 1; frame.bottom -= 1;
            xfx::paint_round_rect(dc, frame, colors().background,
                static_cast<float>(px(8)), colors().divider);
        }
        const RECT search_area = toolbar_layout(hwnd).search_surface;
        xfx::paint_round_rect(dc,search_area,colors().search,static_cast<float>(px(4)),GetFocus()==g_state->search?colors().accent:colors().divider);
        Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.TranslateTransform(static_cast<float>(px(23)),static_cast<float>(px(28)));graphics.ScaleTransform(dpi/96.0f,dpi/96.0f);
        Gdiplus::Pen pen(vector_color(colors().icon),1.5f);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawEllipse(&pen,-7.5f,-7.5f,10.5f,10.5f);graphics.DrawLine(&pen,1.5f,1.5f,7.5f,7.5f);
        EndPaint(hwnd, &paint);
        g_state->initial_frame_painted = true;
        return 0;
    }
    }

    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT ||
        message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORBTN) {
        return theme_control_color(message, reinterpret_cast<HDC>(wparam),reinterpret_cast<HWND>(lparam));
    }
    if (message == WM_ERASEBKGND) {
#ifdef XFX_CONSOLE_TEST
        if (g_state->initial_frame_painted) ++g_test_window_erase_count;
#endif
        if (!g_state->initial_background_primed && !g_state->initial_frame_painted) {
            RECT client{};
            GetClientRect(hwnd, &client);
            FillRect(reinterpret_cast<HDC>(wparam), &client, g_state->background_brush);
            g_state->initial_background_primed = true;
#ifdef XFX_CONSOLE_TEST
            ++g_test_initial_background_fill_count;
            LOGBRUSH brush{};
            if (GetObjectW(g_state->background_brush, sizeof(brush), &brush) == sizeof(brush))
                g_test_initial_background_color = brush.lbColor;
#endif
        }
        return 1;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
    } catch (const std::exception& error) {
        set_status(L"操作失败："+utf8_to_wide(error.what()));
        return message == WM_CREATE ? -1 : 0;
    } catch (...) {
        set_status(L"操作失败，请重新打开控制台");
        return message == WM_CREATE ? -1 : 0;
    }
}

void show_console(bool visible = true) {
    if (!g_state) return;
    const HWND host_input = current_host_input_window();
    if (host_input) g_state->ime_source_window = host_input;
    else if (!IsWindow(g_state->window)) g_state->ime_source_window = nullptr;
    if (g_state->window && IsWindow(g_state->window)) {
        if (!visible) return;
        if (!g_state->ime_session_active)
            g_state->ime_session_active = g_state->ime.capture_original(g_state->search, g_state->ime_source_window);
        ShowWindow(g_state->window, SW_SHOWNORMAL);
        SetWindowPos(g_state->window, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetForegroundWindow(g_state->window);
        SetFocus(g_state->search);
        ensure_search_english_mode();
        RedrawWindow(g_state->window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
        return;
    }

    auto loaded_settings = xfx::SettingsStore::load(settings_path());
    const bool aliases_changed = loaded_settings.custom_aliases != g_state->settings.custom_aliases;
    g_state->settings = std::move(loaded_settings);
    if (g_state->catalog_initialized && aliases_changed) {
        // If rebuilding fails, leave the cache invalid so the next open can
        // recover instead of reusing an engine with stale alias data.
        g_state->catalog_initialized = false;
        rebuild_customized_catalog();
        g_state->catalog_initialized = true;
    }
    g_state->settings_visible = false;
    g_state->settings_extent = 0;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);

    static const wchar_t class_name[] = L"XFXNativeConsoleWindow";
    WNDCLASSEXW klass = {sizeof(WNDCLASSEXW)};
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpfnWndProc = window_proc;
    klass.lpszClassName = class_name;
    klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    // The parent and every custom child paint its own background. A system
    // class brush here would erase the dark/light surface white between
    // resize and WM_PAINT, especially while settings are being expanded.
    klass.hbrBackground = nullptr;
    RegisterClassExW(&klass);

    g_state->font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_state->slot_number_font = CreateFontW(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    refresh_host_theme(true);

    RECT work_area = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    const int window_width = 520;
    const int window_height = 126;
    POINT cursor{}; GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor)) work_area = monitor.rcWork;
    const int window_x = (std::max)(work_area.left, (std::min)(cursor.x - 30, work_area.right - window_width));
    const int window_y = (std::max)(work_area.top, (std::min)(cursor.y - 20, work_area.bottom - window_height));
    // Child controls and owner-drawn surfaces paint their own backgrounds.
    // Avoid WS_EX_COMPOSITED here: it repaints the whole floating palette for
    // every hover/timer invalidation and produces visible flashes in AE.
    const DWORD window_ex_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    // The system nonclient border uses its own light colors. The floating
    // palette has a single themed client surface and a rounded window region.
    const DWORD window_style = WS_POPUP;
    g_state->window = CreateWindowExW(window_ex_style, class_name, L"效果控制台",
                                      window_style | WS_CLIPCHILDREN, window_x, window_y, window_width, window_height,
                                      nullptr, nullptr, klass.hInstance, nullptr);
    if (!g_state->window) {
        g_state->ime.restore();
        g_state->ime_session_active = false;
        show_message_box(nullptr, L"无法创建插件窗口。", L"效果控制台", MB_OK | MB_ICONERROR);
        return;
    }
    // The window is still hidden. Its DPI is now known for the destination
    // monitor; size it in logical units before the first visible frame.
    const int actual_dpi=GetDpiForWindow(g_state->window);
    const int scaled_width=MulDiv(520,actual_dpi,96),scaled_height=MulDiv(126,actual_dpi,96);
    const int scaled_x=(std::max)(work_area.left,(std::min)(cursor.x-MulDiv(30,actual_dpi,96),work_area.right-scaled_width));
    const int scaled_y=(std::max)(work_area.top,(std::min)(cursor.y-MulDiv(20,actual_dpi,96),work_area.bottom-scaled_height));
    SetWindowPos(g_state->window,nullptr,scaled_x,scaled_y,scaled_width,scaled_height,SWP_NOACTIVATE|SWP_NOZORDER);
    adjust_console_height();
    if (visible) {
        g_state->ime_session_active = g_state->ime.capture_original(g_state->search, g_state->ime_source_window);
        HDC initial_dc = GetDC(g_state->window);
        if (initial_dc) {
            SendMessageW(g_state->window, WM_ERASEBKGND, reinterpret_cast<WPARAM>(initial_dc), 0);
            ReleaseDC(g_state->window, initial_dc);
        }
        RedrawWindow(g_state->window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
    }
    if (visible) {
        ShowWindow(g_state->window, SW_SHOWNORMAL);
        SetWindowPos(g_state->window, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetForegroundWindow(g_state->window);
        SetFocus(g_state->search);
        ensure_search_english_mode();
        RedrawWindow(g_state->window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
    }
}

void toggle_console() {
    if (!g_state) return;
    if (g_state->window && IsWindow(g_state->window) && IsWindowVisible(g_state->window)) {
        PostMessageW(g_state->window, WM_CLOSE, 0, 0);
        return;
    }
    show_console();
}

LRESULT CALLBACK hotkey_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
    if (message == WM_APP + 1 || (message == WM_HOTKEY && wparam == kHotkeyId)) {
        if (!g_state || g_state->modal_depth) return 0;
        DWORD foreground_process = 0;
#ifdef XFX_CONSOLE_TEST
        if (g_test_hotkey_foreground_process) foreground_process = *g_test_hotkey_foreground_process;
        else
#endif
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
        if (foreground_process == GetCurrentProcessId()) toggle_console();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
    });
}


xfx::BridgeCompletion bridge_script_result(const xfx::ScriptReply& reply, const std::wstring& success) {
    std::string message = reply.succeeded ? wide_to_utf8(success) : wide_to_utf8(utf8_to_wide(reply.message.c_str()));
    if (reply.partial) message = wide_to_utf8(L"部分完成（" + std::to_wstring(reply.count) + L" 个图层）：") + message;
    return {reply.succeeded, reply.count, reply.partial ? "partial" : "host_error", message};
}

struct SnapshotJob { xfx::BridgeRequest request; fs::path target; };
std::optional<SnapshotJob> g_pending_snapshot;
std::set<std::uint64_t> g_native_snapshot_requests;

void queue_native_snapshot() {
    if (!g_native_snapshot_requests.empty()) { set_status(L"快照正在等待 AE 处理…"); return; }
    const auto response = xfx::script_bridge().submit(xfx::BridgeCommand::Capture);
    constexpr std::string_view prefix = "XFX1\tQUEUED\t";
    if (response.starts_with(prefix)) {
        g_native_snapshot_requests.insert(std::stoull(response.substr(prefix.size())));
        if (IsWindow(g_state->capture)) EnableWindow(g_state->capture, FALSE);
        set_status(L"等待 AE 保存快照…");
    } else {
        ModalGuard modal;
        show_message_box(g_state->window, L"AE 截图服务尚未就绪，请重新打开面板后重试。", L"快照保存失败", MB_OK | MB_ICONERROR);
    }
}
void complete_snapshot_request(const xfx::BridgeRequest& request, const xfx::BridgeCompletion& result) {
    xfx::script_bridge().complete(request.id, result);
    if (g_native_snapshot_requests.erase(request.id)) {
        if (g_state && IsWindow(g_state->capture)) EnableWindow(g_state->capture, TRUE);
        if (result.code != "cancelled") {
            ModalGuard modal;
            const HWND owner = g_state && IsWindow(g_state->window) ? g_state->window : GetForegroundWindow();
            show_message_box(owner, utf8_to_wide(result.message.c_str()).c_str(),
                result.succeeded ? L"快照保存成功" : L"快照保存失败",
                MB_OK | (result.succeeded ? MB_ICONINFORMATION : MB_ICONERROR));
        }
    }
}

xfx::BridgeCompletion prepare_current_snapshot(fs::path& target) {
    ModalGuard modal;
    const auto composition = xfx::current_capture_comp_name(g_basic, g_plugin_id);
    if (!composition.reply.succeeded) {
        set_status(L"快照失败：" + utf8_to_wide(composition.reply.message.c_str()));
        return bridge_script_result(composition.reply, L"");
    }
    auto filename = wide_buffer();
    const auto initial_name = xfx::snapshot_png_filename(composition.name);
    wcscpy_s(filename.data(), filename.size(), initial_name.c_str());
    HWND owner = IsWindowVisible(g_state->window) ? g_state->window : GetForegroundWindow();
    DWORD owner_process = 0;
    GetWindowThreadProcessId(owner, &owner_process);
    if (owner_process != GetCurrentProcessId()) owner = nullptr;
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFile = filename.data(); dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrTitle = L"保存当前合成快照";
    dialog.lpstrFilter = L"PNG 透明图片 (*.png)\0*.png\0";
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) {
        const auto error = CommDlgExtendedError();
        if (error) {
            const auto message = L"无法打开保存对话框（" + std::to_wstring(error) + L"）";
            set_status(message);
            return {false, 0, "save_dialog_error", wide_to_utf8(message)};
        }
        set_status(L"已取消保存快照");
        return {false, 0, "cancelled", wide_to_utf8(L"已取消保存快照")};
    }
    target = fs::path(filename.data());
    if (CompareStringOrdinal(target.extension().c_str(), -1, L".png", -1, TRUE) != CSTR_EQUAL) {
        target += L".png";
        if (fs::exists(target) && show_message_box(owner,
                (L"覆盖已存在的文件？\n" + target.wstring()).c_str(), L"确认替换图片",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            set_status(L"已取消保存快照");
            return {false, 0, "cancelled", wide_to_utf8(L"已取消保存快照")};
        }
    }
    if (!target.is_absolute()) return {false, 0, "save_dialog_error", "保存对话框未返回完整的绝对路径，未执行截图"};
    set_status(L"已选择路径，等待 AE 渲染当前帧…");
    return {true, 0, "snapshot_prepared", "已选择快照路径"};
}

xfx::BridgeCompletion render_current_snapshot(const fs::path& target) {
    ModalGuard modal;
    BusyFeedback feedback(L"正在渲染并保存当前帧…");
    auto rendered = xfx::capture_comp_png(g_basic, g_plugin_id, target, true);
    if (rendered.succeeded) {
        // Do not announce success solely on a renderer return code. Verify the
        // exact user destination, not an old Capture/gallery or temp pathname.
        std::error_code error;
        bool valid = target.is_absolute() && fs::is_regular_file(target, error) && !error &&
            fs::file_size(target, error) > 8 && !error;
        if (valid) {
            std::ifstream stream(target, std::ios::binary);
            char signature[8]{}; stream.read(signature, sizeof(signature));
            valid = stream.gcount() == sizeof(signature) &&
                std::string(signature, sizeof(signature)) == std::string("\x89PNG\r\n\x1a\n", 8);
            if (valid) {
                Gdiplus::Bitmap image(target.c_str());
                valid = image.GetLastStatus() == Gdiplus::Ok && image.GetWidth() && image.GetHeight();
            }
        }
        if (!valid) rendered = {false, false, 0, "目标路径没有生成可读取的 PNG 图片；请检查磁盘、权限及 AE 渲染状态后重试"};
    }
    const auto message = rendered.succeeded ? L"快照已保存：\n" + target.wstring() :
        L"快照失败：" + utf8_to_wide(rendered.message.c_str()) + L"\n目标路径：" + target.wstring();
    set_status(message,rendered.succeeded);
    return {rendered.succeeded, rendered.succeeded ? 1U : 0U,
        rendered.succeeded ? "" : "capture_error", wide_to_utf8(message)};
}

xfx::BridgeCompletion export_current_frame() {
    ModalGuard modal;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"保存当前帧为 PNG…");
    AppendMenuW(menu, MF_STRING, 2, L"保存当前帧为 JPG…");
    AppendMenuW(menu, MF_STRING, 3, L"复制当前帧图片");
    POINT cursor{}; GetCursorPos(&cursor);
    const auto choice = xfx::track_themed_popup(menu, TPM_RETURNCMD | TPM_NONOTIFY,
        cursor.x, cursor.y, g_state->window, colors(), g_state->font);
    DestroyMenu(menu);
    if (!choice) return {false, 0, "cancelled", wide_to_utf8(L"操作已取消")};
    fs::path target;
    bool jpeg = choice == 2;
    if (choice != 3) {
        auto filename = wide_buffer();
        wcscpy_s(filename.data(), filename.size(), L"快照.png");
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = g_state->window; dialog.lpstrFile = filename.data();
        dialog.nMaxFile = static_cast<DWORD>(filename.size());
        dialog.lpstrFilter = L"PNG 透明图片 (*.png)\0*.png\0JPEG 白底图片 (*.jpg)\0*.jpg\0";
        dialog.nFilterIndex = jpeg ? 2 : 1; dialog.lpstrDefExt = jpeg ? L"jpg" : L"png";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&dialog)) return {false, 0, "cancelled", wide_to_utf8(L"操作已取消")};
        jpeg = dialog.nFilterIndex == 2;
        target = filename.data(); target.replace_extension(jpeg ? L".jpg" : L".png");
        if (target != fs::path(filename.data()) && fs::exists(target) &&
            show_message_box(g_state->window, (L"覆盖已存在的文件？\n" + target.wstring()).c_str(),
                L"确认替换图片", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            return {false, 0, "cancelled", wide_to_utf8(L"操作已取消")};
        }
    }
    GUID temporary_id{};
    if (FAILED(CoCreateGuid(&temporary_id)))
        return {false, 0, "export_error", wide_to_utf8(L"无法创建临时导出文件")};
    wchar_t temporary_name[40]{};
    if (!StringFromGUID2(temporary_id, temporary_name, 40))
        return {false, 0, "export_error", wide_to_utf8(L"无法创建临时导出文件")};
    const auto temporary_directory = fs::temp_directory_path() / (L"XFXExport-" + std::wstring(temporary_name));
    if (!fs::create_directory(temporary_directory))
        return {false, 0, "export_error", wide_to_utf8(L"无法创建临时导出目录")};
    const auto source = temporary_directory / L"frame.png";
    struct ExportCleanup {
        fs::path source, directory;
        ~ExportCleanup() {
            std::error_code ignored;
            fs::remove(source, ignored);
            fs::remove(directory, ignored);
        }
    } cleanup{source, temporary_directory};
    BusyFeedback feedback(L"正在渲染并导出当前帧…");
    const auto rendered = xfx::capture_comp_png(g_basic, g_plugin_id, source, true);
    if (!rendered.succeeded) return bridge_script_result(rendered, L"");
    bool success = false;
    if (choice == 3) success = xfx::copy_capture_to_clipboard(g_state->hotkey_window, source);
    else {
        std::error_code error;
        success = (fs::equivalent(source, target, error) && !error) || xfx::export_capture(source, target, jpeg);
    }
    return {success, success ? 1U : 0U, success ? "" : "export_error", wide_to_utf8(success
        ? (choice == 3 ? L"当前帧图片已复制" : L"当前帧图片已保存") : L"图片导出失败，请检查路径或剪贴板后重试")};
}

#include "update_ui.h"

xfx::BridgeCompletion dispatch_bridge_request(const xfx::BridgeRequest& request) {
    using Command = xfx::BridgeCommand;
    // Capture uses the two-stage Idle pipeline below, never a synchronous dispatch.
    if (request.command == Command::Capture) return {false, 0, "capture_context", "截图必须通过 AE 空闲回调排队执行"};
    if (request.command == Command::Update) return perform_update();
    if (request.command == Command::CheckUpdate) return perform_update(true);
    if (request.command == Command::Show) {
        show_console();
        const bool opened = IsWindow(g_state->window) != FALSE;
        return {opened, 0, "window_error", wide_to_utf8(opened ? L"搜索控制台已打开" : L"无法打开搜索控制台")};
    }
    if (request.command == Command::Gallery) {
        return {false, 0, "feature_removed", wide_to_utf8(L"快照图库已移除，请使用保存当前合成快照")};
    }
    if (request.command == Command::Preferences) {
        show_console();
        if (g_state->window && !g_state->settings_visible) SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
        const bool opened = IsWindow(g_state->window) && g_state->settings_visible;
        return {opened, 0, "window_error", wide_to_utf8(opened ? L"设置已打开" : L"无法打开设置")};
    }
    show_console(false);
    if (!g_state->window) return {false, 0, "window_error", wide_to_utf8(L"无法创建插件窗口")};
    if (request.command == Command::Export) return export_current_frame();
    if (request.command == Command::Shortcut) return bridge_script_result(execute_slot(request.slot), L"快捷项已应用");
    if (request.command == Command::About) {
        ModalGuard modal;
        const auto about = L"效果控制台 " + utf8_to_wide(kProductVersion) +
            L"\n中文、全拼与首字母搜索\n\n更新源：GitHub 正式 Releases\n当前为功能对齐测试版，原版完整对照仍在进行。\n\n立即检查更新？";
        if (show_message_box(g_state->window, about.c_str(), L"关于 效果控制台",
            MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2) == IDYES) return perform_update(true);
        return {true, 0, "", wide_to_utf8(L"关于窗口已关闭")};
    }
    return {false, 0, "unknown_command", wide_to_utf8(L"不支持此操作")};
}


bool g_bridge_processing = false;
A_Err bridge_idle_hook(AEGP_GlobalRefcon, AEGP_IdleRefcon, A_long* max_sleep) {
    std::optional<xfx::BridgeRequest> request;
    try {
        if (!g_state || g_bridge_processing) return A_Err_NONE;
        struct ProcessingGuard {
            ProcessingGuard() { g_bridge_processing = true; }
            ~ProcessingGuard() { g_bridge_processing = false; }
        } processing;
        refresh_host_theme();
        ensure_hotkey_window();
        if (!g_first_idle_tick) g_first_idle_tick = GetTickCount64();
        // Staged applies run here, on the AE main thread at a point where no modal
        // loop can be active. This is the single place host work is performed.
        if (run_pending_apply_on_idle()) {
            if (max_sleep && *max_sleep > 6) *max_sleep = 6;
            return A_Err_NONE;
        }
        tick_updates();
        if (max_sleep && *max_sleep > 6) *max_sleep = 6;
        if (g_pending_snapshot) {
            // Re-enter via a NEW host callback after the OS dialog has returned.
            // Its modal message loop must not be the render call's context.
            auto job = std::move(*g_pending_snapshot); g_pending_snapshot.reset();
            request = job.request;
            complete_snapshot_request(*request, render_current_snapshot(job.target));
        } else {
            request = xfx::script_bridge().take_request();
            if (!request) {
                // Expiry/cancellation may complete a queued native request
                // without returning it to this hook. Do not strand its button.
                const auto native = g_native_snapshot_requests;
                for (const auto id : native) {
                    const auto fields = split_tsv(xfx::script_bridge().status(id));
                    if (fields.size() >= 5 && fields[1] == "ERROR")
                        complete_snapshot_request({id, xfx::BridgeCommand::Capture, -1},
                            {false, 0, fields[3], fields[4]});
                }
                return A_Err_NONE;
            }
            if (request->command == xfx::BridgeCommand::Capture) {
                fs::path target;
                const auto prepared = prepare_current_snapshot(target);
                if (prepared.succeeded) {
                    g_pending_snapshot = SnapshotJob{*request, std::move(target)};
                    return A_Err_NONE; // Never render in the dialog callback.
                }
                complete_snapshot_request(*request, prepared);
            } else {
                auto result = dispatch_bridge_request(*request);
                xfx::script_bridge().complete(request->id, std::move(result));
            }
        }
        if (g_state && g_state->window && !IsWindowVisible(g_state->window) &&
            !g_state->gallery_window && !g_state->modal_depth) PostMessageW(g_state->window, WM_APP + 3, 0, 0);
    } catch (...) {
        if (request) {
            try {
                const xfx::BridgeCompletion failure{false, 0, "host_error", wide_to_utf8(L"操作失败，请检查文件权限或重新打开控制台")};
                if (request->command == xfx::BridgeCommand::Capture) complete_snapshot_request(*request, failure);
                else xfx::script_bridge().complete(request->id, failure);
            } catch (...) {}
        }
    }
    return A_Err_NONE;
}

A_Err command_hook(AEGP_GlobalRefcon, AEGP_CommandRefcon, AEGP_Command command,
                   AEGP_HookPriority, A_Boolean, A_Boolean* handled) {
    if (command == g_command) {
        ensure_hotkey_window();
        show_console();
        if (handled) *handled = TRUE;
    }
    return A_Err_NONE;
}

A_Err update_menu_hook(AEGP_GlobalRefcon, AEGP_UpdateMenuRefcon, AEGP_WindowType) {
    try {
        AEGP_SuiteHandler suites(g_basic);
        suites.CommandSuite1()->AEGP_EnableCommand(g_command);
    } catch (...) {
    }
    return A_Err_NONE;
}

A_Err death_hook(AEGP_GlobalRefcon, AEGP_DeathRefcon refcon) {
    try { xfx::script_bridge().set_available(false); } catch (...) {}
    g_pending_snapshot.reset(); g_native_snapshot_requests.clear();
#ifndef XFX_CONSOLE_TEST
    // AE is shutting down and has already released this plugin. Start the silent
    // installer now so the user never has to click anything.
    try { launch_auto_install(); } catch (...) {}
    g_update_client.reset();
#endif
    auto* state = reinterpret_cast<PluginState*>(refcon);
    if (state) {
        if (state->window && IsWindow(state->window)) {
            save_and_restore();
            DestroyWindow(state->window);
        }
        state->hotkey.unregister();
        if (state->keyboard_hook) UnhookWindowsHookEx(state->keyboard_hook);
        if (state->hotkey_window && IsWindow(state->hotkey_window)) {
            DestroyWindow(state->hotkey_window);
        }
        if (state->gdiplus_token) Gdiplus::GdiplusShutdown(state->gdiplus_token);
        delete state;
    }
    g_state = nullptr;
    return A_Err_NONE;
}

} // namespace

extern "C" DllExport AEGP_PluginInitFuncPrototype EntryPointFunc;

A_Err EntryPointFunc(SPBasicSuite* pica_basicP, A_long, A_long,
                     AEGP_PluginID aegp_plugin_id, AEGP_GlobalRefcon* global_refconP) {
    // Prove which build the host actually loaded. A stale plugin on disk and a broken
    // fix are indistinguishable from the user's side; this line settles it.
    update_trace(std::string("plugin-loaded version=") + kProductVersion);
    g_basic = pica_basicP;
    g_plugin_id = aegp_plugin_id;
    try {
        xfx::script_bridge().set_available(false);
        AEGP_SuiteHandler suites(pica_basicP);
        suites.CommandSuite1()->AEGP_GetUniqueCommand(&g_command);
        const std::string menu_name = wide_to_ansi(L"效果控制台");
        suites.CommandSuite1()->AEGP_InsertMenuCommand(
            g_command, reinterpret_cast<const A_char*>(menu_name.c_str()),
            AEGP_Menu_WINDOW, AEGP_MENU_INSERT_SORTED);

        g_state = new PluginState();
        Gdiplus::GdiplusStartupInput startup;
        Gdiplus::GdiplusStartup(&g_state->gdiplus_token, &startup, nullptr);
        *global_refconP = reinterpret_cast<AEGP_GlobalRefcon>(g_state);

        g_state->settings = xfx::SettingsStore::load(settings_path());
        refresh_host_theme(true);
        suites.RegisterSuite5()->AEGP_RegisterCommandHook(
            aegp_plugin_id, AEGP_HP_BeforeAE, g_command, command_hook,
            reinterpret_cast<AEGP_CommandRefcon>(g_state));
        suites.RegisterSuite5()->AEGP_RegisterUpdateMenuHook(
            aegp_plugin_id, update_menu_hook, reinterpret_cast<AEGP_UpdateMenuRefcon>(g_state));
        suites.RegisterSuite5()->AEGP_RegisterDeathHook(
            aegp_plugin_id, death_hook, reinterpret_cast<AEGP_DeathRefcon>(g_state));
        const A_Err bridge_registration = suites.RegisterSuite5()->AEGP_RegisterIdleHook(
            aegp_plugin_id, bridge_idle_hook, reinterpret_cast<AEGP_IdleRefcon>(g_state));
        xfx::script_bridge().set_available(bridge_registration == A_Err_NONE);
    } catch (...) {
        delete g_state;
        g_state = nullptr;
        return A_Err_GENERIC;
    }
    return A_Err_NONE;
}
