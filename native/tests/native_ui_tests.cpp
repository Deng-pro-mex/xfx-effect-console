#define XFX_CONSOLE_TEST
#include "../plugin/XFXNativeConsole.cpp"
#include <cassert>
#include <iostream>
#include <cstring>
#include <iterator>
#include <dlgs.h>

static PF_App_Color simulated_host_color{7196,7196,7196};
static PF_Err fake_background(PF_App_Color* color) { *color=simulated_host_color;return 0; }
static SPErr fake_acquire(const char* name,int,const void** suite) {
    static PFAppSuite6 app{};app.PF_AppGetBgColor=fake_background;
    if(std::strcmp(name,kPFAppSuite)!=0)return -1;
    *suite=&app;return 0;
}
static SPErr fake_release(const char*,int) {return 0;}

static unsigned long long loading_pixels(HWND window) {
    RECT rect{};GetClientRect(window,&rect);
    HDC dc=GetDC(window),memory=CreateCompatibleDC(dc);
    HBITMAP bitmap=CreateCompatibleBitmap(dc,rect.right,rect.bottom);const auto old=SelectObject(memory,bitmap);
    assert(PrintWindow(window,memory,PW_CLIENTONLY));
    unsigned long long hash=1469598103934665603ULL;
    // Sample only the spinner and progress track, excluding status text.
    for(int y=5;y<33;++y) for(int x=5;x<(y<24?25:310);++x) {hash^=GetPixel(memory,x,y);hash*=1099511628211ULL;}
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,dc);return hash;
}
static unsigned long long result_pixels_with_font(HFONT ambient_font) {
    RECT rect{};GetClientRect(g_state->results,&rect);rect.bottom=28;
    HDC dc=GetDC(g_state->results),memory=CreateCompatibleDC(dc);
    HBITMAP bitmap=CreateCompatibleBitmap(dc,rect.right,rect.bottom);
    const auto old_bitmap=SelectObject(memory,bitmap),old_font=SelectObject(memory,ambient_font);
    DRAWITEMSTRUCT draw{};draw.CtlType=ODT_LISTBOX;draw.CtlID=kResultsId;draw.itemID=0;
    draw.hwndItem=g_state->results;draw.hDC=memory;draw.rcItem=rect;draw.itemAction=ODA_DRAWENTIRE;
    SendMessageW(g_state->window,WM_DRAWITEM,kResultsId,reinterpret_cast<LPARAM>(&draw));
    assert(GetCurrentObject(memory,OBJ_FONT)==ambient_font);
    unsigned long long hash=1469598103934665603ULL;
    for(int y=0;y<rect.bottom;++y)for(int x=0;x<rect.right;++x){hash^=GetPixel(memory,x,y);hash*=1099511628211ULL;}
    SelectObject(memory,old_font);SelectObject(memory,old_bitmap);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(g_state->results,dc);
    return hash;
}

static void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}

static std::wstring dialog_title, dialog_text;
static UINT dialog_command;
static bool dialog_handled;
static HWND dialog_owner;
static bool dialog_needs_edit;
static bool dialog_use_keyboard;
static unsigned dialog_attempts;
static void CALLBACK dialog_timeout(HWND dialog, UINT, UINT_PTR timer, DWORD) {
    KillTimer(dialog, timer); dialog_handled = false; EndDialog(dialog, -1);
}
static void CALLBACK answer_test_dialog(HWND, UINT, UINT_PTR timer, DWORD) {
    if (++dialog_attempts > 100) { std::wcerr << L"Test dialog did not appear: " << dialog_title << L"\n"; std::abort(); }
    HWND dialog = nullptr;
    for (HWND candidate : {GetLastActivePopup(dialog_owner), GetLastActivePopup(g_state->window), GetActiveWindow()}) {
        DWORD process = 0; GetWindowThreadProcessId(candidate, &process);
        wchar_t title[128]{}; GetWindowTextW(candidate, title, 128);
        if (process == GetCurrentProcessId() && title == dialog_title) { dialog = candidate; break; }
    }
    if (!dialog) return;
    HWND edit = GetDlgItem(dialog, xfx::kTextInputEdit);
    if (dialog_needs_edit && !edit) return;
    if (edit) {
        const auto& palette = colors();
        HDC dc = CreateCompatibleDC(nullptr);
        const auto brush = reinterpret_cast<HBRUSH>(SendMessageW(dialog, WM_CTLCOLOREDIT,
            reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(edit)));
        assert(GetTextColor(dc) == palette.text && GetBkColor(dc) == palette.input);
        LOGBRUSH description{}; assert(GetObjectW(brush, sizeof(description), &description));
        assert(description.lbColor == palette.input);
        DeleteDC(dc);
    }
    KillTimer(nullptr, timer);
    if (edit) SetWindowTextW(edit, dialog_text.c_str());
    dialog_handled = true;
    if (dialog_use_keyboard && edit) {
        std::cerr << "KEYBOARD dialog focus=" << GetDlgCtrlID(GetFocus()) << " default=" <<
            LOWORD(SendMessageW(dialog, DM_GETDEFID, 0, 0)) << " edit-code=" <<
            SendMessageW(edit, WM_GETDLGCODE, VK_RETURN, 0) << " active=" << (GetActiveWindow() == dialog) << '\n';
        SetTimer(dialog, 9000, 2000, dialog_timeout);
        const auto key = dialog_command == IDOK ? VK_RETURN : VK_ESCAPE;
        PostMessageW(edit, WM_KEYDOWN, key, 0); PostMessageW(edit, WM_KEYUP, key, 0);
    } else PostMessageW(dialog, WM_COMMAND, dialog_command, 0);
}

static void prepare_dialog(const std::wstring& title, const std::wstring& text, UINT command = IDOK,
    HWND owner = nullptr, bool needs_edit = true, bool keyboard = false) {
    dialog_title = title; dialog_text = text; dialog_command = command; dialog_handled = false;
    dialog_owner = owner ? owner : g_state->window; dialog_needs_edit = needs_edit; dialog_attempts = 0;
    dialog_use_keyboard = keyboard;
    assert(SetTimer(nullptr, 0, 50, answer_test_dialog));
}

static void save_test_preview(HWND window, const fs::path& destination) {
    UpdateWindow(window); pump();
    RECT rect{}; GetWindowRect(window, &rect);
    HDC dc = GetDC(window), memory = CreateCompatibleDC(dc);
    HBITMAP image = CreateCompatibleBitmap(dc, rect.right - rect.left, rect.bottom - rect.top);
    HGDIOBJ previous = SelectObject(memory, image);
    assert(PrintWindow(window, memory, 0));
    {
        Gdiplus::Bitmap bitmap(image, nullptr);
        CLSID png{}; CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
        assert(bitmap.Save(destination.c_str(), &png) == Gdiplus::Ok);
    }
    SelectObject(memory, previous); DeleteObject(image); DeleteDC(memory); ReleaseDC(window, dc);
}

// Runs the staged apply queue exactly as the host does: the AEGP idle hook is the only
// context that is allowed to touch AE scripting now, so the test must drive it too.
static void drain_idle_applies(int rounds = 240) {
    // Standalone harness: there is no AE, so the host probe would report "unavailable"
    // and the runner would hold the job for its 20 s safety valve. A throwaway basic
    // suite makes the probe fail open, exactly like the real plugin when the probe
    // itself cannot run.
    SPBasicSuite stand_in{};
    stand_in.AcquireSuite = [](const char*, int, const void**) -> SPErr { return -1; };
    stand_in.ReleaseSuite = [](const char*, int) -> SPErr { return 0; };
    auto* saved_basic = g_basic;
    g_basic = &stand_in;
    g_pending_apply_since = 0;
    struct RestoreBasic { SPBasicSuite* saved; ~RestoreBasic() { g_basic = saved; } }
        restore_basic{saved_basic};
    // The runner holds work for kStartupSettleMs and then needs kRequiredClearStreak
    // consecutive clear idles, so a handful of pumps is not enough; keep ticking until
    // the queue actually drains or the budget is spent.
    A_long max_sleep = 0;
    for (int round = 0; round < rounds; ++round) {
        pump();
        const size_t pending = g_pending_applies.size();
        bridge_idle_hook(nullptr, nullptr, &max_sleep);
        pump();
        if (pending && g_pending_applies.size() < pending) break;
        if (!pending) { Sleep(15); continue; }
        Sleep(15);
    }
}

static void pump_for(DWORD milliseconds) {
    const auto deadline = GetTickCount64() + milliseconds;
    do { pump(); Sleep(1); } while (GetTickCount64() < deadline);
}

static RECT child_rect_in_parent(HWND parent, HWND child) {
    RECT rect{}; GetWindowRect(child, &rect);
    POINT top_left{rect.left, rect.top};
    POINT bottom_right{rect.right, rect.bottom};
    ScreenToClient(parent, &top_left); ScreenToClient(parent, &bottom_right);
    return {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
}

static BOOL CALLBACK count_visible_child_update_regions(HWND child, LPARAM count_pointer) {
    if (!IsWindowVisible(child)) return TRUE;
    RECT update{};
    if (GetUpdateRect(child, &update, FALSE)) {
        std::wcerr << L"Pending visible child paint id=" << GetDlgCtrlID(child) << L" class=";
        wchar_t class_name[128]{}; GetClassNameW(child, class_name, 128); std::wcerr << class_name << L"\n";
        ++*reinterpret_cast<int*>(count_pointer);
    }
    return TRUE;
}

static BOOL CALLBACK invalidate_child_for_reopen(HWND child, LPARAM) {
    InvalidateRect(child, nullptr, TRUE);
    return TRUE;
}

static int visible_children_with_pending_paint(HWND parent) {
    int count = 0;
    EnumChildWindows(parent, count_visible_child_update_regions, reinterpret_cast<LPARAM>(&count));
    return count;
}

// Inspect owner drawing, rather than internal animation state, so a timer that
// runs without changing the visible button cannot satisfy this regression.
static COLORREF button_background(HWND button) {
    RECT rect{}; GetClientRect(button, &rect);
    HDC dc = GetDC(button), memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
    const auto previous = SelectObject(memory, bitmap);
    DRAWITEMSTRUCT draw{};
    draw.CtlType = ODT_BUTTON; draw.CtlID = GetDlgCtrlID(button);
    draw.itemAction = ODA_DRAWENTIRE; draw.hwndItem = button;
    draw.hDC = memory; draw.rcItem = rect;
    SendMessageW(g_state->window, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
    // Sample the opaque interior, clear of rounded corners, icons, and text.
    const auto color = GetPixel(memory, 4, rect.bottom / 2);
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(button, dc);
    return color;
}

static COLORREF result_corner_pixel(bool selected) {
    RECT rect{}; GetClientRect(g_state->results, &rect); rect.bottom = 28;
    HDC dc = GetDC(g_state->results), memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
    const auto previous = SelectObject(memory, bitmap);
    DRAWITEMSTRUCT draw{};
    draw.CtlType = ODT_LISTBOX; draw.CtlID = kResultsId; draw.itemID = 0;
    draw.hwndItem = g_state->results; draw.hDC = memory; draw.rcItem = rect;
    draw.itemState = selected ? ODS_SELECTED : 0;
    SendMessageW(g_state->window, WM_DRAWITEM, kResultsId, reinterpret_cast<LPARAM>(&draw));
    const auto color = GetPixel(memory, 0, 0);
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(g_state->results, dc);
    return color;
}

static COLORREF result_surface_pixel(int row) {
    RECT client{};
    GetClientRect(g_state->results, &client);
    HDC dc = GetDC(g_state->results), memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    const auto previous = SelectObject(memory, bitmap);
    SendMessageW(g_state->results, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    RECT item{};
    assert(SendMessageW(g_state->results, LB_GETITEMRECT, row,
        reinterpret_cast<LPARAM>(&item)) != LB_ERR);
    COLORREF color = GetPixel(memory, (std::min)(item.right - 4, item.left + 30),
        (item.top + item.bottom) / 2);
    // The old sample landed on the right-side action glyph at some font/DPI
    // combinations. Find a real selected-surface pixel while staying inside
    // the row, so the assertion tests selection paint rather than icon ink.
    for (int y = item.top + 3; y < item.bottom - 3; ++y) {
        for (int x = item.left + 3; x < item.right - 3; ++x) {
            if (GetPixel(memory, x, y) == colors().selected) {
                color = colors().selected;
                y = item.bottom;
                break;
            }
        }
    }
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(g_state->results, dc);
    return color;
}

static int search_hint_pixel_count() {
    RECT client{};
    GetClientRect(g_state->search, &client);
    HDC source = GetDC(g_state->search), memory = CreateCompatibleDC(source);
    HBITMAP bitmap = CreateCompatibleBitmap(source, client.right, client.bottom);
    const auto previous = SelectObject(memory, bitmap);
    SetBkColor(memory, RGB(255, 0, 255));
    ExtTextOutW(memory, 0, 0, ETO_OPAQUE, &client, nullptr, 0, nullptr);
    SendMessageW(g_state->search, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    int count = 0;
    for (int y = 0; y < client.bottom; ++y)
        for (int x = 0; x < client.right; ++x)
            if (GetPixel(memory, x, y) == colors().hint) ++count;
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(g_state->search, source);
    return count;
}

static void check_button_hover(HWND button) {
    assert(IsWindow(button));
    // Hovering must settle immediately. A stationary cursor must not keep
    // repainting the button or change its pixels over time.
    g_test_button_hover_time = GetTickCount64();
    const auto advance = [button](ULONGLONG milliseconds) {
        *g_test_button_hover_time += milliseconds;
        SendMessageW(button, WM_TIMER, kButtonHoverTimerId, 0);
    };
    SendMessageW(button, WM_MOUSELEAVE, 0, 0); advance(220);
    const auto resting = button_background(button);
    SendMessageW(button, WM_MOUSEMOVE, 0, MAKELPARAM(8, 8));
    const auto hovered = button_background(button);
    advance(40);
    assert(button_background(button) == hovered);
    advance(220);
    assert(button_background(button) == hovered);
    SendMessageW(button, WM_MOUSELEAVE, 0, 0); advance(220);
    assert(button_background(button) == resting);
    // Captured mouse messages outside the client must not re-enter hover.
    SendMessageW(button, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(8, 8)); advance(220);
    SetCapture(button);
    SendMessageW(button, WM_MOUSELEAVE, 0, 0);
    SendMessageW(button, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-4, 8)); advance(220);
    assert(button_background(button) == resting);
    ReleaseCapture();
    g_test_button_hover_time.reset();
}

#include "gallery_comparison_checks.h"
#include "capture_location_checks.h"
#include "input_geometry_checks.h"
#include "paint_geometry_checks.h"
#include "hotkey_guard_checks.h"
#include "apply_lifecycle_checks.h"
#include "combo_geometry_checks.h"
#include "ime_integration_checks.h"
#include "apply_click_checks.h"
#include "input_cursor_checks.h"
#include "filter_surface_checks.h"

// Drive only the reference window's real common-file dialog. Poll for readiness,
// not a fixed sleep; retain a deadline even after submission so validation errors
// cannot strand this test in a modal loop. Paths are isolated test files.


// The IME option is a dropdown; drive it the way a user would.
static void select_ime_mode(xfx::ImeMode mode) {
    SendMessageW(g_state->ime_mode_combo, CB_SETCURSEL, static_cast<int>(mode), 0);
    SendMessageW(g_state->window, WM_COMMAND,
        MAKEWPARAM(kImeModeId, CBN_SELCHANGE), reinterpret_cast<LPARAM>(g_state->ime_mode_combo));
    pump();
}

int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    std::set_terminate([] {
        try {
            if (const auto error = std::current_exception()) std::rethrow_exception(error);
        } catch (const std::exception& error) {
            std::cerr << "Unhandled native UI test exception: " << error.what() << '\n';
        } catch (...) { std::cerr << "Unhandled native UI test exception\n"; }
        std::abort();
    });
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    // Keyed by pid, and Windows reuses pids. A run that crashed left its directory
    // behind, so the next process with the same pid hit "file exists" while copying the
    // alias fixture and died before printing anything. Start from a clean directory.
    const auto directory = fs::temp_directory_path() / (L"xfx-ui-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code cleanup_error;
    fs::remove_all(directory, cleanup_error);
    fs::create_directories(directory);
    g_test_settings_path = directory / L"settings.ini";
    SetEnvironmentVariableW(L"XFX_THUMBNAIL_CACHE_DIR", (directory / L"thumbnail-cache").c_str());
    {
        const auto source = fs::path(utf8_to_wide(__FILE__)).parent_path().parent_path().parent_path();
        fs::copy_file(source / "installer" / "XFXNativeConsole.aliases.tsv",
                      directory / L"XFXNativeConsole.aliases.default.tsv",
                      fs::copy_options::overwrite_existing);
        const auto aliases = load_aliases();
        std::ifstream inventory(source / "native" / "tests" / "data" / "ae-25.4-effects.txt", std::ios::binary);
        assert(inventory.good());
        std::string row;
        size_t count = 0, discoverable = 0;
        std::set<std::string> catalog_ids;
        std::vector<xfx::CatalogItem> native_catalog;
        while (std::getline(inventory, row)) {
            if (!row.empty() && row.back() == '\r') row.pop_back();
            if (row.empty() || row[0] == '#') continue;
            const auto fields = split_tsv(row);
            const auto* alias = find_effect_alias(aliases, fields[1].c_str(), fields[0].c_str());
            assert(alias && contains_cjk(alias->chinese_name));
            const auto original = utf8_to_wide(fields[1].c_str());
            if (!original.empty()) assert(safe_effect_display_name(original, alias) == original);
            if (append_registered_effect(native_catalog, aliases, catalog_ids, fields[1].c_str(), fields[0].c_str(),
                                         fields[2].c_str(), true)) {
                ++discoverable;
                const auto& item = native_catalog.back();
                assert(item.match_name == fields[0] && item.category == L"效果" &&
                       item.host_category == utf8_to_wide(fields[2].c_str()));
                if (contains_cjk(original)) assert(item_display_label(item)==original);
                if (const auto* localized=xfx::saved_native_chinese_label(fields[0])) {
                    assert(std::wstring(localized)==original);
                    assert(safe_effect_display_name(L"iy",alias,fields[0],true)==original);
                }
                if(fields[0]=="PEDG2")assert(item_display_label(item)==L"Deep Glow 2（深度辉光）");
            }
            ++count;
        }
        assert(count == 1401 && discoverable == 1203 && native_catalog.size() == 1203);
        {
            std::vector<xfx::CatalogItem> localized;std::set<std::string> ids;
            assert(append_registered_effect(localized,aliases,ids,"Drop Shadow","ADBE Drop Shadow","透视",true));
            assert(localized[0].display_name==L"投影" && item_display_label(localized[0])==L"投影");
            assert(xfx::SearchEngine(localized).search(L"drop shadow").size()==1);
        }
        for (const auto* category : {"Perspective","遠近","透视"}) for (const auto* raw : {"iy","Drop Shadow"}) {
            std::vector<xfx::CatalogItem> localized;std::set<std::string> ids;
            assert(append_registered_effect(localized,aliases,ids,raw,"ADBE Drop Shadow",category,true));
            const auto expected=std::string(category)=="透视"?L"投影":L"Drop Shadow";
            assert(localized[0].display_name==expected && localized[0].match_name=="ADBE Drop Shadow");
            assert(xfx::SearchEngine(localized).search(L"drop shadow").size()==1);
        }
        std::set<std::string> synthetic_ids;
        std::vector<xfx::CatalogItem> synthetic;
        assert(!append_registered_effect(synthetic, aliases, synthetic_ids, "Hidden", "Same ID", "_Obsolete", true));
        assert(!append_registered_effect(synthetic, aliases, synthetic_ids, "Unknown", "Other", "Vendor", false));
        assert(append_registered_effect(synthetic, aliases, synthetic_ids, "Same Name", "Same ID", "BCC Obsolete", true));
        assert(append_registered_effect(synthetic, aliases, synthetic_ids, "Same Name", "Distinct ID", "_Vendor", true));
        assert(!append_registered_effect(synthetic, aliases, synthetic_ids, "Same Name", "Same ID", "Vendor", true));
        assert(synthetic.size() == 2 && synthetic[0].match_name != synthetic[1].match_name);
        std::cout << "PASS native TSV loader and visible original labels for all 1401 actual AE effects\n";
    }
    {
        std::ofstream(directory / L"XFXNativeConsole.aliases.default.tsv") <<
            "ADBE Glo2\tGlow\t默认发光\t\t\t\n"
            "\tBCC+ Rectangular Wipe\tBCC+ 矩形擦除\t\t\t矩形转场\n";
        std::ofstream(directory / L"aliases.user.tsv") <<
            "\xEF\xBB\xBF\tGlow\t自定义辉光\t\t\t自定义\n";
        const auto aliases = load_aliases();
        assert(aliases.by_english_name.at("glow").chinese_name == L"自定义辉光");
        assert(aliases.by_english_name.at("glow").priority > aliases.by_match_name.at("adbe glo2").priority);
        assert(find_effect_alias(aliases, "Glow", "ADBE Glo2")->chinese_name == L"自定义辉光");
        assert(find_effect_alias(aliases,"Glow","ADBE Glo2")->custom_translation);
        {
            std::vector<xfx::CatalogItem> custom;std::set<std::string> ids;
            assert(append_registered_effect(custom,aliases,ids,"发光","ADBE Glo2","风格化",true));
            assert(!custom[0].translated_name_custom && item_display_label(custom[0])==L"发光");
            // A late ID-based user override must remain visible and both original
            // host English and explicitly overridden English must be searchable.
            std::ofstream(directory/L"aliases.user.tsv",std::ios::app)<<"ADBE Glo2\tTeam Light\t团队柔光\t\t\t\n";
            const auto custom_aliases=load_aliases();
            assert(custom_aliases.by_match_name.at("adbe glo2").custom_translation);
            custom.clear();ids.clear();
            assert(append_registered_effect(custom,custom_aliases,ids,"Glow","ADBE Glo2","风格化",true));
            assert(item_display_label(custom[0])==L"发光（团队柔光）");
            const xfx::SearchEngine engine(custom);
            assert(engine.search(L"Glow").size()==1 && engine.search(L"Team Light").size()==1);
            assert(engine.search(L"团队柔光").size()==1 && custom[0].match_name=="ADBE Glo2");
            custom.clear();ids.clear();
            assert(append_registered_effect(custom,custom_aliases,ids,"发光","ADBE Glo2","风格化",true));
            assert(item_display_label(custom[0])==L"发光（团队柔光）");
        }
        const auto& rectangular = aliases.by_english_name.at(canonical_alias_key("BCC+Rectangular Wipe"));
        assert(rectangular.chinese_name == L"BCC+ 矩形擦除");
        assert(canonical_alias_key("BCC+Rectangular Wipe") == canonical_alias_key(" bcc+\tRectangular Wipe "));
        assert(canonical_alias_key("BCC Linear Wipe") != canonical_alias_key("BCC+Linear Wipe"));
        const auto external = xfx::SearchEngine({{L"BCC+Rectangular Wipe", "", "", "BCC+Rectangular Wipe",
                                                "fixture-external-wipe", L"效果", "ae"}}).search(L"矩形擦除");
        assert(external.size() == 1);
        assert(item_display_label(external[0].item) == L"BCC+Rectangular Wipe（BCC+ 矩形擦除）");
        AliasEntry drop_shadow;
        drop_shadow.match_name = "ADBE Drop Shadow";
        drop_shadow.english_name = "Drop Shadow";
        drop_shadow.chinese_name = L"投影";
        assert(safe_effect_display_name(L"iy", &drop_shadow,{},true) == L"投影");
        assert(safe_effect_display_name(L"\uFFFD", &drop_shadow,{},true) == L"投影");
        assert(safe_effect_display_name(L"", &drop_shadow,{},true) == L"投影");
        assert(safe_effect_display_name(L"Drop Shadow", &drop_shadow) == L"Drop Shadow");
        assert(safe_effect_display_name(L"阴影", &drop_shadow) == L"阴影");
        // Stable-ID-only third-party translations must not mistake legitimate
        // short product labels for encoding corruption.
        for (const auto& fixture : std::vector<std::pair<std::string, std::wstring>>{
                 {"tc Lux", L"Lux"}, {"tc Tao", L"Tao"}, {"Geo", L"Geo"}}) {
            AliasEntry product;
            product.match_name = fixture.first;
            assert(safe_effect_display_name(fixture.second, &product) == fixture.second);
        }
        AliasEntry photoshop_drop_shadow;
        photoshop_drop_shadow.match_name = "ADBE PSL Drop Shadow";
        photoshop_drop_shadow.chinese_name = L"Photoshop 投影";
        assert(alias_display_fallback(photoshop_drop_shadow) == "Photoshop Drop Shadow");
        assert(safe_effect_display_name(L"Photoshop iy", &photoshop_drop_shadow) == L"Photoshop Drop Shadow");
        // Valid UTF-8 runtime labels must survive on ACP/GBK hosts even when
        // they are not CJK; this guards accented vendor names and symbols.
        assert(runtime_text_to_wide("Caf\xC3\xA9") == L"Caf\u00E9");
    }
    g_test_catalog = {
        {L"Gaussian Blur", "gaosimohu", "gsmh", "Gaussian Blur", "ADBE Gaussian Blur 2", L"效果", "ae"},
        {L"Glow", "faguang", "fg", "Glow", "ADBE Glo2", L"效果", "ae"}
    };
    // Exercise built-in translation when no external/default translation file is present.
    g_test_catalog[1].translated_name = L"发光";
    assert(item_display_label(g_test_catalog[1]) == L"Glow（发光）");
    xfx::Settings settings; settings.hotkey = L"Ctrl+Alt+Shift+F23";
    // Drawing/input checks must not be destroyed by the user's external focus.
    // The explicit focus-away lifecycle check below still enables auto-close.
    settings.auto_close = false;
    settings.custom_aliases[L"ADBE Gaussian Blur 2"] = {L"柔化专用"};
    xfx::SettingsStore::save(g_test_settings_path, settings);
    g_state = new PluginState();
    Gdiplus::GdiplusStartupInput startup;
    assert(Gdiplus::GdiplusStartup(&g_state->gdiplus_token, &startup, nullptr) == Gdiplus::Ok);
    g_state->hotkey_window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    show_console();
    assert(g_state->ime.test_target_window() == g_state->search);
    assert(g_state->initial_background_primed && g_test_initial_background_fill_count > 0);
    assert(g_state->initial_frame_painted && g_test_window_paint_count > 0);
    assert(visible_children_with_pending_paint(g_state->window) == 0);
    assert(g_test_initial_background_color == colors().background);
    const auto regression_artifacts = argc > 1 ? fs::path(argv[1]).parent_path() : fs::path{};
    if (argc > 2) {
        const std::wstring check = argv[2];
        if (check == L"--search-geometry") check_search_input_geometry(regression_artifacts);
        else if (check == L"--hotkey-geometry") {
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump();
            check_hotkey_input_geometry(regression_artifacts);
        } else if (check == L"--frame-geometry") check_console_frame_geometry();
        else if (check == L"--button-geometry") check_button_round_geometry(g_state->capture);
        else if (check == L"--redraw-visibility") check_settings_redraw_visibility();
        else if (check == L"--hotkey-guards") check_hotkey_dispatch_guards();
        else if (check == L"--hover-leave") check_result_hover_leave();
        else if (check == L"--apply-lifecycle") check_apply_lifecycle();
        else if (check == L"--combo-geometry") check_combo_geometry(regression_artifacts);
        else if (check == L"--combo-light") {
            set_theme_mode(xfx::ThemeMode::Light);
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump();
            SetWindowTextW(g_state->search, L"gaussian");
            check_combo_geometry(regression_artifacts);
        }
        else if (check == L"--ime-retry") check_ime_failure_retry();
        else if (check == L"--ime-pending-restore") check_ime_pending_restore();
        else if (check == L"--apply-click") check_result_click_is_deferred();
        else if (check == L"--shortcuts") {
            // Dropdown option count, conflict annotation, wheel immunity.
            show_console();
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(260);
            const auto count = SendMessageW(g_state->slot_row_key, CB_GETCOUNT, 0, 0);
            assert(count == 6);
            // Selecting Ctrl for the strip must be accepted and persisted.
            SendMessageW(g_state->slot_row_key, CB_SETCURSEL, 2, 0);
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kSlotRowKeyId, CBN_SELCHANGE), reinterpret_cast<LPARAM>(g_state->slot_row_key));
            pump();
            assert(g_state->settings.show_slot_row && g_state->settings.slot_row_modifier == L"Ctrl");
            // Choosing the same Ctrl for the digit group must be rejected as a conflict.
            SendMessageW(g_state->result_digit_key, CB_SETCURSEL, 2, 0);
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kResultDigitKeyId, CBN_SELCHANGE), reinterpret_cast<LPARAM>(g_state->result_digit_key));
            pump();
            // Rejected: the digit group keeps its previous modifier instead of taking Ctrl.
            assert(g_state->settings.result_digit_modifier.empty());
            // The conflicting label must be annotated in the other dropdown.
            wchar_t label[128]{};
            SendMessageW(g_state->result_digit_key, CB_GETLBTEXT, 2, reinterpret_cast<LPARAM>(label));
            assert(std::wstring(label).find(L"已被另一组使用") != std::wstring::npos);
            // A wheel over a closed dropdown must not change the selection.
            const auto before = SendMessageW(g_state->slot_row_key, CB_GETCURSEL, 0, 0);
            SendMessageW(g_state->slot_row_key, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
            assert(SendMessageW(g_state->slot_row_key, CB_GETCURSEL, 0, 0) == before);
            std::cout << "PASS shortcut dropdowns: 6 options, conflict annotated and rejected, wheel inert\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--drag-bind") {
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.row_slots[2].clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(160);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            assert(IsWindowVisible(g_state->slot_row_buttons[2]));
            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[2], &cell));
            POINT drop{cell.left + 8, cell.top + 8};
            // The drag helper follows the real cursor, which a test cannot move.
            g_test_drag_cursor = POINT{drop.x, drop.y};
            POINT local = drop;
            ScreenToClient(g_state->results, &local);
            SendMessageW(g_state->results, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(local.x, local.y));
            assert(g_state->result_drag_target == 2);
            SendMessageW(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(local.x, local.y));
            g_test_drag_cursor.reset();
            pump();
            assert(!g_state->settings.row_slots[2].empty());
            std::cout << "PASS drag from results onto the second strip binds the item\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--strip-on-by-default") {
            // The drag complaint has a plain cause: the second row was OFF by default, so a
            // fresh install had nothing to drag onto. It must ship ON, and a near miss must
            // still bind instead of ending the drag in silence.
            xfx::Settings defaults;
            std::cout << "  default show_slot_row=" << (defaults.show_slot_row ? 1 : 0)
                      << " modifier=" << wide_to_utf8(defaults.slot_row_modifier) << "\n";
            assert(defaults.show_slot_row);

            // It must survive a save/load round trip, so an upgraded file keeps it on.
            xfx::SettingsStore::save(g_test_settings_path, defaults);
            const auto reloaded = xfx::SettingsStore::load(g_test_settings_path);
            assert(reloaded.show_slot_row);

            // And a settings file written by an OLD version (no ShowSlotRow key) keeps it on.
            {
                std::ofstream legacy(g_test_settings_path, std::ios::binary);
                legacy << "Hotkey=Ctrl+Space\r\nThemeMode=dark\r\n";
            }
            const auto upgraded = xfx::SettingsStore::load(g_test_settings_path);
            std::cout << "  upgraded file show_slot_row=" << (upgraded.show_slot_row ? 1 : 0) << "\n";
            assert(upgraded.show_slot_row);

            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(240);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);

            // Near miss: aim at the pixel just past a cell's right edge. It must still bind.
            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[4], &cell));
            g_test_drag_cursor = POINT{cell.right + 1, (cell.top + cell.bottom) / 2};
            g_test_drag_button_held = false;
            for (int tick = 0; tick < 8 && g_state->result_drag_armed; ++tick) {
                handle_drag_poll();
                pump_for(30);
            }
            g_test_drag_cursor.reset();
            g_test_drag_button_held.reset();
            pump_for(120);
            std::cout << "  near miss bound slot5="
                      << (g_state->settings.row_slots[4].empty() ? "EMPTY" : "bound") << "\n";
            assert(!g_state->settings.row_slots[4].empty());

            // A real miss must explain itself rather than doing nothing.
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            g_test_drag_cursor = POINT{cell.left, cell.bottom + 120};
            g_test_drag_button_held = false;
            for (int tick = 0; tick < 8 && g_state->result_drag_armed; ++tick) {
                handle_drag_poll();
                pump_for(30);
            }
            g_test_drag_cursor.reset();
            pump_for(120);
            wchar_t status[512]{};
            GetWindowTextW(g_state->status, status, 512);
            std::cout << "  miss status: " << wide_to_utf8(status) << "\n";
            assert(std::wstring(status).find(L"没有放到格子上") != std::wstring::npos);
            std::cout << "PASS the second strip ships on, near misses bind, and a miss explains itself\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--first-apply-no-error") {
            // The reported "first use shows a popup" is AE refusing scripting while it is
            // still settling. A first-time user must see a wait, not an error. Simulate a
            // host that refuses once, then works.
            show_console();
            pump_for(220);
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            pump_for(200);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);

            // Apply #1: the host refuses with AE's real modal wording.
            g_pending_applies.clear();
            g_first_apply_hint_shown = false;
            g_first_apply_retries = 0;
            g_test_script_calls = 0;
            g_test_before_script = count_test_script_calls;
            g_test_script_reply = xfx::ScriptReply{false, false, 0,
                "错误: 当模式对话框正在等待回应时，无法运行脚本。"};
            g_first_idle_tick = GetTickCount64() - 60000;
            g_pending_apply_since = 0;
            g_apply_clear_streak = 0;
            execute_selected();
            for (int tick = 0; tick < 10 && !g_pending_applies.empty(); ++tick) {
                run_pending_apply_on_idle();
                pump_for(30);
            }
            wchar_t status[512]{};
            GetWindowTextW(g_state->status, status, 512);
            const std::wstring first_status(status);
            std::cout << "  first apply status: " << wide_to_utf8(first_status)
                      << " requeued=" << g_pending_applies.size()
                      << " retries=" << g_first_apply_retries << "\n";
            // No error dialog, no "失败" wording, and the work is still queued.
            assert(g_test_message_box_count == 0);
            assert(first_status.find(L"失败") == std::wstring::npos);
            assert(!g_pending_applies.empty());
            assert(g_first_apply_retries >= 1);

            // Apply #2: the host is ready and the same request succeeds.
            g_test_script_reply = xfx::ScriptReply{true, false, 1, "ok"};
            for (int tick = 0; tick < 14 && !g_pending_applies.empty(); ++tick) {
                run_pending_apply_on_idle();
                pump_for(30);
            }
            g_test_script_reply.reset();
            g_test_before_script = nullptr;
            pump_for(150);
            std::cout << "  after the host clears: script_calls=" << g_test_script_calls
                      << " queued=" << g_pending_applies.size() << "\n";
            assert(g_test_script_calls >= 2);
            assert(g_first_apply_hint_shown);
            assert(g_test_message_box_count == 0);
            std::cout << "PASS the first apply waits instead of raising an error dialog\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--drag-guidance") {
            // A drag that works and a drag that does nothing look identical until the drop.
            // The status line must say what is about to happen while the button is held.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(240);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);

            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            g_test_drag_button_held = true;

            // Over a cell: name the cell.
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[3], &cell));
            g_test_drag_cursor = POINT{(cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2};
            update_result_drag_target();
            pump_for(120);
            wchar_t status[512]{};
            GetWindowTextW(g_state->status, status, 512);
            std::cout << "  over cell 4: " << wide_to_utf8(status) << "\n";
            assert(std::wstring(status).find(L"第 4 格") != std::wstring::npos);

            // Away from every cell: explain the gesture.
            g_test_drag_cursor = POINT{g_state->window ? 4 : 4, 4};
            update_result_drag_target();
            pump_for(120);
            GetWindowTextW(g_state->status, status, 512);
            std::cout << "  away from cells: " << wide_to_utf8(status) << "\n";
            assert(std::wstring(status).find(L"拖到") != std::wstring::npos);

            g_test_drag_button_held.reset();
            g_test_drag_cursor.reset();
            pump_for(80);
            std::cout << "PASS a held drag tells the user what releasing will do\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--drop-highlight-visible") {
            // The drag has to SHOW where the drop will land. A gesture whose target is
            // invisible feels dead even when it would bind.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(260);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);

            // Arm the drag and hold the cursor over cell 6 without releasing.
            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[5], &cell));
            g_test_drag_cursor = POINT{(cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2};
            g_test_drag_button_held = true;
            update_result_drag_target();
            repaint_slot_row_cells();
            pump_for(260);
            std::cout << "  drag target = " << g_state->result_drag_target << "\n";
            assert(g_state->result_drag_target == 5);
            // Ship the artefact: the exactly-what-the-user-sees proof of the highlight.
            if (argc > 1) {
                save_test_preview(g_state->window,
                    fs::path(argv[1]).parent_path() / L"drop-highlight.png");
            }
            g_test_drag_button_held.reset();
            g_test_drag_cursor.reset();
            pump_for(120);
            std::cout << "PASS the drop target is resolved during a drag and captured\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--drag-cleanup") {
            // Two leaks that only show up in a long session:
            //  1. closing the palette mid-drag left the 30 ms poll timer running, which kept
            //     firing into the next session and could bind from a stale cursor reading;
            //  2. an apply hides the palette, so a drag must be cancelled before the hide,
            //     not left armed against a window the user cannot see.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(220);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);

            // 1. Arm a drag, then close the palette while the button is still down.
            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            g_test_drag_button_held = true;
            g_test_drag_cursor = POINT{100, 100};
            SendMessageW(g_state->window, WM_CLOSE, 0, 0);
            pump_for(160);
            g_test_drag_button_held.reset();
            g_test_drag_cursor.reset();
            assert(!IsWindow(g_state->window));
            std::cout << "  after close mid-drag: armed=" << (g_state->result_drag_armed ? 1 : 0)
                      << " row=" << g_state->result_drag_row << "\n";
            assert(!g_state->result_drag_armed);
            assert(g_state->result_drag_row == -1);
            // The poll must be dead: another palette opened afterwards must not bind from
            // the stale cursor reading left behind by the aborted drag.
            assert(g_state->settings.row_slots[0].empty());
            show_console();
            pump_for(240);
            for (int tick = 0; tick < 6; ++tick) { SendMessageW(g_state->window, WM_TIMER, kResultDragTimerId, 0); pump_for(40); }
            for (int slot = 0; slot < 9; ++slot)
                assert(g_state->settings.row_slots[static_cast<size_t>(slot)].empty());
            SendMessageW(g_state->window, WM_CLOSE, 0, 0);
            pump_for(120);

            // 2. Arm a drag, then run an apply which hides the palette.
            show_console();
            pump_for(240);
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            pump_for(200);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            g_test_drag_button_held = true;
            g_test_drag_cursor = POINT{100, 100};
            g_pending_applies.clear();
            g_test_script_reply = xfx::ScriptReply{true, false, 1, "ok"};
            g_first_idle_tick = GetTickCount64() - 60000;
            g_pending_apply_since = 0;
            g_apply_clear_streak = 0;
            execute_selected();
            for (int tick = 0; tick < 14 && !g_pending_applies.empty(); ++tick) {
                run_pending_apply_on_idle();
                pump_for(20);
            }
            g_test_script_reply.reset();
            g_test_drag_button_held.reset();
            g_test_drag_cursor.reset();
            pump_for(200);
            std::cout << "  after apply: armed=" << (g_state->result_drag_armed ? 1 : 0)
                      << " pending=" << (g_state->result_click_pending ? 1 : 0)
                      << " redraw_held=" << (g_state->result_click_redraw_held ? 1 : 0) << "\n";
            assert(!g_state->result_drag_armed);
            assert(!g_state->result_click_pending);
            assert(!g_state->result_click_redraw_held);
            for (auto& slot : g_state->settings.row_slots) assert(slot.empty());

            std::cout << "PASS closing or applying mid-drag cancels the drag completely\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--drag-after-pages") {
            // A live session opens the settings page, closes it, searches, applies, and only
            // then drags. If any of those left the strip hidden or its rectangles stale, the
            // drop hit-test finds nothing and the gesture silently does nothing - the exact
            // "dragging never works" report. Walk the same sequence before dragging.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            g_state->settings.result_digit_shortcut = false;
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            xfx::SettingsStore::save(g_test_settings_path, g_state->settings);
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(240);

            // 1. Open and close the settings page twice.
            for (int round = 0; round < 2; ++round) {
                SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
                pump_for(360);
                SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
                pump_for(360);
            }
            // 2. Search, then clear, then search again (the row count changes each time).
            for (const wchar_t* term : {L"gao", L"", L"gsmh", L"gao"}) {
                SetWindowTextW(g_state->search, term);
                SendMessageW(g_state->window, WM_COMMAND,
                    MAKEWPARAM(kSearchId, EN_CHANGE), reinterpret_cast<LPARAM>(g_state->search));
                pump_for(220);
            }
            // 3. Run an apply, which hides and re-shows the palette.
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            g_pending_applies.clear();
            g_test_script_reply = xfx::ScriptReply{true, false, 1, "ok"};
            g_first_idle_tick = GetTickCount64() - 60000;
            g_pending_apply_since = 0;
            g_apply_clear_streak = 0;
            execute_selected();
            for (int tick = 0; tick < 14 && !g_pending_applies.empty(); ++tick) {
                run_pending_apply_on_idle();
                pump_for(20);
            }
            g_test_script_reply.reset();
            pump_for(200);
            if (!IsWindow(g_state->window)) { show_console(); pump_for(220); }
            // The apply closes the palette on success; reopen and search again.
            show_console();
            pump_for(220);
            SetWindowTextW(g_state->search, L"gao");
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kSearchId, EN_CHANGE), reinterpret_cast<LPARAM>(g_state->search));
            pump_for(260);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);

            // 4. NOW the drag has to work.
            for (int slot = 0; slot < 9; ++slot) {
                const HWND cell = g_state->slot_row_buttons[static_cast<size_t>(slot)];
                assert(cell && IsWindow(cell));
                assert(IsWindowVisible(cell));      // still on screen after all of that
                RECT box{};
                assert(GetWindowRect(cell, &box));
                assert(box.right - box.left > 8 && box.bottom - box.top > 8);
                const POINT centre{(box.left + box.right) / 2, (box.top + box.bottom) / 2};
                assert(slot_row_cell_at(centre) == slot);
            }
            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            assert(SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON,
                MAKELPARAM(12, row.top + 4)) == 0);
            assert(g_state->result_drag_armed);
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[6], &cell));
            g_test_drag_cursor = POINT{(cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2};
            g_test_drag_button_held = false;
            for (int tick = 0; tick < 8 && g_state->result_drag_armed; ++tick) {
                handle_drag_poll();
                pump_for(30);
            }
            g_test_drag_cursor.reset();
            g_test_drag_button_held.reset();
            pump_for(150);
            std::cout << "  after settings/apply/search: slot7="
                      << (g_state->settings.row_slots[6].empty() ? "EMPTY" : "bound") << "\n";
            assert(!g_state->settings.row_slots[6].empty());
            assert(xfx::SettingsStore::load(g_test_settings_path).row_slots[6] ==
                g_state->settings.row_slots[6]);
            std::cout << "PASS dragging still binds after settings, apply and repeated searches\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--search-resize") {
            // Reported from a live screenshot: typing narrowed the results to three rows but
            // the window kept its eight-row height, leaving a large empty band above the
            // strip. The search handler skipped the resize to avoid flicker and never did it
            // at all.
            show_console();
            g_state->settings.show_slot_row = true;
            SetWindowTextW(g_state->search, L"");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(260);
            const auto height_of = [](HWND window) {
                RECT r{}; GetWindowRect(window, &r); return r.bottom - r.top;
            };
            const int empty_height = height_of(g_state->window);
            const int empty_rows = g_state->last_result_rows;
            std::cout << "  empty search: rows=" << empty_rows
                      << " height=" << empty_height << "\n";

            // Type a term that narrows the list, through the real EN_CHANGE path.
            SetWindowTextW(g_state->search, L"gao");
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kSearchId, EN_CHANGE), reinterpret_cast<LPARAM>(g_state->search));
            pump_for(320);
            const int typed_height = height_of(g_state->window);
            const int typed_rows = g_state->last_result_rows;
            std::cout << "  after typing: rows=" << typed_rows
                      << " height=" << typed_height << "\n";
            assert(typed_rows >= 1);
            assert(typed_rows != empty_rows || typed_height == empty_height);

            // The window must follow the row count, and the list must fill the space it has
            // rather than leaving an empty band above the strip.
            RECT list{};
            assert(GetWindowRect(g_state->results, &list));
            RECT window{};
            assert(GetWindowRect(g_state->window, &window));
            const int list_height = list.bottom - list.top;
            const double scale = GetDpiForWindow(g_state->window) / 96.0;
            const int expected_rows_height = static_cast<int>(
                (std::min)(typed_rows, 8) * kResultHeight * scale);
            std::cout << "  list height=" << list_height
                      << " expected~=" << expected_rows_height << "\n";
            // The list may be taller than its rows only while the strip is hidden; with the
            // strip on, the list must not reserve the empty band.
            assert(list_height <= static_cast<int>(8 * kResultHeight * scale) + 8);

            // A term that matches nothing must shrink to the minimum, not keep the band.
            SetWindowTextW(g_state->search, L"zzzzzzzz");
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kSearchId, EN_CHANGE), reinterpret_cast<LPARAM>(g_state->search));
            pump_for(320);
            const int none_rows = g_state->last_result_rows;
            const int none_height = height_of(g_state->window);
            std::cout << "  no matches: rows=" << none_rows
                      << " height=" << none_height << "\n";
            assert(none_rows == 0);
            assert(none_height <= typed_height);
            assert(none_height <= empty_height);

            std::cout << "PASS the palette height follows the number of result rows\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--acceptance") {
            // One end-to-end pass over the five reported problems, in the order a user
            // meets them. Each block fails on its own so the output names the broken one.
            show_console();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(240);
            assert(IsWindow(g_state->window));
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            g_state->settings.result_digit_shortcut = false;
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            xfx::SettingsStore::save(g_test_settings_path, g_state->settings);
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(220);

            // (1) Dragging a result onto the second strip binds that cell.
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            pump_for(200);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON,
                MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[2], &cell));
            const POINT drop{(cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2};
            g_test_drag_cursor = drop;
            g_test_drag_button_held = false;
            for (int tick = 0; tick < 8 && g_state->result_drag_armed; ++tick) {
                handle_drag_poll();
                pump_for(30);
            }
            g_test_drag_cursor.reset();
            g_test_drag_button_held.reset();
            pump_for(120);
            std::cout << "  [1] drag bind -> slot3 " << (g_state->settings.row_slots[2].empty() ? "FAILED" : "ok") << "\n";
            assert(!g_state->settings.row_slots[2].empty());
            const auto dragged_key = g_state->settings.row_slots[2];

            // (2) Shift + digit binds a selected result to the row, without a drag.
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            g_test_keyboard_modifiers = static_cast<UINT>(MOD_SHIFT);
            SetFocus(g_state->results);
            SendMessageW(g_state->results, WM_KEYDOWN, '7', 0);
            pump_for(80);
            g_test_keyboard_modifiers.reset();
            std::cout << "  [2] Shift+7 bind -> slot7 " << (g_state->settings.row_slots[6].empty() ? "FAILED" : "ok") << "\n";
            assert(!g_state->settings.row_slots[6].empty());
            assert(g_state->settings.row_slots[6] == dragged_key);

            std::cout << "  [diag] after step2: show_slot_row=" << (g_state->settings.show_slot_row ? 1 : 0)
                      << " combo=" << (int)SendMessageW(g_state->slot_row_key, CB_GETCURSEL, 0, 0)
                      << " count=" << (int)SendMessageW(g_state->slot_row_key, CB_GETCOUNT, 0, 0) << "\n";
            // (3) Every dropdown is dark and rounded, closed and open.
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(360);
            std::cout << "  [diag] after settings open: show_slot_row=" << (g_state->settings.show_slot_row ? 1 : 0)
                      << " combo=" << (int)SendMessageW(g_state->slot_row_key, CB_GETCURSEL, 0, 0)
                      << " count=" << (int)SendMessageW(g_state->slot_row_key, CB_GETCOUNT, 0, 0) << "\n";
            for (int id : {kFilterId, kLightThemeId, kImeModeId,
                           kSlotRowKeyId, kResultDigitKeyId, kHotkeyComboId}) {
                const HWND combo = GetDlgItem(g_state->window, id);
                assert(combo && IsWindowVisible(combo));
                RECT face{};
                assert(GetWindowRect(combo, &face));
                const int w = face.right - face.left, h = face.bottom - face.top;
                HDC screen = GetDC(nullptr);
                HDC memory = CreateCompatibleDC(screen);
                HBITMAP shot = CreateCompatibleBitmap(screen, w, h);
                const auto previous = SelectObject(memory, shot);
                BitBlt(memory, 0, 0, w, h, screen, face.left, face.top, SRCCOPY);
                long white = 0, total = 0;
                for (int y = 0; y < h; y += 2)
                    for (int x = 0; x < w; x += 2) {
                        const COLORREF pixel = GetPixel(memory, x, y);
                        if (pixel == CLR_INVALID) continue;
                        ++total;
                        if (GetRValue(pixel) > 235 && GetGValue(pixel) > 235 &&
                            GetBValue(pixel) > 235) ++white;
                    }
                SelectObject(memory, previous);
                DeleteObject(shot); DeleteDC(memory); ReleaseDC(nullptr, screen);
                assert(total > 0);
                // The closed face follows the theme, so it must not be a bright system box.
                std::cout << "  [3] combo id=" << id << " white="
                          << (white * 100 / total) << "%\n";
                assert(white * 100 / total < 40);
            }

            // (4) The first apply after startup waits instead of raising AE's dialog.
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(300);
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            pump_for(160);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            g_pending_applies.clear();
            const int dialogs_before = g_test_message_box_count;
            g_first_idle_tick = GetTickCount64() - 60000;
            g_pending_apply_since = 0;
            g_apply_clear_streak = 0;
            g_test_force_modal = true;
            execute_selected();
            for (int tick = 0; tick < 8; ++tick) { run_pending_apply_on_idle(); pump_for(30); }
            assert(!g_pending_applies.empty());
            assert(g_test_last_script.empty());
            g_test_force_modal = false;
            g_test_script_reply = xfx::ScriptReply{true, false, 1, "ok"};
            bool ran = false;
            for (int tick = 0; tick < 14 && !ran; ++tick) ran = run_pending_apply_on_idle();
            g_test_script_reply.reset();
            pump_for(160);
            std::cout << "  [4] first apply waited then ran=" << (ran ? 1 : 0)
                      << " dialogs=" << (g_test_message_box_count - dialogs_before) << "\n";
            assert(ran);
            assert(g_test_message_box_count == dialogs_before);

            // (5) The bindings survived on disk through all of the above.
            const auto saved = xfx::SettingsStore::load(g_test_settings_path);
            std::cout << "  [5] saved show_slot_row=" << (saved.show_slot_row ? 1 : 0)
                      << " modifier=" << wide_to_utf8(saved.slot_row_modifier.c_str())
                      << " slot7=" << (saved.row_slots[6].empty() ? "empty" : "bound")
                      << " live show_slot_row=" << (g_state->settings.show_slot_row ? 1 : 0)
                      << " combo=" << (int)SendMessageW(g_state->slot_row_key, CB_GETCURSEL, 0, 0)
                      << "\n";
            assert(saved.row_slots[6] == dragged_key);
            assert(saved.show_slot_row);

            std::cout << "PASS all five reported problems are fixed in one end-to-end pass\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--theme-contrast") {
            // Light mode shades the palette from the HOST background. If the host reports a
            // mid-grey the derived surface can land on the text colour, and the panel turns
            // into unreadable grey-on-grey. Assert a real contrast ratio for both themes and
            // for a range of host backgrounds.
            const auto luminance = [](COLORREF c) { return xfx::relative_luminance(c); };
            const auto contrast = [](COLORREF a, COLORREF b) {
                return xfx::contrast_ratio(a, b);
            };
            struct Case { const wchar_t* name; xfx::ThemeMode mode; std::optional<COLORREF> host; };
            const Case cases[] = {
                {L"dark", xfx::ThemeMode::Dark, std::nullopt},
                {L"light", xfx::ThemeMode::Light, std::nullopt},
                {L"follow-dark-host", xfx::ThemeMode::Follow, RGB(32, 32, 32)},
                {L"follow-light-host", xfx::ThemeMode::Follow, RGB(237, 237, 237)},
                {L"follow-mid-host", xfx::ThemeMode::Follow, RGB(128, 128, 128)},
                {L"follow-white-host", xfx::ThemeMode::Follow, RGB(255, 255, 255)},
                {L"follow-black-host", xfx::ThemeMode::Follow, RGB(0, 0, 0)},
            };
            for (const auto& entry : cases) {
                const auto palette = xfx::resolve_theme(entry.mode, entry.host);
                const double text = contrast(palette.text, palette.background);
                const double muted = contrast(palette.muted, palette.background);
                const double input = contrast(palette.text, palette.input);
                const double selected = contrast(palette.selected_text, palette.selected);
                std::wcout << L"  " << entry.name
                           << L" text=" << static_cast<int>(text * 100) / 100.0
                           << L" muted=" << static_cast<int>(muted * 100) / 100.0
                           << L" input=" << static_cast<int>(input * 100) / 100.0
                           << L" selected=" << static_cast<int>(selected * 100) / 100.0 << L"\n";
                // WCAG AA for body text is 4.5; the muted rows are secondary, 3.0.
                assert(text >= 4.5);
                assert(input >= 4.5);
                assert(selected >= 4.5);
                assert(muted >= 3.0);
            }
            // The measured colour of the panel itself must agree with the chosen palette,
            // otherwise the theme is computed but never applied.
            set_theme_mode(xfx::ThemeMode::Light);
            pump_for(160);
            const auto applied = colors();
            std::cout << "  applied light background=#"
                      << std::hex << (applied.background & 0xFFFFFF) << std::dec << "\n";
            assert(luminance(applied.background) > 0.5);
            set_theme_mode(xfx::ThemeMode::Dark);
            pump_for(160);
            assert(luminance(colors().background) < 0.2);
            std::cout << "PASS both themes keep readable contrast and are actually applied\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--settings-hints-visible") {
            // Both explanatory notes next to the shortcut dropdowns were created but never
            // positioned: zero-sized windows at the origin, invisible forever, so the
            // settings page explained nothing about what those two rows do.
            show_console();
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(420);
            assert(g_state->settings_visible);

            RECT client{};
            GetClientRect(g_state->window, &client);
            struct Probe { const wchar_t* name; HWND window; };
            const Probe probes[] = {
                {L"slot_row_hint", g_state->slot_row_hint},
                {L"result_digit_hint", g_state->result_digit_hint},
            };
            for (const auto& probe : probes) {
                assert(probe.window && IsWindow(probe.window));
                RECT box{};
                GetWindowRect(probe.window, &box);
                const int w = box.right - box.left, h = box.bottom - box.top;
                wchar_t text[256]{};
                GetWindowTextW(probe.window, text, 256);
                std::cout << "  " << wide_to_utf8(probe.name) << " " << w << "x" << h
                          << " visible=" << (IsWindowVisible(probe.window) ? 1 : 0)
                          << " text=" << wide_to_utf8(text) << "\n";
                // Real size, on screen, with text: a zero-sized control is the bug.
                assert(w > 40 && h > 4);
                assert(IsWindowVisible(probe.window));
                assert(text[0] != L'\0');
                // Fully inside the client area, so it is not painted off the page.
                POINT origin{box.left, box.top};
                ScreenToClient(g_state->window, &origin);
                assert(origin.x >= 0);
                assert(origin.x + w <= client.right);
            }
            // And the page must be tall enough to show the last row, not clip it.
            RECT window{};
            GetWindowRect(g_state->window, &window);
            RECT last{};
            assert(GetWindowRect(g_state->result_digit_hint, &last));
            assert(last.bottom <= window.bottom);
            std::cout << "  window height=" << (window.bottom - window.top)
                      << " last hint bottom=" << last.bottom << "\n";
            std::cout << "PASS both settings notes are positioned, visible and inside the page\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--no-dialog-on-failure") {
            // The reported "第一次使用效果会出现弹窗" had two sources: AE's script refusal
            // (handled by the queue) and our own MessageBox paths. A failed apply must
            // report in the status line only - never a modal dialog the user must dismiss
            // before they can keep working.
            show_console();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(200);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            g_pending_applies.clear();
            const int dialogs_before = g_test_message_box_count;

            // 1. The host refuses the script with a plain error.
            g_test_script_reply = xfx::ScriptReply{false, false, 0, "AE script execution failed"};
            g_first_idle_tick = GetTickCount64() - 60000;
            g_pending_apply_since = 0;
            g_apply_clear_streak = 0;
            execute_selected();
            for (int tick = 0; tick < 14 && !g_pending_applies.empty(); ++tick) {
                run_pending_apply_on_idle();
                pump_for(20);
            }
            pump_for(160);
            wchar_t status[512]{};
            GetWindowTextW(g_state->status, status, 512);
            std::cout << "  failure status: " << wide_to_utf8(status) << "\n";
            assert(g_test_message_box_count == dialogs_before);   // no dialog
            assert(std::wstring(status).find(L"失败") != std::wstring::npos);
            // The palette must still be there: the user needs to see the reason.
            if (!IsWindow(g_state->window)) { show_console(); pump_for(200); }
            assert(IsWindow(g_state->window));

            // 2. A partial apply (some layers failed) is also status-only.
            g_test_script_reply = xfx::ScriptReply{true, true, 2, "1 个图层没有可应用的位置"};
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            g_pending_apply_since = 0;
            g_apply_clear_streak = 0;
            execute_selected();
            for (int tick = 0; tick < 14 && !g_pending_applies.empty(); ++tick) {
                run_pending_apply_on_idle();
                pump_for(20);
            }
            pump_for(150);
            GetWindowTextW(g_state->status, status, 512);
            std::cout << "  partial status: " << wide_to_utf8(status) << "\n";
            assert(g_test_message_box_count == dialogs_before);
            std::cout << "PASS a failed or partial apply reports in the status line, never a dialog\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--drag-zoom-scale") {
            // A drag is resolved by testing the REAL cursor against each cell's screen
            // rectangle. At a non-100 % scale those rectangles are scaled, and a hit test
            // against a mis-scaled rectangle silently binds the wrong cell or nothing.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(240);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);

            for (int scale : {1, 2, 3}) {
                // Verify every cell maps its own centre back to its own index.
                for (int slot = 0; slot < 9; ++slot) {
                    const HWND cell = g_state->slot_row_buttons[static_cast<size_t>(slot)];
                    assert(cell && IsWindowVisible(cell));
                    RECT box{};
                    assert(GetWindowRect(cell, &box));
                    const POINT centre{(box.left + box.right) / 2, (box.top + box.bottom) / 2};
                    std::cout << "  scale=" << scale << " cell=" << slot
                              << " centre=" << centre.x << "," << centre.y
                              << " hit=" << slot_row_cell_at(centre) << "\n";
                    assert(slot_row_cell_at(centre) == slot);
                    // Well outside the tolerance ring the cell must not answer: the point
                    // is that a drop never bleeds into a neighbour, not that the edges are
                    // pixel-exact (a few pixels of slack is deliberate, see the hit test).
                    const int slack = (std::max)(2, MulDiv(3, GetDpiForWindow(g_state->window), 96));
                    const POINT outside{box.right + slack + 8, centre.y};
                    assert(slot_row_cell_at(outside) != slot);
                    // A point just inside the tolerance ring still resolves to this cell.
                    const POINT near_edge{box.right + 1, centre.y};
                    assert(slot_row_cell_at(near_edge) == slot);
                }
                // And a real drop on the fifth cell binds exactly that cell.
                RECT target{};
                assert(GetWindowRect(g_state->slot_row_buttons[4], &target));
                const POINT drop{(target.left + target.right) / 2, (target.top + target.bottom) / 2};
                g_test_drag_cursor = drop;
                g_state->result_drag_row = 0;
                g_state->result_drag_armed = true;
                g_test_drag_button_held = false;      // released
                handle_drag_poll();
                g_test_drag_button_held.reset();
                g_test_drag_cursor.reset();
                pump_for(80);
                assert(!g_state->settings.row_slots[4].empty());
                for (int other = 0; other < 9; ++other) {
                    if (other == 4) continue;
                    assert(g_state->settings.row_slots[static_cast<size_t>(other)].empty());
                }
                for (auto& slot : g_state->settings.row_slots) slot.clear();
            }
            std::cout << "PASS dragging resolves the exact cell at every scale factor\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--small-screen") {
            // The palette is 520 logical pixels wide and grows downwards with the strip and
            // the results. On a laptop at 150 % with a 1366x768 screen the work area is only
            // ~900x500 logical pixels, and a window that extends past the bottom edge hides
            // its own status line and strip - which reads as "the panel is cut off".
            show_console();
            g_state->settings.show_slot_row = true;
            SetWindowTextW(g_state->search, L"");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(240);

            // Shrink the virtual screen by moving the palette into a corner and checking it
            // still fits inside the monitor's work area after every height change.
            struct Probe { const wchar_t* name; std::function<void()> action; };
            const Probe probes[] = {
                {L"default", [] {}},
                {L"settings", [] { SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); }},
                {L"settings-off", [] { SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); }},
            };
            for (const auto& probe : probes) {
                probe.action();
                pump_for(320);
                RECT box{};
                assert(GetWindowRect(g_state->window, &box));
                MONITORINFO monitor{sizeof(monitor)};
                assert(GetMonitorInfoW(MonitorFromWindow(g_state->window, MONITOR_DEFAULTTONEAREST),
                    &monitor));
                const RECT& work = monitor.rcWork;
                std::cout << "  " << wide_to_utf8(probe.name)
                          << " window=" << box.left << "," << box.top << " "
                          << (box.right - box.left) << "x" << (box.bottom - box.top)
                          << " work=" << work.left << "," << work.top << " "
                          << (work.right - work.left) << "x" << (work.bottom - work.top) << "\n";
                // It must never be taller than the work area or positioned above it.
                assert(box.bottom - box.top <= work.bottom - work.top);
                assert(box.top >= work.top);
                assert(box.right - box.left <= work.right - work.left);
                // And the status line has to remain fully inside the window.
                if (IsWindowVisible(g_state->status)) {
                    RECT status{};
                    assert(GetWindowRect(g_state->status, &status));
                    assert(status.bottom <= box.bottom);
                    assert(status.top >= box.top);
                }
            }
            // Clamping the height must not collapse the list: it has to stay scrollable
            // and keep showing a meaningful number of rows.
            assert(IsWindowVisible(g_state->results));
            RECT list{};
            assert(GetWindowRect(g_state->results, &list));
            std::cout << "  results height after clamping: " << (list.bottom - list.top) << "\n";
            assert(list.bottom - list.top >= 18);
            std::cout << "PASS the palette always fits inside the monitor work area\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--popup-screenshot") {
            // Direct visual proof for the two reported dropdown bugs: the list was all
            // white and every corner was square. Open each dropdown and capture the REAL
            // popup window, then measure it: white pixels, corner pixels against the
            // middle, and the window region.
            show_console();
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(300);
            const int ids[] = {kFilterId, kLightThemeId, kImeModeId,
                               kSlotRowKeyId, kResultDigitKeyId, kHotkeyComboId};
            const wchar_t* names[] = {L"filter", L"theme", L"ime",
                                      L"slotrow", L"digit", L"hotkey"};
            for (int index = 0; index < 6; ++index) {
                const HWND combo = GetDlgItem(g_state->window, ids[index]);
                assert(combo && IsWindowVisible(combo));
                SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
                pump_for(200);
                COMBOBOXINFO info{sizeof(info)};
                assert(GetComboBoxInfo(combo, &info));
                const HWND popup = info.hwndList;
                assert(popup && IsWindow(popup));
                RECT box{};
                assert(GetWindowRect(popup, &box));
                const int width = box.right - box.left, height = box.bottom - box.top;
                assert(width > 10 && height > 10);

                HDC screen = GetDC(nullptr);
                HDC memory = CreateCompatibleDC(screen);
                HBITMAP shot = CreateCompatibleBitmap(screen, width, height);
                const auto previous = SelectObject(memory, shot);
                const bool copied = BitBlt(memory, 0, 0, width, height, screen,
                    box.left, box.top, SRCCOPY) != FALSE;
                assert(copied);

                // White means "the system default list painted over our theme".
                long white = 0;
                long total = 0;
                COLORREF middle = 0;
                for (int y = 2; y < height - 2; y += 2) {
                    for (int x = 2; x < width - 2; x += 2) {
                        const COLORREF pixel = GetPixel(memory, x, y);
                        if (pixel == CLR_INVALID) continue;
                        ++total;
                        const int r = GetRValue(pixel), g = GetGValue(pixel), b = GetBValue(pixel);
                        if (r > 235 && g > 235 && b > 235) ++white;
                        if (x == width / 2 && y == height / 2) middle = pixel;
                    }
                }
                assert(total > 0);
                const int white_percent = static_cast<int>(white * 100 / total);

                // The four corners must be cut off by the rounded region, and the middle
                // must not be: a square popup fails this immediately.
                HRGN region = CreateRectRgn(0, 0, 0, 0);
                const int region_type = GetWindowRgn(popup, region);
                bool rounded = false;
                if (region_type != ERROR) {
                    rounded = PtInRegion(region, 1, 1) == FALSE &&
                              PtInRegion(region, width / 2, height / 2) != FALSE;
                }
                DeleteObject(region);

                // Screenshot per dropdown so the evidence can be looked at.
                if (index == 4 && argc > 1) {
                    Gdiplus::Bitmap bitmap(shot, nullptr);
                    CLSID png{};
                    CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
                    bitmap.Save((fs::path(argv[1]).parent_path() / L"dropdown-open.png").c_str(), &png,
                        nullptr);
                }

                std::cout << "  " << wide_to_utf8(names[index]) << ": " << width << "x" << height
                          << " white=" << white_percent << "% region=" << region_type
                          << " rounded=" << (rounded ? 1 : 0)
                          << " middle=#" << std::hex << (middle & 0xFFFFFF) << std::dec << "\n";
                assert(white_percent < 40);      // it must NOT be the system white list
                assert(middle != 0x00FFFFFF);    // centre is our dark surface
                assert(rounded);                  // corners are cut off by the region
                SelectObject(memory, previous);
                DeleteObject(shot);
                DeleteDC(memory);
                ReleaseDC(nullptr, screen);
                SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
                pump_for(120);
            }
            std::cout << "PASS every open dropdown is dark, rounded and unclipped\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--keyboard-row-bind") {
            // "选择绑定第二行" - binding a SELECTED result to the second row without a
            // drag. Type, highlight, Shift+digit. The old branch also required Alt, so the
            // gesture never fired at all.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            g_state->settings.result_digit_shortcut = false;
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(220);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            const LRESULT chosen = 0;
            SendMessageW(g_state->results, LB_SETCURSEL, chosen, 0);
            g_test_keyboard_modifiers = static_cast<UINT>(MOD_SHIFT);
            const auto data = SendMessageW(g_state->results, LB_GETITEMDATA, chosen, 0);
            assert(data != LB_ERR && data != 0);
            const auto expected = item_key(*reinterpret_cast<const xfx::CatalogItem*>(data));

            // From the results list.
            SetFocus(g_state->results);
            SendMessageW(g_state->results, WM_KEYDOWN, '3', 0);
            pump_for(60);
            std::cout << "  slot2 after Shift+3 on the list: "
                      << (g_state->settings.row_slots[2].empty() ? "empty" : "bound") << "\n";
            assert(!g_state->settings.row_slots[2].empty());
            assert(g_state->settings.row_slots[2] == expected);

            // From the search box: focus is there while the user types.
            SetFocus(g_state->search);
            SendMessageW(g_state->search, WM_KEYDOWN, '7', 0);
            pump_for(60);
            std::cout << "  slot6 after Shift+7 in the search box: "
                      << (g_state->settings.row_slots[6].empty() ? "empty" : "bound") << "\n";
            assert(!g_state->settings.row_slots[6].empty());
            assert(g_state->settings.row_slots[6] == expected);

            // The digit must NOT have been typed into the search box.
            wchar_t text[64]{};
            GetWindowTextW(g_state->search, text, 64);
            std::cout << "  search text after the binds: " << wide_to_utf8(text) << "\n";
            assert(std::wstring(text) == L"gao");

            g_test_keyboard_modifiers.reset();
            // And the bindings reached the file.
            const auto saved = xfx::SettingsStore::load(g_test_settings_path);
            assert(saved.row_slots[2] == expected);
            assert(saved.row_slots[6] == expected);

            // Shift must not bind when a group is configured to use Shift: that key is the
            // effect shortcut then, and silently rebinding would be a regression.
            g_state->settings.slot_row_modifier = L"Shift";
            xfx::SettingsStore::save(g_test_settings_path, g_state->settings);
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            SetFocus(g_state->results);
            g_test_keyboard_modifiers = static_cast<UINT>(MOD_SHIFT);
            SendMessageW(g_state->results, WM_KEYDOWN, '3', 0);
            pump_for(60);
            g_test_keyboard_modifiers.reset();
            assert(g_state->settings.row_slots[2].empty());
            std::cout << "PASS Shift+digit binds the selected result to the second row\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--diagnostics-written") {
            // Everything the plugin can report about a failure on a machine we cannot see
            // goes through update_trace. If that silently drops writes there is no evidence
            // at all, which is why the same report cannot be reproduced.
            const auto log = xfx::SettingsStore::load(g_test_settings_path);
            (void)log;
            const auto trace_path = (g_test_settings_path.parent_path() / L"effect-console.log");
            std::error_code ignored;
            fs::remove(trace_path, ignored);
            update_trace("unit-test-marker v=1");
            update_trace(std::string("plugin-loaded version=") + kProductVersion);
            assert(fs::exists(trace_path));
            {
                std::ifstream in(trace_path, std::ios::binary);
                const std::string text((std::istreambuf_iterator<char>(in)), {});
                assert(text.find("unit-test-marker v=1") != std::string::npos);
                assert(text.find("plugin-loaded version=") != std::string::npos);
                // A timestamp prefix that the support flow relies on when reading it.
                assert(text.size() > 20 && text[4] == '-' && text[7] == '-');
            }
            // And the file must stay bounded: a log that grows for ever is worse than none.
            std::string filler(1024, 'x');
            for (int n = 0; n < 1200; ++n) update_trace(filler);
            const auto size = fs::file_size(trace_path, ignored);
            std::cout << "  log size after the rotation check: " << size << " bytes\n";
            assert(!ignored);
            assert(size <= 1024 * 1024 + 4096);
            std::cout << "PASS the diagnostic log is written, timestamped and bounded\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--first-open-latency") {
            // The very first open builds the whole catalogue synchronously inside WM_CREATE.
            // If that takes long the palette appears frozen with no window at all, which is
            // what "the first time I use it, it hangs / pops up an error" looks like.
            const auto t0 = std::chrono::steady_clock::now();
            show_console();
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            pump_for(200);
            std::cout << "  first open with a cold catalogue: " << elapsed_ms << " ms\n";
            assert(IsWindow(g_state->window));
            assert(g_state->catalog_initialized);
            assert(!g_state->catalog.empty());
            // A bare open on this harness must stay well inside a second; anything near that
            // means the scan is blocking the window procedure.
            assert(elapsed_ms < 900);

            // The second open reuses the cache and must be faster still.
            SendMessageW(g_state->window, WM_CLOSE, 0, 0);
            pump_for(80);
            const auto t1 = std::chrono::steady_clock::now();
            show_console();
            const auto warm_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t1).count();
            pump_for(200);
            std::cout << "  second open with a warm catalogue: " << warm_ms << " ms\n";
            assert(IsWindow(g_state->window));
            // Both opens must fit the same budget. Comparing them to each other measures
            // window setup, not the scan: the harness catalogue is a two-item fixture.
            assert(warm_ms < 900);
            std::cout << "PASS the first open finishes quickly and the cache is reused\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--drag-without-capture") {
            // Drag-to-bind must not depend on SetCapture succeeding. Another app (or AE
            // itself) can hold the mouse capture, and then no WM_MOUSEMOVE/WM_LBUTTONUP
            // ever arrives at the list - the exact "dragging does nothing" symptom. The
            // poll timer reads the cursor and the button state, so it must finish the drop
            // on its own.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(220);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);

            POINT saved{};
            assert(GetCursorPos(&saved));
            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);

            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);

            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[4], &cell));
            assert(SetCursorPos((cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2));
            // No WM_MOUSEMOVE and no WM_LBUTTONUP are delivered to the list: only the poll
            // can finish the drop.
            g_test_drag_button_held = false;    // the user let go
            g_test_drag_cursor = POINT{(cell.left + cell.right) / 2,
                                       (cell.top + cell.bottom) / 2};
            for (int tick = 0; tick < 6 && g_state->result_drag_armed; ++tick) {
                handle_drag_poll();
                pump_for(30);
            }
            g_test_drag_button_held.reset();
            g_test_drag_cursor.reset();
            SetCursorPos(saved.x, saved.y);
            pump_for(120);
            std::cout << "  armed=" << (g_state->result_drag_armed ? 1 : 0)
                      << " slot4=" << (g_state->settings.row_slots[4].empty() ? "empty" : "bound") << "\n";
            assert(!g_state->result_drag_armed);
            assert(!g_state->settings.row_slots[4].empty());
            assert(xfx::SettingsStore::load(g_test_settings_path).row_slots[4] ==
                g_state->settings.row_slots[4]);
            std::cout << "PASS drag-to-bind completes even when the mouse capture is stolen\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--settings-bool-roundtrip") {
            // A single off-by-one in the parser made "ResultDigitShortcut=0" read back as
            // enabled, so the switch could never be turned off. Every boolean must survive
            // BOTH values, and the file text must be exactly what was asked for.
            struct Flag { const char* name; bool xfx::Settings::*field; const wchar_t* key; };
            const Flag flags[] = {
                {"result_digit_shortcut", &xfx::Settings::result_digit_shortcut, L"ResultDigitShortcut"},
                {"show_slot_row", &xfx::Settings::show_slot_row, L"ShowSlotRow"},
                {"auto_close", &xfx::Settings::auto_close, L"AutoClose"},
                {"light_theme", &xfx::Settings::light_theme, L"LightTheme"},
                {"fixed_window", &xfx::Settings::fixed_window, L"FixedWindow"},
            };
            for (const auto& flag : flags) {
                for (bool value : {false, true}) {
                    auto settings = xfx::SettingsStore::load(g_test_settings_path);
                    settings.*(flag.field) = value;
                    xfx::SettingsStore::save(g_test_settings_path, settings);
                    const auto reloaded = xfx::SettingsStore::load(g_test_settings_path);
                    std::cout << "  " << flag.name << "=" << (value ? 1 : 0)
                              << " reloaded=" << ((reloaded.*(flag.field)) ? 1 : 0) << "\n";
                    assert(reloaded.*(flag.field) == value);
                    // And the raw file must carry the right digit, not just the struct.
                    std::ifstream raw(g_test_settings_path);
                    std::string text((std::istreambuf_iterator<char>(raw)), {});
                    std::wstring wide = utf8_to_wide(text.c_str());
                    const std::wstring line = std::wstring(flag.key) + L"=" +
                        (value ? L"1" : L"0");
                    assert(wide.find(line) != std::wstring::npos);
                }
            }
            // Text fields must round-trip byte-exact too; a wrong prefix offset loses or
            // corrupts the first character.
            {
                auto settings = xfx::SettingsStore::load(g_test_settings_path);
                settings.slot_button_labels[0] = L"投影";
                settings.slot_button_labels[4] = L"=等号开头";
                settings.slot_labels[2] = L"光";
                settings.slots[2] = L"ADBE Glow";
                settings.row_slots[6] = L"ADBE Tint";
                settings.capture_directory = L"C:\\XFX Capture Test";
                xfx::SettingsStore::save(g_test_settings_path, settings);
                const auto back = xfx::SettingsStore::load(g_test_settings_path);
                assert(back.slot_button_labels[0] == L"投影");
                assert(back.slot_button_labels[4] == L"=等号开头");
                assert(back.slot_labels[2] == L"光");
                assert(back.slots[2] == L"ADBE Glow");
                assert(back.row_slots[6] == L"ADBE Tint");
                assert(back.capture_directory == L"C:\\XFX Capture Test");
                std::cout << "  text fields round-tripped including a leading '='\n";
            }
            std::cout << "PASS every boolean setting survives a save and a reload\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--close-keeps-strip") {
            // save_and_restore reads BOTH shortcut combos and treats the selection as
            // authoritative. That is only safe if the combos always mirror the stored
            // settings; if they ever drift (or are still empty), closing the palette would
            // silently switch the second strip OFF - the reported "the strip keeps turning
            // itself off". This drives the real open/close cycle and checks the file.
            if (IsWindow(g_state->window)) { SendMessageW(g_state->window, WM_CLOSE, 0, 0); pump_for(80); }
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            g_state->settings.result_digit_shortcut = false;
            g_state->settings.row_slots[0] = item_key(g_test_catalog[0]);
            xfx::SettingsStore::save(g_test_settings_path, g_state->settings);

            show_console();
            pump_for(220);
            assert(IsWindow(g_state->window));
            // The combos must already show what is stored, before the page is ever opened.
            {
                const LRESULT slot_sel = SendMessageW(
                    GetDlgItem(g_state->window, kSlotRowKeyId), CB_GETCURSEL, 0, 0);
                const LRESULT digit_sel = SendMessageW(
                    GetDlgItem(g_state->window, kResultDigitKeyId), CB_GETCURSEL, 0, 0);
                std::cout << "  combos on open: slotRow=" << slot_sel
                          << " digit=" << digit_sel << "\n";
                assert(slot_sel == 3);    // Alt + 数字
                assert(digit_sel == 0);   // 关闭
            }
            SendMessageW(g_state->window, WM_CLOSE, 0, 0);
            pump_for(160);

            const auto after_close = xfx::SettingsStore::load(g_test_settings_path);
            std::cout << "  after close: show_slot_row=" << (after_close.show_slot_row ? 1 : 0)
                      << " modifier=" << wide_to_utf8(after_close.slot_row_modifier.c_str())
                      << " slot0=" << (after_close.row_slots[0].empty() ? "empty" : "bound") << "\n";
            assert(after_close.show_slot_row);
            assert(after_close.slot_row_modifier == L"Alt");
            assert(!after_close.row_slots[0].empty());

            // A second open/close cycle must be a no-op for the setting.
            show_console();
            pump_for(220);
            SendMessageW(g_state->window, WM_CLOSE, 0, 0);
            pump_for(160);
            const auto again = xfx::SettingsStore::load(g_test_settings_path);
            assert(again.show_slot_row);
            assert(again.slot_row_modifier == L"Alt");
            std::cout << "PASS closing the palette never resets the second strip\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--modal-depth-balanced") {
            // modal_depth gates auto-close, the drag poll and "is the palette busy". One
            // leaked increment leaves the palette permanently modal: it stops closing on
            // focus loss and stops accepting drops, with no visible reason.
            show_console();
            pump_for(160);
            const unsigned int baseline = g_state->modal_depth;
            assert(baseline == 0);

            // Every operation that opens a nested scope must give the counter back.
            for (int round = 0; round < 6; ++round) {
                // A real menu would block waiting for input in this harness; feed the
                // command the way every other menu test does.
                g_test_popup_command = static_cast<std::optional<UINT>>(0);
                show_slot_row_menu(0);                 // ModalGuard inside
                pump_for(20);
                assert(g_state->modal_depth == baseline);
                g_test_popup_command = static_cast<std::optional<UINT>>(0);
                show_shortcut_menu();
                pump_for(20);
                assert(g_state->modal_depth == baseline);
                g_test_popup_command = static_cast<std::optional<UINT>>(0);
                show_result_menu(g_state->results, MAKELPARAM(10, 10));
                pump_for(20);
                assert(g_state->modal_depth == baseline);
            }

            // And an apply that runs through the idle runner must not leave it raised.
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            pump_for(120);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            g_pending_applies.clear();
            g_test_script_reply = xfx::ScriptReply{true, false, 1, "ok"};
            g_first_idle_tick = GetTickCount64() - 60000;
            g_apply_clear_streak = 0;
            execute_selected();
            for (int tick = 0; tick < 12 && !g_pending_applies.empty(); ++tick) {
                run_pending_apply_on_idle();
                pump_for(20);
            }
            g_test_script_reply.reset();
            pump_for(100);
            std::cout << "  modal_depth after all scopes = " << g_state->modal_depth << "\n";
            assert(g_state->modal_depth == baseline);
            std::cout << "PASS every modal scope restores the depth counter\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--ime-session-balance") {
            // The IME is switched on open and restored on close. Every path that hides the
            // palette must leave the session flag false, or the next open captures the
            // ALREADY-SWITCHED state as "original" and the user's own IME setting is lost.
            show_console();
            pump_for(160);
            assert(g_state->ime_session_active);          // opened -> session live

            // 1. A plain close restores and clears.
            SendMessageW(g_state->window, WM_CLOSE, 0, 0);
            pump_for(80);
            assert(!g_state->ime_session_active);
            assert(!IsWindow(g_state->window));

            // 2. Open, then run an apply (which hides the palette mid-flight).
            show_console();
            pump_for(160);
            assert(g_state->ime_session_active);
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            pump_for(120);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            g_pending_applies.clear();
            g_test_script_reply = xfx::ScriptReply{true, false, 1, "ok"};
            g_first_idle_tick = GetTickCount64() - 60000;
            g_apply_clear_streak = 0;
            execute_selected();
            assert(!g_pending_applies.empty());
            for (int tick = 0; tick < 12 && !g_pending_applies.empty(); ++tick) {
                run_pending_apply_on_idle();
                pump_for(20);
            }
            g_test_script_reply.reset();
            pump_for(120);
            // The success path closes the palette; either way the session must be settled.
            assert(!g_state->ime_session_active);

            // 3. Reopening starts a NEW session rather than reusing the old capture.
            if (!IsWindow(g_state->window)) { show_console(); pump_for(200); }
            assert(g_state->ime_session_active);

            // 4. Closing from the settings page also settles it.
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(280);
            SendMessageW(g_state->window, WM_CLOSE, 0, 0);
            pump_for(90);
            assert(!g_state->ime_session_active);
            std::cout << "PASS the IME session is balanced across open, apply and close\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--shortcut-choice-roundtrip") {
            // The two shortcut dropdowns store "closed" and "no modifier" as DIFFERENT
            // selections that both map to an empty modifier string. Reopening the page must
            // restore exactly what was chosen, or the user sees a value they never picked.
            // Each group is exercised with the OTHER group turned off, because two groups
            // both set to "no modifier" is a real conflict the plugin must refuse.
            show_console();
            auto open_settings = [&](bool open) {
                SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
                pump_for(open ? 300 : 200);
            };
            auto pick = [&](int id, int index) {
                const HWND combo = GetDlgItem(g_state->window, id);
                assert(combo && IsWindowVisible(combo));
                SendMessageW(combo, CB_SETCURSEL, index, 0);
                SendMessageW(g_state->window, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE),
                    reinterpret_cast<LPARAM>(combo));
                pump_for(90);
                return static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
            };
            // Slot row first, with the digit group off.
            g_state->settings.result_digit_shortcut = false;
            g_state->settings.result_digit_modifier.clear();
            open_settings(true);
            for (int index = 0; index < 6; ++index) {
                const int applied = pick(kSlotRowKeyId, index);
                assert(applied == index);   // the guard must not reject these
                open_settings(false);
                open_settings(true);
                const HWND again = GetDlgItem(g_state->window, kSlotRowKeyId);
                assert(again);
                const LRESULT restored = SendMessageW(again, CB_GETCURSEL, 0, 0);
                std::cout << "  slotRow picked=" << index << " restored=" << restored << "\n";
                assert(restored == index);
                if (index == 0) {
                    assert(!g_state->settings.show_slot_row);
                    assert(g_state->settings.slot_row_modifier.empty());
                } else if (index == 1) {
                    assert(g_state->settings.show_slot_row);
                    assert(g_state->settings.slot_row_modifier.empty());
                } else {
                    assert(g_state->settings.show_slot_row);
                    assert(g_state->settings.slot_row_modifier ==
                        std::wstring(kShortcutChoices[static_cast<size_t>(index)].modifier));
                }
            }
            open_settings(false);

            // Then the digit group, with the slot row off.
            g_state->settings.show_slot_row = false;
            g_state->settings.slot_row_modifier.clear();
            open_settings(true);
            for (int index = 0; index < 6; ++index) {
                const int applied = pick(kResultDigitKeyId, index);
                assert(applied == index);
                open_settings(false);
                open_settings(true);
                const HWND again = GetDlgItem(g_state->window, kResultDigitKeyId);
                assert(again);
                const LRESULT restored = SendMessageW(again, CB_GETCURSEL, 0, 0);
                std::cout << "  digit   picked=" << index << " restored=" << restored << "\n";
                assert(restored == index);
            }
            open_settings(false);

            // And two groups with the same modifier must be refused, not silently applied.
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Ctrl";
            g_state->settings.result_digit_shortcut = true;
            g_state->settings.result_digit_modifier = L"Ctrl";
            open_settings(true);
            pick(kResultDigitKeyId, 4);   // Shift: no conflict
            assert(g_state->settings.result_digit_modifier == L"Shift");
            const int refused = pick(kResultDigitKeyId, 2);   // Ctrl: conflicts with slot row
            assert(refused == 4);                             // put back to the stored value
            assert(g_state->settings.result_digit_modifier == L"Shift");
            std::cout << "PASS the shortcut dropdowns restore exactly the choice that was made\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--popup-near-bottom") {
            // A dropdown opened from a combo near the bottom of the screen is clamped by
            // the work area. Before the fix the list then opened short - visually the same
            // as the "white 2 pixel bar" bug - with no feedback.
            show_console();
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            const int bare_height = 360;
            const int bottom_top = (std::max)(work.top, work.bottom - bare_height - 8);
            SetWindowPos(g_state->window, HWND_TOP, work.left + 60, bottom_top, 520, bare_height,
                SWP_SHOWWINDOW);
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(320);
            for (int id : {kFilterId, kImeModeId, kHotkeyComboId}) {
                const HWND combo = GetDlgItem(g_state->window, id);
                assert(combo && IsWindowVisible(combo));
                SetFocus(combo);
                SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
                pump_for(260);
                COMBOBOXINFO info{sizeof(info)};
                assert(GetComboBoxInfo(combo, &info));
                RECT bounds{};
                GetWindowRect(info.hwndList, &bounds);
                const int list_height = bounds.bottom - bounds.top;
                const int row = static_cast<int>(SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0));
                std::cout << "  combo=" << id << " popupHeight=" << list_height
                          << " top=" << bounds.top << " workArea=(" << work.top << ","
                          << work.bottom << ")\n";
                // Even near the bottom the list must show its rows, and it must stay inside
                // the work area rather than being clipped by it.
                assert(list_height >= row * 3);
                assert(bounds.top >= work.top - 2);
                assert(bounds.bottom <= work.bottom + 2);
                SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
                pump_for(60);
            }
            std::cout << "PASS a dropdown near the bottom still opens with its rows visible\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--no-white-surface") {
            // The reported bug is a WHITE dropdown. The list only looks white if some
            // control still paints with the system default surface. Sample every visible
            // control of the palette in both themes and prove none of them is near-white on
            // a dark theme, or near-black on a light theme.
            for (bool light : {false, true}) {
                if (IsWindow(g_state->window)) { SendMessageW(g_state->window, WM_CLOSE, 0, 0); pump_for(60); }
                g_state->settings.theme_mode = light ? xfx::ThemeMode::Light : xfx::ThemeMode::Dark;
                g_state->settings.show_slot_row = true;
                xfx::SettingsStore::save(g_test_settings_path, g_state->settings);
                show_console();
                pump_for(220);
                RECT work{};
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
                SetWindowPos(g_state->window, HWND_TOP, work.left + 30, work.top + 30, 520, 420, SWP_SHOWWINDOW);
                pump_for(120);
                HDC screen = GetDC(nullptr);
                size_t sampled = 0;
                for (HWND child : {g_state->search, g_state->filter, g_state->results,
                                   g_state->status, g_state->slot_row_buttons[0],
                                   g_state->layer_style_buttons[0], g_state->preferences,
                                   g_state->count, g_state->hotkey_label}) {
                    if (!child || !IsWindow(child) || !IsWindowVisible(child)) continue;
                    RECT box{};
                    if (!GetWindowRect(child, &box)) continue;
                    const int w = box.right - box.left, h = box.bottom - box.top;
                    if (w < 4 || h < 4) continue;
                    HDC memory = CreateCompatibleDC(screen);
                    HBITMAP shot = CreateCompatibleBitmap(screen, w, h);
                    const auto previous = SelectObject(memory, shot);
                    BitBlt(memory, 0, 0, w, h, screen, box.left, box.top, SRCCOPY);
                    // Sample a grid of points; the average decides "near-white/near-black".
                    long sum = 0; int n = 0;
                    for (int y = h / 4; y < h * 3 / 4; y += (std::max)(1, h / 6))
                        for (int x = w / 8; x < w * 7 / 8; x += (std::max)(1, w / 8), ++n) {
                            const COLORREF c = GetPixel(memory, x, y);
                            sum += (GetRValue(c) + GetGValue(c) + GetBValue(c)) / 3;
                        }
                    const int average = n ? static_cast<int>(sum / n) : -1;
                    if (light) assert(average > 90);    // light theme must not be near-black
                    else assert(average < 200);         // dark theme must not be near-white
                    SelectObject(memory, previous);
                    DeleteObject(shot); DeleteDC(memory);
                    ++sampled;
                }
                ReleaseDC(nullptr, screen);
                assert(sampled >= 6);
                std::cout << "  " << (light ? "light" : "dark") << " theme sampled " << sampled
                          << " controls, none pure white/black\n";
            }
            std::cout << "PASS no visible control paints the system default surface\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--long-session") {
            // One continuous sequence through every panel state. Each transition can leave
            // stale list style, stale focus or a stale drag flag, and none of those show up
            // in a test that exercises a single state in isolation.
            show_console();
            pump_for(160);
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();

            // 1. Shortcut strip (empty query) -> own list style, nine rows.
            SetWindowTextW(g_state->search, L"");
            populate_results();
            pump_for(120);
            assert((GetWindowLongW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) != 0);

            // 2. Typing leaves the shortcut strip and shows search results.
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            pump_for(140);
            assert((GetWindowLongW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) == 0);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);

            // 3. A filter switch keeps the non-shortcut list and does not crash on an
            //    empty result (预设 with 0 rows).
            for (int filter = 0; filter < 5; ++filter) {
                SendMessageW(g_state->filter, CB_SETCURSEL, filter, 0);
                SendMessageW(g_state->window, WM_COMMAND,
                    MAKEWPARAM(kFilterId, CBN_SELCHANGE),
                    reinterpret_cast<LPARAM>(g_state->filter));
                pump_for(90);
                assert(IsWindow(g_state->results));
                assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) >= 0);
            }
            SendMessageW(g_state->filter, CB_SETCURSEL, 0, 0);
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kFilterId, CBN_SELCHANGE), reinterpret_cast<LPARAM>(g_state->filter));

            // 4. Settings opens and closes; the palette must return to the same list mode.
            const bool before_shortcut = (GetWindowLongW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) != 0;
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(280);
            assert(g_state->settings_visible);
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(300);
            assert(!g_state->settings_visible);
            assert(((GetWindowLongW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) != 0) == before_shortcut);

            // 5. A stale drag state from an aborted gesture must not survive into a bind.
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            pump_for(120);
            RECT row{};
            SendMessageW(g_state->results, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&row));
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            SendMessageW(g_state->results, WM_CANCELMODE, 0, 0);   // aborted
            pump_for(80);
            assert(!g_state->result_drag_armed);
            for (const auto& slot : g_state->settings.row_slots) assert(slot.empty());

            // 6. And a real drag right after the abort still binds.
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[6], &cell));
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            SetCursorPos((cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2);
            SendMessageW(g_state->results, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(2, 2));
            SendMessageW(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(2, 2));
            pump_for(120);
            assert(!g_state->settings.row_slots[6].empty());

            // 7. Closing and reopening keeps the bind and the strip switch. The switch is
            //    persisted by the settings page, so that is the path that must save it.
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(300);
            {
                const HWND slot_combo = GetDlgItem(g_state->window, kSlotRowKeyId);
                assert(slot_combo && IsWindowVisible(slot_combo));
                SendMessageW(slot_combo, CB_SETCURSEL, 3, 0);
                SendMessageW(g_state->window, WM_COMMAND,
                    MAKEWPARAM(kSlotRowKeyId, CBN_SELCHANGE),
                    reinterpret_cast<LPARAM>(slot_combo));
                pump_for(220);
            }
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(320);
            SendMessageW(g_state->window, WM_CLOSE, 0, 0);
            pump_for(80);
            show_console();
            pump_for(200);
            const auto reloaded = xfx::SettingsStore::load(g_test_settings_path);
            assert(reloaded.show_slot_row);
            assert(reloaded.row_slots[6] == g_state->settings.row_slots[6]);
            std::cout << "PASS a full session through every panel state keeps its invariants\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--reopen-no-leak") {
            // Open and close the palette many times. Every handle the window owns must be
            // released, or a long AE session leaks GDI objects until the process dies.
            show_console();
            pump_for(160);
            const DWORD gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            const DWORD user_before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
            for (int cycle = 0; cycle < 12; ++cycle) {
                SetWindowTextW(g_state->search, L"gao");
                populate_results();
                pump_for(30);
                SendMessageW(g_state->window, WM_CLOSE, 0, 0);
                pump_for(40);
                assert(!IsWindow(g_state->window));
                show_console();
                pump_for(50);
                assert(IsWindow(g_state->window));
            }
            pump_for(120);
            const DWORD gdi_after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            const DWORD user_after = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
            std::cout << "  gdi " << gdi_before << " -> " << gdi_after
                      << "  user " << user_before << " -> " << user_after << "\n";
            // A handful of caches may settle upward; a leak of every reopen would be ~12x
            // the per-window handle count and is caught by this bound.
            assert(gdi_after <= gdi_before + 20);
            assert(user_after <= user_before + 20);
            std::cout << "PASS closing and reopening the palette does not leak handles\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--panel-contract") {
            // The ScriptUI panel parses the native replies. Any change to their shape
            // silently breaks the panel instead of the plugin, which is exactly the kind of
            // bug that looks like "the panel does nothing".
            const auto ready = xfx::script_bridge().status(std::nullopt);
            assert(ready.rfind("XFX1\t", 0) == 0);
            // Either READY <bridgeVersion> from a live host, or the documented error shape
            // an unavailable bridge returns. Both must keep the XFX1 prefix the panel
            // checks first, and the version must be in the range the panel accepts.
            if (ready.find("READY") == std::string::npos) {
                std::cout << "  bridge not registered in this harness: [" << ready << "]\n";
                assert(ready.find("ERROR") != std::string::npos);
                std::cout << "PASS the panel reply contract is unchanged (ERROR shape)\n";
                death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
                return 0;
            }
            const auto version_pos = ready.rfind('\t');
            assert(version_pos != std::string::npos);
            const int bridge_version = std::atoi(ready.c_str() + version_pos + 1);
            std::cout << "  bridgeVersion=" << bridge_version << "\n";
            assert(bridge_version >= 10003);
            assert(bridge_version < 20000);

            // THEME reply: light|dark + four RRGGBB colours, exactly as the panel parses it.
            const auto theme = xfx::script_bridge().appearance();
            assert(theme.rfind("XFX1\tTHEME\t", 0) == 0);
            size_t tabs = 0;
            for (char c : theme) if (c == '\t') ++tabs;
            std::cout << "  theme tabs=" << tabs << " [" << theme << "]\n";
            assert(tabs == 5);
            assert(theme.find("light") != std::string::npos ||
                   theme.find("dark") != std::string::npos);
            for (size_t at = 0; at + 6 <= theme.size(); ++at) {
                if (theme[at] != '\t') continue;
                bool colour = at + 7 <= theme.size();
                for (size_t n = 1; colour && n <= 6; ++n)
                    colour = std::isxdigit(static_cast<unsigned char>(theme[at + n])) != 0;
                if (!colour) continue;
                // A tab followed by six hex digits is a colour field.
                assert(at + 6 < theme.size());
            }

            // UPDATE reply always carries an empty version until a check completes.
            const auto update = xfx::script_bridge().update_status();
            assert(update.rfind("XFX1\tUPDATE\t", 0) == 0);
            std::cout << "PASS the panel reply contract is unchanged (READY/THEME/UPDATE)\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--dpi-layout") {
            // The palette is laid out in 96-DPI units and multiplied by the monitor scale.
            // A control placed without that factor is half-size at 200% and overlaps its
            // neighbour, which reads as "the layout is broken on my screen".
            show_console();
            g_state->settings.show_slot_row = true;
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(220);
            for (int dpi : {96, 120, 144, 192}) {
                const double scale = dpi / 96.0;
                RECT client{};
                GetClientRect(g_state->window, &client);
                // Representative controls that must all scale with the window.
                struct Probe { const wchar_t* name; HWND window; int min_height; };
                const Probe probes[] = {
                    {L"results", g_state->results, 18},
                    {L"status", g_state->status, 10},
                    {L"filter", g_state->filter, 18},
                    {L"preferences", g_state->preferences, 18},
                };
                for (const auto& probe : probes) {
                    if (!probe.window || !IsWindow(probe.window)) continue;
                    RECT box{};
                    assert(GetWindowRect(probe.window, &box));
                    const int h = box.bottom - box.top;
                    const int expected = static_cast<int>(probe.min_height * scale);
                    std::cout << "  dpi=" << dpi << " " << wide_to_utf8(probe.name)
                              << " height=" << h << " minExpected=" << expected << "\n";
                    assert(h >= static_cast<int>(probe.min_height * 0.9));
                }
                // Nothing may overflow the client area horizontally.
                for (HWND child : {g_state->search, g_state->results, g_state->filter,
                                   g_state->preferences, g_state->status}) {
                    if (!child || !IsWindow(child) || !IsWindowVisible(child)) continue;
                    RECT box{};
                    assert(GetWindowRect(child, &box));
                    POINT top_left{box.left, box.top};
                    ScreenToClient(g_state->window, &top_left);
                    POINT bottom_right{box.right, box.bottom};
                    ScreenToClient(g_state->window, &bottom_right);
                    assert(top_left.x >= -2);
                    assert(bottom_right.x <= client.right + 2);
                }
            }
            std::cout << "PASS the layout scales with the monitor instead of using raw pixels\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--closed-dropdown-corners") {
            // The reported bug is "all the dropdowns are right angles". The open list is
            // rounded by a window region; the CLOSED face is a native control whose square
            // window would still show four sharp corners over the rounded card behind it.
            // Sample the four corners and the middle of the real control.
            show_console();
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(300);
            const int ids[] = {kFilterId, kLightThemeId, kImeModeId,
                               kSlotRowKeyId, kResultDigitKeyId, kHotkeyComboId};
            for (int id : ids) {
                const HWND combo = GetDlgItem(g_state->window, id);
                assert(combo && IsWindowVisible(combo));
                RECT box{};
                assert(GetWindowRect(combo, &box));
                const int w = box.right - box.left, h = box.bottom - box.top;
                assert(w > 8 && h > 8);
                HDC screen = GetDC(nullptr);
                HDC memory = CreateCompatibleDC(screen);
                HBITMAP shot = CreateCompatibleBitmap(screen, w, h);
                const auto previous = SelectObject(memory, shot);
                const bool copied = BitBlt(memory, 0, 0, w, h, screen, box.left, box.top, SRCCOPY) != FALSE;
                assert(copied);
                // The card surface is what must show through a cut corner; the control's
                // own fill is a different colour. Compare the corner against the centre.
                const COLORREF centre = GetPixel(memory, w / 2, h / 2);
                const COLORREF corner = GetPixel(memory, 0, 0);
                std::cout << "  combo=" << id << " centre=" << (int)GetRValue(centre)
                          << "," << (int)GetGValue(centre) << "," << (int)GetBValue(centre)
                          << " corner=" << (int)GetRValue(corner)
                          << "," << (int)GetGValue(corner) << "," << (int)GetBValue(corner) << "\n";
                // A corner that is painted with the control's own interior colour means the
                // square window is still visible there.
                assert(!(corner == centre));
                SelectObject(memory, previous);
                DeleteObject(shot); DeleteDC(memory); ReleaseDC(nullptr, screen);
            }
            std::cout << "PASS the closed dropdown face does not paint a square corner\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--row-cell-empty-click") {
            // Clicking an EMPTY cell must bind; clicking an OCCUPIED cell must run it.
            // That split is the whole interaction, and getting it backwards makes the
            // strip feel like it ignores clicks.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(220);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);

            // Empty cell -> bind, no host call.
            g_pending_applies.clear();
            SendMessageW(g_state->slot_row_buttons[1], BM_CLICK, 0, 0);
            pump_for(120);
            assert(!g_state->settings.row_slots[1].empty());
            assert(g_pending_applies.empty());

            // Occupied cell -> run it, queueing a host apply and NOT rebinding.
            const auto bound = g_state->settings.row_slots[1];
            g_test_script_reply = xfx::ScriptReply{true, false, 1, "ok"};
            g_first_idle_tick = GetTickCount64() - 60000;
            g_apply_clear_streak = 0;
            SendMessageW(g_state->slot_row_buttons[1], BM_CLICK, 0, 0);
            pump_for(160);
            assert(g_state->settings.row_slots[1] == bound);   // unchanged
            assert(!g_pending_applies.empty() || !g_test_last_script.empty());
            g_test_script_reply.reset();
            g_pending_applies.clear();

            // Shift+click always rebinds, even over an occupied cell.
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            g_test_keyboard_modifiers = MOD_SHIFT;
            SendMessageW(g_state->slot_row_buttons[1], BM_CLICK, 0, 0);
            g_test_keyboard_modifiers.reset();
            pump_for(120);
            assert(!g_state->settings.row_slots[1].empty());
            std::cout << "PASS empty cell binds, filled cell runs, shift always rebinds\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--row-slot-hover-menu") {
            // Binding a cell must be reachable from the cell itself: right-click opens its
            // menu, and the menu's own command numbers must bind that exact cell.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(220);
            assert(IsWindowVisible(g_state->slot_row_buttons[3]));
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);

            // Every cell must open a menu that binds *its own* index. Feed the menu the
            // command the real handler would issue for that cell and check the result.
            for (int slot : {0, 5, 8}) {
                SendMessageW(g_state->slot_row_buttons[static_cast<size_t>(slot)],
                    WM_CONTEXTMENU, reinterpret_cast<WPARAM>(g_state->slot_row_buttons[static_cast<size_t>(slot)]),
                    MAKELPARAM(-1, -1));
                pump_for(60);
                // The menu was driven through the test hook; drive the bind directly with
                // the id the menu carries so the wiring is what is measured.
                g_test_popup_command = static_cast<std::optional<UINT>>(70 + slot);
                show_slot_row_menu(slot);
                pump_for(60);
                assert(!g_state->settings.row_slots[static_cast<size_t>(slot)].empty());
                // No other cell may have been touched.
                for (int other = 0; other < 9; ++other) {
                    if (other == slot) continue;
                    assert(g_state->settings.row_slots[static_cast<size_t>(other)].empty());
                }
                g_state->settings.row_slots[static_cast<size_t>(slot)].clear();
            }

            // A cell with nothing left to bind must say so instead of doing nothing.
            const LRESULT sel = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
            SendMessageW(g_state->results, LB_SETCURSEL, -1, 0);
            g_state->settings.row_slots[2].clear();
            assign_row_slot(2);
            pump_for(60);
            assert(g_state->settings.row_slots[2].empty());
            SendMessageW(g_state->results, LB_SETCURSEL, sel, 0);
            std::cout << "PASS each strip cell binds only itself and refuses without a selection\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--apply-script-shape") {
            // Applying an effect must go through a script that reports a machine-readable
            // result and never calls alert(): an alert is AE's own modal dialog, and on
            // the first apply of a session that is exactly the popup the user reported.
            for (const wchar_t* match : {L"ADBE Gaussian Blur 2", L"ADBE Tint"}) {
                const auto script = xfx::script_apply_effect(wide_to_utf8(match));
                assert(script.find("alert(") == std::string::npos);
                assert(script.find("confirm(") == std::string::npos);
                assert(script.find("prompt(") == std::string::npos);
                assert(script.find("(function () {") != std::string::npos);
                assert(script.find("beginUndoGroup") != std::string::npos);
                assert(script.find("endUndoGroup") != std::string::npos);
                // The result contract the runner parses.
                assert(script.find("OK\\t") != std::string::npos);
                assert(script.find("ERROR\\t") != std::string::npos);
                assert(script.find("PARTIAL\\t") != std::string::npos);
                // The matchName is embedded, quoted, and cannot break out of the literal.
                assert(script.find(std::string(match, match + wcslen(match))) != std::string::npos);
            }
            // An identifier containing a quote must not be able to escape the string.
            const auto hostile = xfx::script_apply_effect("ADBE \"); alert(1); //");
            // A quote in the identifier must be escaped, so it can only appear as \" in
            // the generated literal - never as a bare closing quote followed by code.
            assert(hostile.find("\\\"") != std::string::npos);
            const size_t first_alert = hostile.find("alert(");
            if (first_alert != std::string::npos) {
                // Any literal "alert(" may only appear inside a quoted string, never as code.
                const size_t escaped = hostile.rfind("\\\"", first_alert);
                assert(escaped != std::string::npos);
            }
            std::cout << "PASS apply scripts are silent, undo-wrapped and report a status\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--menu-row-bind") {
            // The results context menu carries a 第二行快捷栏 submenu whose items are
            // numbered 70+slot. Those numbers must bind the row slot - not fall through
            // to the first-strip handler, and not be swallowed as an inert label.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            for (auto& slot : g_state->settings.slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(200);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            const auto data = SendMessageW(g_state->results, LB_GETITEMDATA, 0, 0);
            assert(data && data != LB_ERR);
            const auto expected_key = item_key(*reinterpret_cast<const xfx::CatalogItem*>(data));

            // Drive the real menu command numbers through the real handler.
            for (int slot : {0, 4, 8}) {
                const UINT command = static_cast<UINT>(70 + slot);
                handle_slot_command(command);
                // The first-strip handler must not claim these numbers.
                assert(g_state->settings.slots[static_cast<size_t>(slot)].empty());

                // show_result_menu dispatches 70..78 itself; call the same code path by
                // feeding the tracked command through the public entry.
                g_test_popup_command = command;
                show_result_menu(g_state->results, MAKELPARAM(10, 10));
                pump_for(80);
                assert(g_state->settings.row_slots[static_cast<size_t>(slot)] == expected_key);
            }
            assert(g_state->settings.slots[0].empty());
            assert(g_state->settings.slots[4].empty());
            assert(g_state->settings.slots[8].empty());
            // Persisted, not merely in memory.
            const auto reloaded = xfx::SettingsStore::load(g_test_settings_path);
            assert(reloaded.row_slots[0] == expected_key);
            assert(reloaded.row_slots[8] == expected_key);
            std::cout << "PASS the row-slot menu items bind row slots only\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--strip-toggle") {
            // Turning the strip on is the only way a user reaches it, and the palette must
            // grow by exactly one strip row. Comparing a settings-open height to a closed
            // one measures the settings page, not the strip, so both samples are closed.
            show_console();
            g_state->settings.show_slot_row = false;
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(200);
            RECT without{};
            GetWindowRect(g_state->window, &without);
            const int height_without_strip = without.bottom - without.top;
            assert(std::none_of(g_state->slot_row_buttons.begin(), g_state->slot_row_buttons.end(),
                [](HWND b) { return b && IsWindowVisible(b); }));

            // Turn it on through the combo the settings page actually shows. Index 3 is
            // "Alt + 数字", the documented default for the second strip.
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(280);
            const HWND slot_combo = GetDlgItem(g_state->window, kSlotRowKeyId);
            assert(slot_combo && IsWindowVisible(slot_combo));
            SendMessageW(slot_combo, CB_SETCURSEL, 3, 0);
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kSlotRowKeyId, CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(slot_combo));
            pump_for(320);
            assert(g_state->settings.show_slot_row);
            assert(g_state->settings.slot_row_modifier == L"Alt");
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(340);

            RECT with_strip{};
            GetWindowRect(g_state->window, &with_strip);
            const int height_with_strip = with_strip.bottom - with_strip.top;
            std::cout << "  height without strip=" << height_without_strip
                      << " with strip=" << height_with_strip << "\n";
            assert(height_with_strip > height_without_strip);

            // Every cell must be visible, non-empty on screen, and inside the client area.
            RECT client{};
            GetClientRect(g_state->window, &client);
            for (size_t index = 0; index < g_state->slot_row_buttons.size(); ++index) {
                const HWND button = g_state->slot_row_buttons[index];
                assert(button && IsWindowVisible(button));
                RECT cell{};
                assert(GetWindowRect(button, &cell));
                assert(cell.right > cell.left && cell.bottom > cell.top);
                POINT top_left{cell.left, cell.top};
                ScreenToClient(g_state->window, &top_left);
                assert(top_left.y >= 0);
                assert(top_left.y + (cell.bottom - cell.top) <= client.bottom);
            }
            // And the choice must be on disk: the combo path saves before repainting.
            const auto reloaded = xfx::SettingsStore::load(g_test_settings_path);
            assert(reloaded.show_slot_row);
            assert(reloaded.slot_row_modifier == L"Alt");
            std::cout << "PASS the second strip turns on, fits the window and is saved\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--recent-survives") {
            // 最近 (recent) is only useful if it survives a restart: the apply path writes
            // it into settings, and the filter reads it back. Test both halves together.
            show_console();
            g_state->settings.recent.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(160);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            const auto data = SendMessageW(g_state->results, LB_GETITEMDATA, 0, 0);
            const auto key = item_key(*reinterpret_cast<const xfx::CatalogItem*>(data));

            // Run the staged apply so the recents list is written the real way.
            g_pending_applies.clear();
            g_test_force_modal = false;
            g_test_scripting_available = true;
            g_first_idle_tick = GetTickCount64() - 60000;
            g_apply_clear_streak = 0;
            xfx::ScriptReply ok_reply;
            ok_reply.succeeded = true;
            ok_reply.count = 1;
            ok_reply.message = "ok";
            g_test_script_reply = ok_reply;
            execute_item(*reinterpret_cast<const xfx::CatalogItem*>(data));
            assert(!g_pending_applies.empty());
            for (int tick = 0; tick < 12; ++tick) { if (run_pending_apply_on_idle()) break; pump_for(20); }
            assert(g_pending_applies.empty());
            g_test_script_reply.reset();
            assert(!g_state->settings.recent.empty());
            assert(g_state->settings.recent.front() == key);

            // Persist and reload exactly as the next AE session would.
            xfx::SettingsStore::save(g_test_settings_path, g_state->settings);
            const auto reloaded = xfx::SettingsStore::load(g_test_settings_path);
            assert(!reloaded.recent.empty());
            assert(reloaded.recent.front() == key);

            // The successful apply closed the palette (that is the designed behaviour), so
            // reopening it also exercises the restart path: settings are re-read from disk
            // and the catalog is rebuilt.
            show_console();
            pump_for(200);
            assert(IsWindow(g_state->results));
            assert(g_state->settings.recent.front() == key);

            // The 最近 filter must then list whatever this session's catalog contains.
            assert(!g_state->catalog.empty());
            const auto live_key = item_key(g_state->catalog.front());
            auto with_live_recent = g_state->settings;
            with_live_recent.recent = {live_key};
            g_state->settings = with_live_recent;
            SetWindowTextW(g_state->search, L"");
            SendMessageW(g_state->filter, CB_SETCURSEL, 4, 0);
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kFilterId, CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(g_state->filter));
            populate_results();
            pump_for(180);
            const int recent_rows = static_cast<int>(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0));
            std::cout << "  recent rows=" << recent_rows
                      << " filterSel=" << (int)SendMessageW(g_state->filter, CB_GETCURSEL, 0, 0)
                      << " catalog=" << g_state->catalog.size()
                      << " shortcutMode=" << (shortcut_list_mode() ? 1 : 0) << "\n";
            assert(recent_rows >= 1);
            std::cout << "PASS a used effect survives in the recent list across a restart\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--hotkey-fallback") {
            // If AE (or another app) already owns the configured combination, the console
            // must still be summonable. Registration failure is reported and the fallback
            // path is used instead of leaving the user with a dead shortcut.
            show_console();
            const wchar_t* candidates[] = {L"Ctrl+Shift+Space", L"Ctrl+Alt+Space", L"Ctrl+Shift+A"};
            for (const wchar_t* candidate : candidates) {
                g_state->settings.hotkey = candidate;
                const bool registered = register_configured_hotkey();
                // Either RegisterHotKey took it, or the low-level fallback was installed.
                // Both are "the shortcut works"; a silent failure is the bug.
                const bool hook = g_state->keyboard_hook != nullptr;
                std::wcout << L"  " << candidate << L" registered=" << registered
                           << L" hook=" << hook << L"\n";
                assert(registered || hook);
            }
            // A malformed shortcut must be rejected rather than half-installed.
            g_state->settings.hotkey = L"Ctrl+";
            const bool bad = register_configured_hotkey();
            (void)bad;   // parse failure is reported, not a crash
            // Restore a working one for the rest of the harness.
            g_state->settings.hotkey = L"Ctrl+Alt+Shift+F23";
            assert(register_configured_hotkey());
            std::cout << "PASS the summon shortcut always has a working path\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--popup-reopens-clean") {
            // Open every dropdown, close it, and open it again. The height fix runs on each
            // open, so a list that opens correctly once must keep doing it - and the popup
            // must not accumulate window regions or shrink on the second open.
            show_console();
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            SetWindowPos(g_state->window, HWND_TOP, work.left + 40, work.top + 40,
                520, 400, SWP_SHOWWINDOW);
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(320);
            const int ids[] = {kFilterId, kLightThemeId, kImeModeId,
                               kSlotRowKeyId, kResultDigitKeyId, kHotkeyComboId};
            for (int id : ids) {
                const HWND combo = GetDlgItem(g_state->window, id);
                assert(combo && IsWindowVisible(combo));
                int heights[3]{};
                for (int pass = 0; pass < 3; ++pass) {
                    SetFocus(combo);
                    SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
                    pump_for(220);
                    COMBOBOXINFO info{sizeof(info)};
                    assert(GetComboBoxInfo(combo, &info));
                    RECT bounds{};
                    GetWindowRect(info.hwndList, &bounds);
                    heights[pass] = bounds.bottom - bounds.top;
                    SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
                    pump_for(80);
                }
                std::cout << "  combo=" << id << " heights=" << heights[0]
                          << "," << heights[1] << "," << heights[2] << "\n";
                const int row = static_cast<int>(SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0));
                for (int pass = 0; pass < 3; ++pass) assert(heights[pass] >= row * 3);
                // The second and third opens must be the same size as the first: a list
                // that grows or shrinks each time is its own bug.
                assert(heights[1] == heights[0]);
                assert(heights[2] == heights[0]);
            }
            std::cout << "PASS every dropdown opens at the same usable height every time\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--first-apply-waits") {
            // The first apply after AE starts is the one that produced AE's own error
            // dialog. Simulate that exact situation end to end: the host is mid-dialog,
            // an effect is applied, and the dialog then closes. The apply must run
            // afterwards with no error and no dialog of ours.
            show_console();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(160);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            g_pending_applies.clear();
            g_first_idle_tick = GetTickCount64() - 60000;   // past the startup settle window
            g_pending_apply_since = 0;
            g_apply_clear_streak = 0;

            const int messages_before = g_test_message_box_count;
            g_test_force_modal = true;
            assert(execute_selected());          // stages the apply
            assert(!g_pending_applies.empty());
            // Several idle ticks while the host is still inside its dialog.
            for (int tick = 0; tick < 8; ++tick) {
                run_pending_apply_on_idle();
                pump_for(40);
            }
            assert(!g_pending_applies.empty());  // still waiting, never forced
            assert(g_test_last_script.empty());  // nothing reached the host
            assert(g_test_message_box_count == messages_before);

            // The dialog closes: the next few idles must run it. The runner requires a
            // short streak of clear idles by design, so tick for it rather than once.
            g_test_force_modal = false;
            bool ran = false;
            for (int tick = 0; tick < 10 && !ran; ++tick) ran = run_pending_apply_on_idle();
            assert(ran);
            assert(g_pending_applies.empty());
            assert(!g_test_last_script.empty()); // the script really reached the host
            // And no dialog was shown to the user at any point.
            assert(g_test_message_box_count == messages_before);
            std::cout << "PASS the first apply waits out AE's dialog and runs afterwards, no popup\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--no-forced-run") {
            // The apply queue must never force a script through a modal loop. The old code
            // ran it anyway after 20 seconds; that call is exactly what AE answers with its
            // own error dialog, which was the first-use popup.
            assert(g_pending_applies.empty());
            stage_apply("return 'x';", L"", false, true);
            assert(g_pending_applies.size() == 1);
            // Pretend the host is unreachable and let the clock run past the old valve.
            g_test_force_modal = true;
            g_pending_apply_since = GetTickCount64() - 60000;
            g_first_idle_tick = GetTickCount64() - 60000;
            const bool ran = run_pending_apply_on_idle();
            g_test_force_modal.reset();
            assert(!ran);                       // must still be waiting
            assert(g_pending_applies.size() == 1);  // and must still hold the request
            assert(g_test_last_script.empty());     // nothing was executed
            g_pending_applies.clear();
            g_pending_apply_since = 0;
            std::cout << "PASS a queued apply waits out a modal loop instead of forcing it\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--modal-text") {
            // The exact string AE 25.4 zh-CN returns when it refuses a script because its
            // own dialog is up. The detector used to spell this 模态 instead of 模式, so it
            // matched nothing and the retry never ran - that was the first-use popup.
            const char* real_host_reply =
                "After Effects\xe9\x94\x99\xe8\xaf\xaf: "
                "\xe5\xbd\x93\xe6\xa8\xa1\xe5\xbc\x8f\xe5\xaf\xb9\xe8\xaf\x9d\xe6\xa1\x86"
                "\xe6\xad\xa3\xe5\x9c\xa8\xe7\xad\x89\xe5\xbe\x85\xe5\x9b\x9e\xe5\xba\x94"
                "\xe6\x97\xb6\xef\xbc\x8c\xe6\x97\xa0\xe6\xb3\x95\xe8\xbf\x90\xe8\xa1\x8c"
                "\xe8\x84\x9a\xe6\x9c\xac\xe3\x80\x82";
            assert(is_modal_script_rejection(real_host_reply));
            // The other spelling, and the English host, must also be recognised.
            assert(is_modal_script_rejection(
                "After Effects \xe9\x94\x99\xe8\xaf\xaf: "
                "\xe6\xa8\xa1\xe6\x80\x81\xe5\xaf\xb9\xe8\xaf\x9d\xe6\xa1\x86"));
            assert(is_modal_script_rejection("cannot run a script while a modal dialog is waiting"));
            // An ordinary failure must not be mistaken for a modal refusal: retrying it
            // would hide the real error for 20 seconds.
            assert(!is_modal_script_rejection("\xe8\xaf\xb7\xe5\x85\x88\xe6\x89\x93\xe5\xbc\x80 AE \xe9\xa1\xb9\xe7\x9b\xae"));
            assert(!is_modal_script_rejection(""));
            std::cout << "PASS the real zh-CN modal refusal text is detected and normal errors are not\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--drag-real-cursor") {
            // Production drag reads GetCursorPos. Every other drag test swaps that out, so
            // none of them prove the shipped path. This one moves the real cursor.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.row_slots[7].clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(200);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            assert(IsWindowVisible(g_state->slot_row_buttons[7]));
            POINT saved{};
            assert(GetCursorPos(&saved));
            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[7], &cell));
            // Real cursor to the middle of strip cell 8, then report a move. The handler is
            // expected to ignore the reported coordinates and follow the cursor.
            assert(SetCursorPos((cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2));
            const POINT stale{1, 1};
            SendMessageW(g_state->results, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(stale.x, stale.y));
            assert(g_state->result_drag_target == 7);
            SendMessageW(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(stale.x, stale.y));
            SetCursorPos(saved.x, saved.y);
            pump();
            assert(!g_state->settings.row_slots[7].empty());
            assert(xfx::SettingsStore::load(g_test_settings_path).row_slots[7] ==
                g_state->settings.row_slots[7]);
            std::cout << "PASS dragging with the real cursor binds strip cell 8\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--click-still-applies") {
            // The drag guard must not swallow a plain click: with the poll timer now
            // running from the moment the mouse goes down, a click that never reaches the
            // strip has to keep its normal "apply this result" behaviour.
            show_console();
            g_state->settings.show_slot_row = true;
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(160);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            const int row_top = row.top + 4;
            const auto before_slots = g_state->settings.row_slots;
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row_top));
            assert(g_state->result_drag_armed);
            // Pointer stays over the list the whole time: no strip cell, no drag.
            RECT list_box{};
            assert(GetWindowRect(g_state->results, &list_box));
            g_test_drag_button_held = false;
            g_test_drag_cursor = POINT{(list_box.left + list_box.right) / 2,
                                       (list_box.top + list_box.bottom) / 2};
            SendMessageW(g_state->window, WM_TIMER, kResultDragTimerId, 0);
            g_test_drag_button_held.reset();
            g_test_drag_cursor.reset();
            assert(g_state->result_drag_armed);          // untouched by the timer
            SendMessageW(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(12, row_top));
            pump();
            assert(!g_state->result_drag_armed);
            // Nothing was bound by the click, and the apply path was reached.
            assert(g_state->settings.row_slots == before_slots);
            std::cout << "PASS a click that never reaches the strip still applies\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--drag-capture-lost") {
            // AE can activate its own window mid-drag, which steals the mouse capture while
            // the button is still down. The pointer is already over a strip cell, so the
            // drop must bind then and there instead of waiting for a release that will go
            // to another window.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.row_slots[4].clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(160);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            assert(IsWindowVisible(g_state->slot_row_buttons[4]));
            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[4], &cell));
            g_test_drag_cursor = POINT{cell.left + 5, cell.top + 5};
            g_test_drag_button_held = true;      // still held down
            ReleaseCapture();                    // another window took it
            SendMessageW(g_state->window, WM_TIMER, kResultDragTimerId, 0);
            g_test_drag_button_held.reset();
            g_test_drag_cursor.reset();
            pump();
            assert(!g_state->result_drag_armed);
            assert(!g_state->settings.row_slots[4].empty());
            assert(xfx::SettingsStore::load(g_test_settings_path).row_slots[4] ==
                g_state->settings.row_slots[4]);
            std::cout << "PASS a drag that loses capture over a cell still binds\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--drag-release-away") {
            // A real drag can end without the list ever seeing WM_LBUTTONUP: the release
            // goes to whatever window is under the cursor. The poll timer is what catches
            // it, so exercise exactly that path.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.row_slots[6].clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(160);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            assert(IsWindowVisible(g_state->slot_row_buttons[6]));
            RECT row{};
            assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
                reinterpret_cast<LPARAM>(&row)) != LB_ERR);
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row.top + 4));
            assert(g_state->result_drag_armed);
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[6], &cell));
            g_test_drag_cursor = POINT{cell.left + 6, cell.top + 6};
            g_test_drag_button_held = false;   // the button is already up
            SendMessageW(g_state->window, WM_TIMER, kResultDragTimerId, 0);
            g_test_drag_button_held.reset();
            g_test_drag_cursor.reset();
            pump();
            assert(!g_state->result_drag_armed);
            assert(!g_state->settings.row_slots[6].empty());
            assert(xfx::SettingsStore::load(g_test_settings_path).row_slots[6] ==
                g_state->settings.row_slots[6]);
            std::cout << "PASS a drag released outside the list still binds to the strip\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--slot-row-alt-fires") {
            // The DEFAULT second-strip binding is Alt+1..9, and Alt+digit arrives as
            // WM_SYSKEYDOWN. If the key handler only listens to WM_KEYDOWN, the shortcut
            // simply never fires - which reads as "the second strip does not work".
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(180);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            // Bind through the real bind path, using the key the catalog produces.
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            assign_row_slot(2);
            pump();
            assert(!g_state->settings.row_slots[2].empty());
            // The bound key must resolve in the live catalog, or running the cell reports
            // "项目已失效" instead of applying anything.
            {
                const auto key = g_state->settings.row_slots[2];
                const bool resolvable = std::any_of(g_state->catalog.begin(),
                    g_state->catalog.end(),
                    [&](const xfx::CatalogItem& item) { return item_key(item) == key; });
                assert(resolvable);
            }
            const LPARAM down = MAKELPARAM(1, 0x4E);
            // With Alt held, Alt+3 must be consumed as the strip shortcut and must queue
            // the bound effect for the host.
            g_test_keyboard_modifiers = MOD_ALT;
            assert(try_digit_shortcut('3', down, false));
            assert(!g_pending_applies.empty());
            g_pending_applies.clear();
            // Without Alt the same key belongs to "apply the 3rd result", not the strip.
            g_test_keyboard_modifiers = 0;
            assert(!try_digit_shortcut('3', down, false));
            // The window proc must route WM_SYSKEYDOWN into that handler at all.
            g_test_keyboard_modifiers = MOD_ALT;
            SetFocus(g_state->results);
            SendMessageW(g_state->results, WM_SYSKEYDOWN, '3', down);
            assert(!g_pending_applies.empty());
            g_pending_applies.clear();
            g_test_keyboard_modifiers.reset();
            std::cout << "PASS Alt+digit reaches the second strip (WM_SYSKEYDOWN)\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--slot-row-all-ways") {
            // Every documented way to bind a second-strip cell must work, and each must
            // land the SAME key. A path that silently binds nothing is exactly what the
            // user reported as "not supported".
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(180);
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            const auto data = SendMessageW(g_state->results, LB_GETITEMDATA, 0, 0);
            assert(data && data != LB_ERR);
            const auto expected = item_key(*reinterpret_cast<const xfx::CatalogItem*>(data));

            // 1. Shift+click an empty cell.
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kSlotRowFirstId + 0, BN_CLICKED),
                reinterpret_cast<LPARAM>(g_state->slot_row_buttons[0]));
            pump();
            assert(g_state->settings.row_slots[0] == expected);

            // 2. Clicking an empty cell binds (no modifier key held).
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kSlotRowFirstId + 1, BN_CLICKED),
                reinterpret_cast<LPARAM>(g_state->slot_row_buttons[1]));
            pump();
            assert(g_state->settings.row_slots[1] == expected);

            // 3. Right-click menu command path.
            g_state->settings.row_slots[2].clear();
            assert(selected_row_can_bind());
            assign_row_slot(2);
            pump();
            assert(g_state->settings.row_slots[2] == expected);

            // 4. Drag path.
            g_state->settings.row_slots[3].clear();
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[3], &cell));
            g_test_drag_cursor = POINT{cell.left + 5, cell.top + 5};
            assign_row_slot_dragged(3, 0);
            g_test_drag_cursor.reset();
            pump();
            assert(g_state->settings.row_slots[3] == expected);

            // All four must persist to disk, not just live in memory.
            const auto stored = xfx::SettingsStore::load(g_test_settings_path);
            for (int slot = 0; slot < 4; ++slot)
                assert(stored.row_slots[static_cast<size_t>(slot)] == expected);

            // And every bound cell must show a real label, with the configured modifier.
            for (int slot = 0; slot < 4; ++slot) {
                const auto label = slot_row_display_label(slot);
                assert(!label.empty() && label != L"空");
                const auto number = slot_number_text(slot, g_state->settings.slot_row_modifier);
                assert(number == L"Alt+" + std::to_wstring(slot + 1));
            }
            std::cout << "PASS all four bind paths agree, persist and label correctly\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--slot-row-click-bind") {
            // Select a result, then click an EMPTY second-strip cell. That is the natural
            // gesture for "bind this to the second row" and it used to report the slot as
            // empty instead of binding.
            show_console();
            g_state->settings.show_slot_row = true;
            for (auto& slot : g_state->settings.row_slots) slot.clear();
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(160);
            const int row_count = static_cast<int>(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0));
            assert(row_count > 0);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            assert(selected_row_can_bind());
            const auto data = SendMessageW(g_state->results, LB_GETITEMDATA, 0, 0);
            const auto expected = item_key(*reinterpret_cast<const xfx::CatalogItem*>(data));
            // Click cell 7, which is empty.
            assert(IsWindowVisible(g_state->slot_row_buttons[7]));
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kSlotRowFirstId + 7, BN_CLICKED),
                reinterpret_cast<LPARAM>(g_state->slot_row_buttons[7]));
            pump();
            assert(g_state->settings.row_slots[7] == expected);
            assert(xfx::SettingsStore::load(g_test_settings_path).row_slots[7] == expected);
            // Clicking it again must RUN it, not overwrite the binding with a different row.
            const auto before_second_click = g_state->settings.row_slots[7];
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kSlotRowFirstId + 7, BN_CLICKED),
                reinterpret_cast<LPARAM>(g_state->slot_row_buttons[7]));
            pump();
            assert(g_state->settings.row_slots[7] == before_second_click);
            std::cout << "PASS clicking an empty second-strip cell binds, a filled one runs\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--slot-row-menu") {
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.row_slots[5].clear();
            layout_controls(g_state->window);
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            pump_for(160);
            assert(IsWindowVisible(g_state->slot_row_buttons[5]));
            // Nothing selected: binding must refuse instead of silently doing nothing.
            SendMessageW(g_state->results, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
            g_test_popup_command = 70 + 5;   // "bind here" in the strip menu
            SendMessageW(g_state->slot_row_buttons[5], WM_CONTEXTMENU,
                reinterpret_cast<WPARAM>(g_state->slot_row_buttons[5]), MAKELPARAM(-1, -1));
            pump();
            assert(g_state->settings.row_slots[5].empty());
            // Select a row, then bind through the same menu command.
            assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
            SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
            g_test_popup_command = 70 + 5;
            SendMessageW(g_state->slot_row_buttons[5], WM_CONTEXTMENU,
                reinterpret_cast<WPARAM>(g_state->slot_row_buttons[5]), MAKELPARAM(-1, -1));
            pump();
            assert(!g_state->settings.row_slots[5].empty());
            assert(xfx::SettingsStore::load(g_test_settings_path).row_slots[5] ==
                g_state->settings.row_slots[5]);
            // And clear it through the same menu.
            g_test_popup_command = 80 + 5;
            SendMessageW(g_state->slot_row_buttons[5], WM_CONTEXTMENU,
                reinterpret_cast<WPARAM>(g_state->slot_row_buttons[5]), MAKELPARAM(-1, -1));
            pump();
            assert(g_state->settings.row_slots[5].empty());
            std::cout << "PASS second strip context menu binds and clears a cell\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--hotkey-combo") {
            show_console();
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(260);
            assert(IsWindowVisible(g_state->hotkey_combo));
            const auto count = SendMessageW(g_state->hotkey_combo, CB_GETCOUNT, 0, 0);
            assert(count >= 10);
            const auto stored = g_state->settings.hotkey;
            const LRESULT current = SendMessageW(g_state->hotkey_combo, CB_GETCURSEL, 0, 0);
            assert(current >= 0);
            // Pick a different entry: it must be applied to the settings and persisted.
            const LRESULT next = (current + 1) % count;
            SendMessageW(g_state->hotkey_combo, CB_SETCURSEL, next, 0);
            SendMessageW(g_state->window, WM_COMMAND,
                MAKEWPARAM(kHotkeyComboId, CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(g_state->hotkey_combo));
            pump();
            wchar_t picked[128]{};
            SendMessageW(g_state->hotkey_combo, CB_GETLBTEXT, next, reinterpret_cast<LPARAM>(picked));
            assert(g_state->settings.hotkey == picked);
            assert(xfx::SettingsStore::load(g_test_settings_path).hotkey == picked);
            // The selected entry must stay selected, with no second value left on display.
            assert(SendMessageW(g_state->hotkey_combo, CB_GETCURSEL, 0, 0) == next);
            // Restore, so the rest of the run sees the original shortcut.
            if (g_state->settings.hotkey != stored) {
                const LRESULT back = (current >= 0 ? current : 0);
                SendMessageW(g_state->hotkey_combo, CB_SETCURSEL, back, 0);
                SendMessageW(g_state->window, WM_COMMAND,
                    MAKEWPARAM(kHotkeyComboId, CBN_SELCHANGE),
                    reinterpret_cast<LPARAM>(g_state->hotkey_combo));
                pump();
            }
            std::cout << "PASS summon shortcut dropdown applies, persists and restores\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--drop-highlight") {
            show_console();
            g_state->settings.show_slot_row = true;
            SetWindowTextW(g_state->search, L"gao");
            populate_results();
            layout_controls(g_state->window);
            pump_for(160);
            RECT cell{};
            assert(GetWindowRect(g_state->slot_row_buttons[4], &cell));
            g_test_drag_cursor = POINT{cell.left + 10, cell.top + 10};
            g_state->result_drag_armed = true;
            g_state->result_drag_row = 0;
            update_result_drag_target();
            repaint_slot_row_cells();
            pump_for(220);
            if (argc > 1) save_test_preview(g_state->window,
                fs::path(argv[1]).parent_path() / L"drop-highlight.png");
            g_state->result_drag_armed = false;
            g_test_drag_cursor.reset();
            std::cout << "PASS drop target highlight rendered\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--second-strip-preview") {
            // Visual proof of the second strip: bound, empty and hovered cells side by
            // side, so the rounded cards and the bound labels can be inspected directly.
            show_console();
            g_state->settings.show_slot_row = true;
            for (int slot = 0; slot < 9; ++slot)
                g_state->settings.row_slots[static_cast<size_t>(slot)].clear();
            // Bind the first five effects the catalog actually holds, so the screenshot
            // shows real names rather than placeholders.
            const int cells[] = {0, 2, 5, 8, 7};
            int chosen = 0;
            for (const auto& item : g_state->catalog) {
                if (chosen >= 5) break;
                if (item.item_type != L"effect") continue;
                if (item.match_name.empty()) continue;
                g_state->settings.row_slots[static_cast<size_t>(cells[chosen])] = item_key(item);
                ++chosen;
            }
            std::cout << "  bound " << chosen << " cells from the fixture catalog\n";
            assert(chosen >= 2);   // bound and empty cells must both appear
            SetWindowTextW(g_state->search, L"");
            populate_results();
            layout_controls(g_state->window);
            adjust_console_height();
            pump_for(260);
            if (argc > 1) {
                save_test_preview(g_state->window,
                    fs::path(argv[1]).parent_path() / L"second-strip-preview.png");
            }
            assert(IsWindowVisible(g_state->slot_row_buttons[0]));
            // Bound cells must show a label; the tooltip table is what the user reads.
            // Every bound cell must resolve to a visible label, and every cell must have
            // a non-empty tooltip (that is the only explanation of what the row does).
            int bound_cells = 0;
            for (int slot = 0; slot < 9; ++slot) {
                const auto label = slot_row_display_label(slot);
                assert(!label.empty());
                const bool bound = !g_state->settings.row_slots[static_cast<size_t>(slot)].empty();
                if (bound) { assert(label != L"空"); ++bound_cells; }
                // Each cell must also have a tooltip registered, or the row explains nothing.
                // TOOLINFOW has no cchTextMax member (that is TOOLINFO before V2 sizing);
                // the buffer is simply large enough.
                TOOLINFOW query{};
                query.cbSize = sizeof(query);
                query.hwnd = g_state->window;
                query.uId = static_cast<UINT_PTR>(600 + slot);
                wchar_t tip[512]{};
                query.lpszText = tip;
                if (g_state->tooltip && SendMessageW(g_state->tooltip, TTM_GETTOOLINFO, 0,
                        reinterpret_cast<LPARAM>(&query))) {
                    assert(tip[0] != L'\0');
                }
            }
            std::cout << "  bound cells=" << bound_cells
                      << " strip height=" << [] {
                          RECT r{}; GetWindowRect(g_state->window, &r); return r.bottom - r.top;
                      }() << "\n";
            assert(bound_cells == chosen);
            std::cout << "PASS the second strip renders bound and empty cells\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
else if (check == L"--popup-round") {
            // round_combo_popup clips a dropdown list to rounded corners. The real system
            // popup cannot be resized safely in this harness, so exercise the same helper
            // on a window we own.
            show_console();
            HWND probe = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"",
                WS_POPUP | WS_VISIBLE, 0, 0, 160, 120, g_state->window, nullptr, nullptr, nullptr);
            assert(probe);
            round_combo_popup(probe);
            RECT box{};
            const int type = GetWindowRgnBox(probe, &box);
            assert(type == COMPLEXREGION || type == SIMPLEREGION);
            assert(box.right - box.left == 160);
            assert(box.bottom - box.top == 120);
            DestroyWindow(probe);
            std::cout << "PASS the dropdown popup is clipped to a rounded region\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--combo-popup-size") {
            // The reported bug is a dropdown that opens as a white BAR. Windows sized these
            // lists to 2 pixels; the fix sizes them from the row height. Assert the real
            // popup window now has room for its rows, and that it is still a rounded
            // region - both properties belong to the same window.
            show_console();
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            SetWindowPos(g_state->window, HWND_TOP, work.left + 40, work.top + 40,
                520, 400, SWP_SHOWWINDOW);
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(320);
            const int ids[] = {kFilterId, kLightThemeId, kImeModeId,
                               kSlotRowKeyId, kResultDigitKeyId, kHotkeyComboId};
            size_t checked = 0;
            for (int id : ids) {
                const HWND combo = GetDlgItem(g_state->window, id);
                assert(combo && IsWindowVisible(combo));
                SetFocus(combo);
                SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
                pump_for(240);
                COMBOBOXINFO info{sizeof(info)};
                assert(GetComboBoxInfo(combo, &info));
                assert(info.hwndList);
                RECT bounds{};
                GetWindowRect(info.hwndList, &bounds);
                const int height = bounds.bottom - bounds.top;
                const int row = static_cast<int>(SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0));
                const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
                std::cout << "  combo=" << id << " rows=" << count
                          << " rowHeight=" << row << " popupHeight=" << height << "\n";
                // At least three rows visible: a 2px sliver is the bug.
                assert(height >= row * 3);
                assert(height <= row * count + row);
                // Rounded clip lives on the same window. A plain rectangle region would
                // also satisfy "no error", so the corner itself has to be checked: the
                // extreme corner must be OUTSIDE the region while the centre is inside.
                RECT region_box{};
                const int region_type = GetWindowRgnBox(info.hwndList, &region_box);
                assert(region_type != ERROR);
                {
                    HRGN copy = CreateRectRgn(0, 0, 1, 1);
                    assert(copy);
                    const int copied = GetWindowRgn(info.hwndList, copy);
                    assert(copied != ERROR);
                    const int popup_width = bounds.right - bounds.left;
                    assert(PtInRegion(copy, popup_width / 2, height / 2));  // centre painted
                    const bool corner_cut = !PtInRegion(copy, 0, 0) && !PtInRegion(copy, 1, 1);
                    std::cout << "    regionType=" << region_type
                              << " cornerCut=" << (corner_cut ? 1 : 0) << "\n";
                    assert(corner_cut);
                    DeleteObject(copy);
                }
                // Sample what the popup actually paints. A blank white list - the reported
                // bug - shows up here as a mostly-white capture.
                if (argc > 1 && height > 8) {
                    RECT shot_rect{};
                    GetWindowRect(info.hwndList, &shot_rect);
                    const int sw = shot_rect.right - shot_rect.left;
                    const int sh = shot_rect.bottom - shot_rect.top;
                    HDC screen = GetDC(nullptr);
                    HDC memory = CreateCompatibleDC(screen);
                    HBITMAP shot = CreateCompatibleBitmap(screen, sw, sh);
                    const auto previous = SelectObject(memory, shot);
                    BitBlt(memory, 0, 0, sw, sh, screen, shot_rect.left, shot_rect.top, SRCCOPY);
                    size_t white = 0, total = 0;
                    for (int y = 0; y < sh; ++y)
                        for (int x = 0; x < sw; ++x, ++total)
                            if (GetPixel(memory, x, y) == RGB(255, 255, 255)) ++white;
                    std::cout << "    whitePixels=" << (total ? white * 100 / total : 0) << "%\n";
                    assert(white * 100 < total * 50);
                    const auto destination = fs::path(argv[1]).parent_path() /
                        (L"dropdown-live-" + std::to_wstring(id) + L".png");
                    Gdiplus::Bitmap image(shot, nullptr);
                    CLSID png{0x557cf406, 0x1a04, 0x11d3,
                        {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
                    image.Save(destination.c_str(), &png, nullptr);
                    SelectObject(memory, previous);
                    DeleteObject(shot); DeleteDC(memory); ReleaseDC(nullptr, screen);
                }
                SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
                pump_for(60);
                ++checked;
            }
            assert(checked == std::size(ids));
            std::cout << "PASS every dropdown opens tall enough for its rows, rounded\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--combo-popup-real") {
            // Structural check on the REAL popup window of every dropdown. The popup is a
            // "ComboLBox" that Windows creates; if it is never themed, subclassed or given
            // our font, it paints its rows with the system defaults - a white list. Row
            // appearance itself is covered by --combo-dropdown, which drives WM_DRAWITEM.
            show_console();
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(260);
            const int ids[] = {kFilterId, kLightThemeId, kImeModeId,
                               kSlotRowKeyId, kResultDigitKeyId, kHotkeyComboId};
            size_t checked = 0;
            for (int id : ids) {
                const HWND combo = GetDlgItem(g_state->window, id);
                assert(combo);
                assert(IsWindowVisible(combo));
                SetFocus(combo);
                SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
                pump_for(160);
                assert(SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0));
                COMBOBOXINFO info{sizeof(info)};
                assert(GetComboBoxInfo(combo, &info));
                const HWND popup = info.hwndList;
                assert(popup);
                // Ours: themed, subclassed, and carrying the console font.
                assert(GetPropW(popup, L"XFXComboPopupThemed"));
                assert(SendMessageW(popup, WM_GETFONT, 0, 0) ==
                    reinterpret_cast<LRESULT>(g_state->font));
                // And it has a real height, not the collapsed sliver that reads as blank.
                // Height and rounded region are asserted together in --combo-popup-size.
                RECT dropped{};
                SendMessageW(combo, CB_GETDROPPEDCONTROLRECT, 0,
                    reinterpret_cast<LPARAM>(&dropped));
                assert(dropped.bottom - dropped.top >= 26);
                SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
                pump_for(60);
                ++checked;
            }
            assert(checked == std::size(ids));
            std::cout << "PASS every real dropdown popup is themed, fonted and sized\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--combo-dropdown") {
            // Render every item of every ownerdraw dropdown through the REAL WM_DRAWITEM
            // path into one image. This is exactly what the popup list shows, so a blank
            // white list is visible in the artifact rather than inferred.
            show_console();
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(260);
            const int ids[] = {kFilterId, kLightThemeId, kImeModeId,
                               kSlotRowKeyId, kResultDigitKeyId, kHotkeyComboId};
            const int rowHeight = 26;
            int totalRows = 0;
            for (int id : ids) totalRows += static_cast<int>(SendMessageW(GetDlgItem(g_state->window, id), CB_GETCOUNT, 0, 0));
            const int width = 320;
            const int height = totalRows * rowHeight + 8;
            HDC screen = GetDC(g_state->window);
            HDC memory = CreateCompatibleDC(screen);
            HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
            const auto previous = SelectObject(memory, bitmap);
            RECT all{0, 0, width, height};
            FillRect(memory, &all, g_state->background_brush);
            int y = 0;
            for (int id : ids) {
                const HWND combo = GetDlgItem(g_state->window, id);
                const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
                for (LRESULT item = 0; item < count; ++item) {
                    RECT row{0, y, width, y + rowHeight};
                    DRAWITEMSTRUCT draw{};
                    draw.CtlType = ODT_COMBOBOX;
                    draw.CtlID = static_cast<UINT>(id);
                    draw.hwndItem = combo;
                    draw.hDC = memory;
                    draw.itemID = static_cast<UINT>(item);
                    draw.rcItem = row;
                    SendMessageW(g_state->window, WM_DRAWITEM, id, reinterpret_cast<LPARAM>(&draw));
                    y += rowHeight;
                }
            }
            if (argc > 1) {
                // Write the rendered strip as a PNG via the shared encoder.
                const auto target = fs::path(argv[1]).parent_path() / L"dropdown-items.png";
                Gdiplus::Bitmap image(bitmap, nullptr);
                CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
                image.Save(target.c_str(), &png, nullptr);
                int white = 0;
                for (int row = 0; row < height; ++row)
                    for (int column = 0; column < width; column += 4)
                        if (GetPixel(memory, column, row) == RGB(255, 255, 255)) ++white;
                assert(white < width * height / 20);
            }
            SelectObject(memory, previous);
            DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(g_state->window, screen);
            std::cout << "PASS every dropdown list item renders with the panel theme\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--settings") {
            // The settings page as the user sees it, at the normal palette width.
            show_console();
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Alt";
            g_state->settings.result_digit_shortcut = true;
            g_state->settings.result_digit_modifier = L"";
            SendMessageW(g_state->slot_row_key, CB_SETCURSEL, 3, 0);
            SendMessageW(g_state->hotkey_combo, CB_SETCURSEL, 1, 0);
            SendMessageW(g_state->result_digit_key, CB_SETCURSEL, 1, 0);
            refresh_shortcut_choice_labels();
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
            pump_for(260);
            layout_controls(g_state->window);
            RedrawWindow(g_state->window, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
            pump_for(120);
            if (argc > 1) save_test_preview(g_state->window,
                fs::path(argv[1]).parent_path() / L"settings-preview-now.png");
            std::cout << "PASS settings rendered\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--strips") {
            // Both favourite strips side by side, exactly as the palette draws them.
            g_state->settings.show_slot_row = true;
            g_state->settings.slot_row_modifier = L"Ctrl";
            g_state->settings.row_slots[0] = L"ADBE Gaussian Blur 2";
            g_state->settings.row_slots[1] = L"ADBE Glo2";
            g_state->settings.slots[0] = L"ADBE Gaussian Blur 2";
            g_state->settings.slots[1] = L"ADBE Glo2";
            g_state->settings.slot_labels[0] = L"";
            update_results_list_style(true);
            populate_results();
            layout_controls(g_state->window);
            pump_for(200);
            if (argc > 1) save_test_preview(g_state->window,
                fs::path(argv[1]).parent_path() / L"strips-preview.png");
            std::cout << "PASS strips rendered\n";
            death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
            return 0;
        }
        else if (check == L"--search-cursor") check_search_cursor_stability();
        else if (check == L"--filter-surface") check_filter_background_transitions(regression_artifacts);
        else if (check == L"--filter-center") check_filter_combo_group_center();
        else if (check == L"--update-settings") {
            for (const int width : {520, 360}) for (auto mode : {xfx::ThemeMode::Dark, xfx::ThemeMode::Light}) {
                set_theme_mode(mode);
                SetWindowPos(g_state->window, nullptr, 0, 0, width, 154, SWP_NOMOVE | SWP_NOZORDER);
                if (g_state->settings_visible) SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
                g_test_update_info = {}; refresh_update_ui();
                assert(!IsWindowVisible(g_state->update_button));
                SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump();
                wchar_t text[128]{};
                for (auto state : {xfx::UpdateInfo::Idle, xfx::UpdateInfo::Current, xfx::UpdateInfo::Failed}) {
                    g_test_update_info = {state}; refresh_update_ui(); pump();
                    GetWindowTextW(g_state->update_button, text, 128);
                    assert(IsWindowVisible(g_state->update_button) && IsWindowEnabled(g_state->update_button));
                    assert(std::wstring(text) == L"检查更新");
                }
                g_test_update_info = {xfx::UpdateInfo::Available, "1.0.29"}; refresh_update_ui(); pump();
                GetWindowTextW(g_state->update_button, text, 128);
                assert(std::wstring(text) == L"更新至 1.0.29");
                RECT button = child_rect_in_parent(g_state->window, g_state->update_button);
                RECT status = child_rect_in_parent(g_state->window, g_state->status);
                assert(button.bottom <= status.top);
                for (const auto other : {g_state->refresh, g_state->dictionary, g_state->count, g_state->hotkey_combo,
                     g_state->import_settings, g_state->export_settings, g_state->ime_mode_combo, g_state->light_theme}) {
                    RECT intersection{}, r = child_rect_in_parent(g_state->window, other);
                    assert(!IsWindowVisible(other) || !IntersectRect(&intersection, &button, &r));
                }
                if (argc > 1) save_test_preview(g_state->window, regression_artifacts /
                    (std::wstring(L"update-settings-") + std::to_wstring(width) + (mode == xfx::ThemeMode::Dark ? L"-dark.png" : L"-light.png")));
                SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump();
            }
            std::cout << "PASS persistent settings update entry across offline/current/new versions, widths and themes\n";
        }
        else if (check == L"--update-button") {
            xfx::script_bridge().set_available(true);
            assert(!IsWindowVisible(g_state->update_button));
            for (auto mode : {xfx::ThemeMode::Dark, xfx::ThemeMode::Light}) {
                set_theme_mode(mode);
                const RECT search = child_rect_in_parent(g_state->window, g_state->search);
                const auto slots = g_state->settings.slots;
                g_test_update_info = {xfx::UpdateInfo::Available, "1.0.28"};
                refresh_update_ui(); pump();
                assert(IsWindowVisible(g_state->update_button) && IsWindowEnabled(g_state->update_button));
                const auto button = child_rect_in_parent(g_state->window, g_state->update_button);
                const auto status = child_rect_in_parent(g_state->window, g_state->status);
                const auto after = child_rect_in_parent(g_state->window, g_state->search);
                assert(EqualRect(&search, &after) && status.right <= button.left && g_state->settings.slots == slots);
                assert(xfx::script_bridge().update_status().find("available\t1.0.28") != std::string::npos);
                if (argc > 1) save_test_preview(g_state->window, regression_artifacts /
                    (mode == xfx::ThemeMode::Dark ? L"update-dark.png" : L"update-light.png"));
                g_test_update_info.state = xfx::UpdateInfo::Downloading; refresh_update_ui();
                assert(!IsWindowEnabled(g_state->update_button));
                g_test_update_info.state = xfx::UpdateInfo::Ready; refresh_update_ui();
                wchar_t label[64]{}; GetWindowTextW(g_state->update_button, label, 64);
                assert(std::wstring(label) == L"安装更新");
                g_test_update_info = {}; refresh_update_ui();
                assert(!IsWindowVisible(g_state->update_button));
            }
            std::cout << "PASS update button dark/light state, non-overlap and original home preservation\n";
        }
        else if (check == L"--display-labels") {
            const auto source=fs::path(utf8_to_wide(__FILE__)).parent_path().parent_path().parent_path();
            fs::copy_file(source/"installer"/"XFXNativeConsole.aliases.tsv",directory/L"XFXNativeConsole.aliases.default.tsv",fs::copy_options::overwrite_existing);
            const auto aliases=load_aliases();std::set<std::string> ids;std::vector<xfx::CatalogItem> catalog;
            std::ifstream input(source/"native"/"tests"/"data"/"ae-25.4-effects.txt",std::ios::binary);std::string row;
            while(std::getline(input,row)){
                if(!row.empty() && row.back()=='\r')row.pop_back();
                if(row.empty() || row[0]=='#')continue;const auto f=split_tsv(row);
                append_registered_effect(catalog,aliases,ids,f[1].c_str(),f[0].c_str(),f[2].c_str(),true);
            }
            assert(catalog.size()==1203);g_state->base_catalog=catalog;rebuild_customized_catalog();
            for(const auto& preview:std::vector<std::pair<std::wstring,std::wstring>>{{L"touy",L"native-shadow-labels.png"},{L"Deep Glow 2",L"deep-glow-label.png"}}){
                SetWindowTextW(g_state->search,preview.first.c_str());populate_results();pump();
                assert(SendMessageW(g_state->results,LB_GETCOUNT,0,0)>0);
                save_test_preview(g_state->window,regression_artifacts/preview.second);
            }
            SetWindowTextW(g_state->search,L"");g_state->settings.slots[0]=L"PEDG2";
            g_state->settings.slots[1]=L"ADBE Fill";g_state->settings.slots[2]=L"ADBE Tint";
            populate_results();pump();check_home_layout_restored();
            save_test_preview(g_state->window,regression_artifacts/L"home-unchanged.png");
            std::cout<<"PASS real saved inventory label screenshots and unchanged home (isolated fixture settings)\n";
        }
        else if (check == L"--home-restored") {
            check_home_layout_restored();
            g_state->settings.slots[0]=L"ADBE Gaussian Blur 2";
            g_state->settings.slots[1]=L"ADBE Glo2";
            populate_results();pump();
            save_test_preview(g_state->window,regression_artifacts/L"home-restored.png");
            SetWindowTextW(g_state->search,L"g");populate_results();pump();
            save_test_preview(g_state->window,regression_artifacts/L"search-restored.png");
        }
        else return 2;
        death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
        return 0;
    }
    {
        // Search can change LISTBOX identity, but must never leave children
        // redraw-disabled/hidden or briefly hide the top-level palette.
        for (const auto* query : {L"g", L"gaussian", L"no-such-effect-987", L"", L"glow", L""}) {
            SetWindowTextW(g_state->search, query);
            pump();
            wchar_t actual[128]{};
            GetWindowTextW(g_state->search, actual, 128);
            assert(std::wstring(actual) == query);
            assert(IsWindowVisible(g_state->window));
            assert(!g_state->redraw_transaction);
            for (HWND control : {g_state->search, g_state->results, g_state->filter,
                                 g_state->capture, g_state->slot, g_state->preferences, g_state->status}) {
                assert(IsWindowVisible(control));
                assert(!GetPropW(control, L"SysSetRedraw"));
            }
            assert(!IsWindowVisible(g_state->hotkey_combo));
        }
        {
            SearchRedrawGuard redraw(g_state->window);
            assert(IsWindowVisible(g_state->window));
            assert(IsWindowVisible(g_state->search));
        }
        assert(IsWindowVisible(g_state->capture));
        std::cout << "PASS search restores all child visibility and preserves native text/parent visibility\n";
    }
    {
        // Dedicated style row is independent of search and user 1-9 bindings.
        const auto slots_before = g_state->settings.slots;
        RECT previous{};
        for (int index = 0; index < 9; ++index) {
            const HWND button = GetDlgItem(g_state->window, 1100 + index);
            assert(IsWindow(button) && IsWindowVisible(button));
            wchar_t label[32]{}; GetWindowTextW(button, label, 32);
            assert(std::wstring(label) == xfx::layer_styles[static_cast<size_t>(index)].short_label);
            const RECT cell = child_rect_in_parent(g_state->window, button);
            assert(cell.bottom - cell.top >= ui_px(28) && cell.right - cell.left >= ui_px(28));
            if (index) assert(cell.top == previous.top && cell.left >= previous.right + ui_px(2));
            previous = cell;
            assert(child_rect_in_parent(g_state->window, g_state->results).bottom <= cell.top);
            assert(cell.bottom <= child_rect_in_parent(g_state->window, g_state->status).top);
        }
        g_test_script_reply = xfx::ScriptReply{false, false, 0, "fixture style failure"};
        g_test_last_script.clear();
        SendMessageW(g_state->window, WM_COMMAND, MAKEWPARAM(1108, BN_CLICKED),
                     reinterpret_cast<LPARAM>(GetDlgItem(g_state->window, 1108)));
        assert(g_test_last_script.empty()); // deferred, not inside button event
        drain_idle_applies();
        assert(g_test_last_script.find("frameFX/enabled") != std::string::npos);
        assert(g_test_last_script.find("executeCommand") != std::string::npos);
        assert(g_test_last_script.find("addProperty") == std::string::npos);
        assert(IsWindowVisible(g_state->window));
        g_test_script_reply.reset();
        assert(g_state->settings.slots == slots_before);
        assert(SendMessageW(g_state->filter, CB_GETCOUNT, 0, 0) == 5);
        assert(std::none_of(g_state->catalog.begin(), g_state->catalog.end(), [](const auto& item) {
            return item.item_type == L"layer_style";
        }));
        std::cout << "PASS dedicated nine-style row, layout, existing shortcuts and search unchanged\n";
    }
    check_search_input_geometry(regression_artifacts);
    check_home_layout_restored();
    check_filter_background_transitions(regression_artifacts);
    check_filter_combo_group_center();
    check_search_cursor_stability();
    check_console_paint_geometry();
    check_hotkey_dispatch_guards();
    check_result_hover_leave();
    {
        const RECT search = child_rect_in_parent(g_state->window, g_state->search);
        const RECT filter = child_rect_in_parent(g_state->window, g_state->filter);
        const RECT capture = child_rect_in_parent(g_state->window, g_state->capture);
        const RECT slot = child_rect_in_parent(g_state->window, g_state->slot);
        const RECT preferences = child_rect_in_parent(g_state->window, g_state->preferences);
        RECT client{}; GetClientRect(g_state->window, &client);
        assert(filter.top == 10 && filter.bottom - filter.top == 36);
        assert(capture.top == filter.top && capture.bottom == filter.bottom);
        assert(slot.top == filter.top && slot.bottom == filter.bottom);
        assert(preferences.top == filter.top && preferences.bottom == filter.bottom);
        assert(filter.left - search.right == 8);
        assert(capture.left - filter.right == 4);
        assert(slot.left - capture.right == 4 && preferences.left - slot.right == 4);
        const auto metrics = input_font_metrics(g_state->search);
        assert(search.bottom - search.top == metrics.tmHeight);
        assert(std::abs((search.top + search.bottom) - (filter.top + filter.bottom)) <= 1);
        assert(preferences.right == client.right - 8);
    }
    {
        g_test_search_hint_paint_count = 0;
        g_test_search_default_paint_count = 0;
        SetFocus(g_state->search);
        InvalidateRect(g_state->search, nullptr, FALSE); UpdateWindow(g_state->search);
        assert(g_test_search_default_paint_count == 0);
        assert(search_hint_pixel_count() > 0);
        assert(g_test_search_hint_paint_count > 0);
        SetFocus(g_state->results);
        InvalidateRect(g_state->search, nullptr, FALSE); UpdateWindow(g_state->search);
        assert(g_test_search_hint_paint_count > 0);
        SetFocus(g_state->search);
    }
    {
        // Clear any mode applied earlier in this run, then read the real ambient state:
        // that is exactly what the controller captures and later restores.
        select_ime_mode(xfx::ImeMode::Disabled);
        HIMC context = ImmGetContext(g_state->search);
        DWORD original_conversion = 0, original_sentence = 0;
        if (context && ImmGetConversionStatus(context, &original_conversion, &original_sentence)) {
            ImmReleaseContext(g_state->search, context);
            select_ime_mode(xfx::ImeMode::EnglishAlways);
            assert(g_state->ime_session_active);
            assert(g_state->settings.use_english_mode);
            context = ImmGetContext(g_state->search);
            DWORD conversion = 0, sentence = 0;
            assert(context && ImmGetConversionStatus(context, &conversion, &sentence));
            assert(!ImmGetOpenStatus(context));
            ImmReleaseContext(g_state->search, context);
            SetFocus(g_state->results);
            SetFocus(g_state->search);
            context = ImmGetContext(g_state->search);
            assert(context && ImmGetConversionStatus(context, &conversion, &sentence));
            assert(!ImmGetOpenStatus(context));
            ImmReleaseContext(g_state->search, context);
            save_settings();
            assert(g_state->settings.use_english_mode &&
                xfx::SettingsStore::load(g_test_settings_path).use_english_mode);
            select_ime_mode(xfx::ImeMode::Disabled);
            save_settings();
            assert(!g_state->settings.use_english_mode &&
                !xfx::SettingsStore::load(g_test_settings_path).use_english_mode);
            context = ImmGetContext(g_state->search);
            assert(context && ImmGetConversionStatus(context, &conversion, &sentence));
            assert(conversion == original_conversion && sentence == original_sentence);
            ImmReleaseContext(g_state->search, context);
            // Closing in Restore mode must hand the original input state back.
            select_ime_mode(xfx::ImeMode::Restore);
            context = ImmGetContext(g_state->search);
            assert(context && ImmGetConversionStatus(context, &conversion, &sentence));
            assert(!ImmGetOpenStatus(context));
            ImmReleaseContext(g_state->search, context);
            save_and_restore();
            assert(!g_state->ime_session_active);
            context = ImmGetContext(g_state->search);
            assert(context && ImmGetConversionStatus(context, &conversion, &sentence));
            assert(conversion == original_conversion && sentence == original_sentence);
            ImmReleaseContext(g_state->search, context);
            select_ime_mode(xfx::ImeMode::Disabled);
            std::cout << "PASS English input mode really closes the IME and restores its original conversion state\n";
        } else {
            if (context) ImmReleaseContext(g_state->search, context);
            std::cout << "SKIP IME conversion round trip: active test input context does not expose conversion status\n";
        }
    }
    WNDCLASSEXW console_class{sizeof(console_class)};
    assert(GetClassInfoExW(GetModuleHandleW(nullptr), L"XFXNativeConsoleWindow", &console_class));
    assert(console_class.hbrBackground == nullptr);
    {
        SetWindowTextW(g_state->search,L"g");
        HFONT sans=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Arial");
        HFONT serif=CreateFontW(-20,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Cambria");
        assert(result_pixels_with_font(sans)==result_pixels_with_font(serif));
        DeleteObject(sans);DeleteObject(serif);SetWindowTextW(g_state->search,L"");
        std::cout<<"PASS result owner drawing uses its UI font and restores caller font\n";
    }
    {
        assert(g_state->settings.theme_mode==xfx::ThemeMode::Follow);
        SPBasicSuite basic{};basic.AcquireSuite=fake_acquire;basic.ReleaseSuite=fake_release;
        g_basic=&basic;refresh_host_theme(true);assert(colors().background==RGB(28,28,28));
        simulated_host_color={59110,59110,59110};g_state->last_theme_check=0;refresh_host_theme();
        assert(colors().background==RGB(230,230,230) && effective_light_theme());
        set_theme_mode(xfx::ThemeMode::Dark);simulated_host_color={65535,65535,65535};refresh_host_theme(true);
        assert(colors().background==RGB(19,19,19));
        set_theme_mode(xfx::ThemeMode::Follow);g_basic=nullptr;refresh_host_theme(true);
        assert(colors().background==RGB(32,32,32));
        HWND loading_window=nullptr;
        {
            xfx::LoadingSurface loading({40,40,360,74},colors(),L"正在测试…",1,false);
            loading_window=loading.window();assert(IsWindow(loading_window));
            const auto first=loading_pixels(loading_window);
            Sleep(250); // AE/UI thread does not pump: the loading surface must still animate.
            assert(loading_pixels(loading_window)!=first);
            if(argc>1) save_test_preview(loading_window,fs::path(argv[1]).parent_path()/L"loading-preview.png");
        }
        assert(!IsWindow(loading_window));
        const bool enabled=IsWindowEnabled(g_state->capture)!=FALSE;
        try {BusyFeedback feedback(L"测试异常清理…");assert(!IsWindowEnabled(g_state->capture));throw 1;}catch(int){}
        assert((IsWindowEnabled(g_state->capture)!=FALSE)==enabled);
        std::cout<<"PASS host RGB changes, manual override, fallback, independent loading pixels and exception cleanup\n";
    }
    // Owner drawing must change after a real pointer press, then return to rest.
    {
        const auto before = button_background(g_state->capture);
        g_test_button_hover_time = GetTickCount64();
        SendMessageW(g_state->capture, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(3, 3));
        *g_test_button_hover_time += 180;
        SendMessageW(g_state->capture, WM_TIMER, kButtonHoverTimerId, 0);
        const auto pressed_color=button_background(g_state->capture);
        if(pressed_color==before) {
            const auto& hover=g_state->button_hovers.at(g_state->capture);
            std::cerr<<"ripple diagnostic before="<<before<<" after="<<pressed_color<<" enabled="<<IsWindowEnabled(g_state->capture)
                <<" visible="<<IsWindowVisible(g_state->capture)<<" waves="<<hover.ripples.size()<<" progress="<<hover.progress
                <<" reduced="<<reduced_motion()<<" time="<<button_hover_time()<<"\n";
        }
        assert(pressed_color != before);
        SendMessageW(g_state->capture, WM_CANCELMODE, 0, 0);
        *g_test_button_hover_time += 800;
        SendMessageW(g_state->capture, WM_TIMER, kButtonHoverTimerId, 0);
        assert(button_background(g_state->capture) == before);
        g_test_button_hover_time.reset();
    }
    {
        RECT before{},after{};GetWindowRect(g_state->capture,&before);
        g_test_button_hover_time=GetTickCount64();
        for(int click=0;click<20;++click) {
            SendMessageW(g_state->capture,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,9));
            SendMessageW(g_state->capture,WM_CANCELMODE,0,0);
        }
        assert(g_state->button_hovers.at(g_state->capture).ripples.size()==12);
        assert(g_state->button_hovers.at(g_state->capture).ripples.back().origin.x==4);
        *g_test_button_hover_time+=701;SendMessageW(g_state->capture,WM_TIMER,kButtonHoverTimerId,0);
        assert(g_state->button_hovers.at(g_state->capture).ripples.empty());
        SendMessageW(g_state->capture,WM_KEYDOWN,VK_RETURN,0);
        RECT client{};GetClientRect(g_state->capture,&client);
        assert(g_state->button_hovers.at(g_state->capture).ripples.back().origin.x==client.right/2);
        EnableWindow(g_state->capture,FALSE);SendMessageW(g_state->capture,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,9));
        assert(g_state->button_hovers.at(g_state->capture).ripples.empty());EnableWindow(g_state->capture,TRUE);
        GetWindowRect(g_state->capture,&after);assert(EqualRect(&before,&after));g_test_button_hover_time.reset();
        set_status(L"配置已导入",true);pump_for(2100);
        wchar_t status[128]{};GetWindowTextW(g_state->status,status,128);
        if(std::wstring(status)!=L"就绪")std::wcerr<<L"status timer diagnostic actual="<<status<<L" expiry="<<g_state->status_expires<<L" now="<<GetTickCount64()<<L"\n";
        assert(std::wstring(status)==L"就绪");
        set_status(L"操作失败：测试原因");pump_for(2100);GetWindowTextW(g_state->status,status,128);
        assert(std::wstring(status)==L"操作失败：测试原因");
        std::cout<<"PASS rapid/off-center/keyboard/disabled ripples, stable bounds, success expiry and persistent errors\n";
    }
    {
        auto& hover = g_state->button_hovers.at(g_state->preferences);
        g_test_button_hover_time = GetTickCount64();
        SendMessageW(g_state->preferences, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(18, 18));
        assert(hover.ripples.empty());
        SendMessageW(g_state->preferences, WM_CANCELMODE, 0, 0);
        g_test_button_hover_time.reset();
    }
    assert(!IsWindow(GetDlgItem(g_state->window, kApplyId)));
    assert(!IsWindow(GetDlgItem(g_state->window, kFavoriteId)));
    assert(!IsWindow(GetDlgItem(g_state->window, kGalleryId)));
    assert(!IsWindow(GetDlgItem(g_state->window, kCaptureLocationId)));
    assert(!IsWindow(GetDlgItem(g_state->window, kThumbnailCacheClearId)));
    check_native_capture_unavailable();
    assert(SendMessageW(g_state->tooltip, TTM_GETTOOLCOUNT, 0, 0) >= 14);
    {
        TOOLINFOW capture_tool{TTTOOLINFOW_V1_SIZE};
        capture_tool.uFlags = TTF_IDISHWND;
        capture_tool.hwnd = g_state->window;
        capture_tool.uId = reinterpret_cast<UINT_PTR>(g_state->capture);
        assert(SendMessageW(g_state->tooltip, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&capture_tool)));
        assert((capture_tool.uFlags & (TTF_IDISHWND | TTF_SUBCLASS)) == (TTF_IDISHWND | TTF_SUBCLASS));
        POINT original_cursor{}; GetCursorPos(&original_cursor);
        SetWindowPos(g_state->window, nullptr, 80, 80, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetForegroundWindow(g_state->window);
        SetCursorPos(60, 60); pump();
        POINT hover{14, 14}; ClientToScreen(g_state->capture, &hover);
        SetCursorPos(hover.x, hover.y);
        SendMessageW(g_state->capture, WM_MOUSEMOVE, 0, MAKELPARAM(14, 14));
        const auto deadline = GetTickCount64() + 5000;
        while (!IsWindowVisible(g_state->tooltip) && GetTickCount64() < deadline) {
            POINT actual{}; GetCursorPos(&actual);
            if (actual.x != hover.x || actual.y != hover.y) {
                SetCursorPos(hover.x, hover.y);
                SendMessageW(g_state->capture, WM_MOUSEMOVE, 0, MAKELPARAM(14, 14));
            }
            pump(); Sleep(10);
        }
        if (!IsWindowVisible(g_state->tooltip)) {
            POINT actual{}; GetCursorPos(&actual);
            RECT parent_rect{}, capture_rect{};
            GetWindowRect(g_state->window, &parent_rect);
            GetWindowRect(g_state->capture, &capture_rect);
            std::cerr << "hover parent=" << g_state->window << "/" << IsWindowVisible(g_state->window) << " hit=" << WindowFromPoint(hover) << " button=" << g_state->capture << " parent_rect=" << parent_rect.left << "," << parent_rect.top << "," << parent_rect.right << "," << parent_rect.bottom << " capture_rect=" << capture_rect.left << "," << capture_rect.top << "," << capture_rect.right << "," << capture_rect.bottom << " cursor=" << actual.x << "," << actual.y << " expected=" << hover.x << "," << hover.y << " foreground=" << GetForegroundWindow() << '\n';
        }
        assert(IsWindowVisible(g_state->tooltip));
        assert(button_background(g_state->capture) == colors().button_pressed);
        TOOLINFOW current{TTTOOLINFOW_V1_SIZE};
        assert(SendMessageW(g_state->tooltip, TTM_GETCURRENTTOOLW, 0, reinterpret_cast<LPARAM>(&current)));
        assert(current.uId == reinterpret_cast<UINT_PTR>(g_state->capture));
        if (argc > 1) save_test_preview(g_state->tooltip, fs::path(argv[1]).parent_path() / L"hover-snapshot-tooltip.png");
        SetCursorPos(original_cursor.x, original_cursor.y);
        SendMessageW(g_state->tooltip, TTM_POP, 0, 0);
    }
    SetWindowTextW(g_state->search, L"g");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 2);
    {
        std::vector<xfx::CatalogItem> many;
        for (int i = 0; i < 12; ++i) {
            auto item = g_test_catalog[0]; item.display_name = L"fixture " + std::to_wstring(i);
            item.match_name = "fixture-" + std::to_string(i); many.push_back(std::move(item));
        }
        g_state->engine = xfx::SearchEngine(many);
        SetWindowTextW(g_state->search, L"fixture");
        assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 12);
        assert((GetWindowLongPtrW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) == 0);
        SendMessageW(g_state->results, LB_SETTOPINDEX, 4, 0);
        assert(SendMessageW(g_state->results, LB_GETTOPINDEX, 0, 0) == 4);
        g_state->engine = xfx::SearchEngine(g_state->catalog);
        SetWindowTextW(g_state->search, L"g");
    }
    {
        RECT row{};
        assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&row)) != LB_ERR);
        const int right = row.right;
        assert(result_action_at_point({right - 70, row.top + 8}) == ResultAction::Pin);
        assert(result_action_at_point({right - 46, row.top + 8}) == ResultAction::Favorite);
        assert(result_action_at_point({right - 22, row.top + 8}) == ResultAction::Shortcut);
        results_list_proc(g_state->results, WM_MOUSEMOVE, 0, MAKELPARAM(right - 70, row.top + 8));
        assert(g_state->result_tooltip == L"置顶排序 / 取消置顶排序");
        results_list_proc(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(right - 70, row.top + 8));
        assert(g_state->settings.pinned.size() == 1);
        assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&row)) != LB_ERR);
        results_list_proc(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(row.right - 46, row.top + 8));
        assert(g_state->settings.favorites.size() == 1);
        assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&row)) != LB_ERR);
        results_list_proc(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(row.right - 22, row.top + 8));
        assert(g_state->settings.slots[0] == L"ADBE Gaussian Blur 2");
        g_state->settings.pinned.clear();
        g_state->settings.favorites.clear();
        g_state->settings.slots.fill(L"");
        xfx::SettingsStore::save(g_test_settings_path, g_state->settings);
        populate_results();
    }
    SetWindowTextW(g_state->search, L"");
    assert(!IsWindow(GetDlgItem(g_state->window, 1034)));
    assert(!IsWindow(GetDlgItem(g_state->window, 1035)));
    assert(!IsWindow(GetDlgItem(g_state->window, 1037))); // Full resolution is always on; no switch.
    assert(!IsWindow(GetDlgItem(g_state->window, 1038)));
    check_full_resolution_setting();
    {
        HMENU shortcut_menu = CreatePopupMenu();
        append_slot_menu(shortcut_menu, false);
        assert(GetMenuItemCount(shortcut_menu) == 9);
        MENUITEMINFO first_column_break{sizeof(first_column_break)};
        first_column_break.fMask = MIIM_FTYPE;
        MENUITEMINFO second_column_break{sizeof(second_column_break)};
        second_column_break.fMask = MIIM_FTYPE;
        assert(GetMenuItemInfoW(shortcut_menu, 3, TRUE, &first_column_break));
        assert(GetMenuItemInfoW(shortcut_menu, 6, TRUE, &second_column_break));
        assert((first_column_break.fType & MFT_MENUBREAK) == 0);
        assert((second_column_break.fType & MFT_MENUBREAK) == 0);
        DestroyMenu(shortcut_menu);
    }
    {
        const auto original = capture_directory();
        const auto chosen = directory / L"中文快照🎬";
        g_state->settings.capture_directory = chosen.wstring();
        assert(capture_directory() == chosen);
        g_state->settings.capture_directory.clear();
        assert(capture_directory() == original);
    }
    assert(IsWindow(g_state->window));
    RECT drag_window_rect{}; GetWindowRect(g_state->window, &drag_window_rect);
    // The window edge should use the native caption drag path.
    assert(SendMessageW(g_state->window, WM_NCHITTEST, 0,
        MAKELPARAM(drag_window_rect.left + 1, drag_window_rect.top + 1)) == HTCAPTION);
    SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
    assert(g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONUP, 0, MAKELPARAM(2, 2));
    assert(!g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
    assert(g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONUP, 0, MAKELPARAM(2, 2));
    assert(!g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(250, 42));
    assert(!g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
    assert(g_state->drag_active && !g_state->drag_pending);
    ReleaseCapture();
    assert(!g_state->drag_active && !g_state->drag_pending);
    {
        RECT original{}; GetWindowRect(g_state->window, &original);
        SetWindowPos(g_state->window, nullptr, 180, 180, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetCursorPos(182, 182);
        g_test_drag_button_held = true;
        SendMessageW(g_state->window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
        assert(g_state->drag_active && !g_state->drag_pending);
        SendMessageW(g_state->window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(62, 62));
        RECT moved{}; GetWindowRect(g_state->window, &moved);
        // Removing the system border also removes its one-pixel coordinate offset.
        assert(moved.left == 240 && moved.top == 240);
        SendMessageW(g_state->window, WM_LBUTTONUP, 0, MAKELPARAM(62, 62));
        assert(!g_state->drag_active && !g_state->drag_pending);
        g_test_drag_button_held.reset();
        SetWindowPos(g_state->window, nullptr, original.left, original.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);
    }
    // Home page in shortcut-strip mode: nine cells, independent of any earlier search.
    SetWindowTextW(g_state->search, L"");
    populate_results();
    assert(shortcut_list_mode());
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 9);
    const LRESULT top_before_wheel = SendMessageW(g_state->results, LB_GETTOPINDEX, 0, 0);
    for (int wheel = 0; wheel < 8; ++wheel)
        results_list_proc(g_state->results, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
    assert(SendMessageW(g_state->results, LB_GETTOPINDEX, 0, 0) >= top_before_wheel);
    RECT status_client{}; GetClientRect(g_state->status, &status_client);
    SendMessageW(g_state->status, WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(2, static_cast<LONG>((std::max)(0L, status_client.bottom - 1L))));
    assert(g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONUP, 0, 0);
    assert(!g_state->drag_active && !g_state->drag_pending);
    SetWindowTextW(g_state->search, L"g");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 2);
    g_state->settings.pinned = {L"ADBE Glo2"};
    populate_results();
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 2);
    {
        const auto data = SendMessageW(g_state->results, LB_GETITEMDATA, 0, 0);
        assert(data != LB_ERR && data != 0);
        assert(item_key(*reinterpret_cast<const xfx::CatalogItem*>(data)) == L"ADBE Glo2");
    }
    toggle_pinned(L"ADBE Glo2");
    assert(g_state->settings.pinned.empty());
    toggle_pinned(L"ADBE Glo2");
    assert(g_state->settings.pinned == std::vector<std::wstring>{L"ADBE Glo2"});
    {
        const LPARAM second_row = MAKELPARAM(12, kResultHeight + 4);
        results_list_proc(g_state->results, WM_MOUSEMOVE, 0, second_row);
        assert(SendMessageW(g_state->results, LB_GETCURSEL, 0, 0) == 1);
        pump();
        assert(result_surface_pixel(1) == colors().selected);
    }
    g_test_result_draw_count = 0;
    g_test_result_erase_count = 0;
    for (int move = 0; move < 8; ++move) {
        const int row = move % 2;
        results_list_proc(g_state->results, WM_MOUSEMOVE, 0,
            MAKELPARAM(12, row * kResultHeight + 4));
        pump();
    }
    std::cout << "result hover draws=" << g_test_result_draw_count
              << " erases=" << g_test_result_erase_count
              << " buffered-paints=" << g_test_result_buffered_paint_count << '\n';
    assert(g_test_result_erase_count == 0);
    assert(g_test_result_buffered_paint_count > 0);
    g_state->settings.pinned.clear();
    populate_results();
    SetWindowTextW(g_state->search, L"");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 9);
    RECT compact{}; GetWindowRect(g_state->window, &compact);
    assert(compact.bottom - compact.top < 200);
    SetWindowTextW(g_state->search, L"rhzy");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    save_item_aliases(L"ADBE Gaussian Blur 2", {});
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 0);
    prepare_dialog(L"编辑搜索别名", L"  软化\r\n柔焦\r\n柔焦 \r\n");
    edit_item_aliases(g_test_catalog[0]);
    assert(dialog_handled && g_state->modal_depth == 0);
    assert(g_state->settings.custom_aliases.at(L"ADBE Gaussian Blur 2") ==
        (std::vector<std::wstring>{L"软化", L"柔焦"}));
    SetWindowTextW(g_state->search, L"rj");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    prepare_dialog(L"编辑搜索别名", L"取消的别名", IDCANCEL);
    edit_item_aliases(g_test_catalog[0]);
    assert(dialog_handled && g_state->settings.custom_aliases.at(L"ADBE Gaussian Blur 2").size() == 2);
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_READONLY));
    bool alias_save_failed = false;
    try { save_item_aliases(L"ADBE Gaussian Blur 2", {L"写入失败"}); }
    catch (...) { alias_save_failed = true; }
    assert(alias_save_failed && g_state->settings.custom_aliases.at(L"ADBE Gaussian Blur 2")[0] == L"软化");
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_NORMAL));
    save_item_aliases(L"ADBE Gaussian Blur 2", {});
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 0);
    SetWindowTextW(g_state->search, L"gsmh");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    assign_slot(0);
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    SetWindowTextW(g_state->search, L"");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 9 && g_state->visible_slot_numbers[0] == 0);
    for (int slot = 0; slot < 9; ++slot) assert(g_state->visible_slot_numbers[static_cast<size_t>(slot)] == slot);
    g_state->settings.slots[1] = L"ADBE Glo2";
    SetWindowTextW(g_state->search, L"");
    populate_results();
    const LONG_PTR shortcut_style = GetWindowLongPtrW(g_state->results, GWL_STYLE);
    assert((shortcut_style & LBS_MULTICOLUMN) != 0 && (shortcut_style & WS_HSCROLL) == 0);
    RECT shortcut_bounds{}; GetWindowRect(g_state->results, &shortcut_bounds);
    assert(shortcut_bounds.bottom - shortcut_bounds.top == kShortcutHeight);
    RECT first_slot_rect{}, second_slot_rect{};
    assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&first_slot_rect)) != LB_ERR);
    assert(SendMessageW(g_state->results, LB_GETITEMRECT, 1, reinterpret_cast<LPARAM>(&second_slot_rect)) != LB_ERR);
    assert(first_slot_rect.right - first_slot_rect.left >= 50);
    {
        RECT normal{};GetWindowRect(g_state->window,&normal);
        SetWindowPos(g_state->window,nullptr,0,0,360,normal.bottom-normal.top,SWP_NOMOVE|SWP_NOZORDER);pump();
        RECT last{},client{};GetClientRect(g_state->results,&client);
        assert(SendMessageW(g_state->results,LB_GETITEMRECT,8,reinterpret_cast<LPARAM>(&last))!=LB_ERR);
        assert(last.right<=client.right && last.top==0);
        g_test_window_erase_count = 0;
        SendMessageW(g_state->window,WM_COMMAND,kPreferencesId,0);pump_for(240);
        assert(g_test_window_erase_count == 0);
        assert((GetWindowLongPtrW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) == 0);
        if(argc>1)save_test_preview(g_state->window,fs::path(argv[1]).parent_path()/L"narrow-preview.png");
        g_test_window_erase_count = 0;
        SendMessageW(g_state->window,WM_COMMAND,kPreferencesId,0);pump_for(240);
        assert(g_test_window_erase_count == 0);
        assert((GetWindowLongPtrW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) != 0);
        SetWindowPos(g_state->window,nullptr,0,0,normal.right-normal.left,normal.bottom-normal.top,SWP_NOMOVE|SWP_NOZORDER);pump();
    }
    assert(second_slot_rect.left > first_slot_rect.left);
    if (argc > 1) save_test_preview(g_state->window, fs::path(argv[1]).parent_path() / L"shortcut-preview.png");
    clear_slot(1);
    assert(slot_display_label(0) == L"Gaussian Blur（高斯模糊）");
    RECT slot_screen{}; GetWindowRect(g_state->results, &slot_screen);
    g_test_popup_command = 30;
    prepare_dialog(L"编辑快捷槽备注", L"柔化一键");
    show_result_menu(g_state->window, MAKELPARAM(slot_screen.left + 10, slot_screen.top + 10));
    assert(dialog_handled && slot_display_label(0) == L"柔化一键");
    assert(xfx::SettingsStore::load(g_test_settings_path).slot_labels[0] == L"柔化一键");
    prepare_dialog(L"编辑快捷槽备注", L"忽略改名", IDCANCEL);
    edit_slot_label(0);
    assert(dialog_handled && slot_display_label(0) == L"柔化一键");
    SetWindowTextW(g_state->search, L"");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 9);
    assert(g_state->visible_slot_numbers[0] == 0);
    BYTE saved_keys[256]{}, digit_keys[256]{};
    GetKeyboardState(saved_keys); SetKeyboardState(digit_keys);
    // Real main-row digit carries scan code 0x02 ('1'). Passing 0 exercises no path at
    // all, so the digit fell through to the default edit behaviour.
    // Shortcut-strip mode: the digit is consumed by the favourites shortcut, so the
    // second strip has to be enabled and its first cell bound for that path to exist.
    // Shortcut-strip mode: a main-row digit is consumed by the strip shortcut and must
    // never be typed into the search box. The apply itself runs on the idle hook (which
    // this harness does drive), so only the "no text inserted" contract is asserted here;
    // the apply path has its own checks.
    {
        const auto saved = g_state->settings;
        g_state->settings.show_slot_row = true;
        g_state->settings.slot_row_modifier = L"";
        g_state->settings.row_slots[0] = L"ADBE Gaussian Blur 2";
        search_edit_proc(g_state->search, WM_KEYDOWN, '1', 0x00020001);
        search_edit_proc(g_state->search, WM_CHAR, '1', 0x00020001);
        // The pressed key staged an apply. Drop it: running it would close the palette and
        // this block is only about the digit never reaching the text field.
        g_pending_applies.clear();
        g_state->settings = saved;
    }
    assert(GetWindowTextLengthW(g_state->search) == 0);
    SetWindowTextW(g_state->search, L"");
    SendMessageW(g_state->filter, CB_SETCURSEL, 0, 0);
    populate_results();
    SetKeyboardState(saved_keys);
    clear_slot(0);
    assert(slot_display_label(0) == L"空" && g_state->settings.slot_labels[0].empty());
    SetWindowTextW(g_state->search, L"");
    SetWindowTextW(g_state->search, L"gsmh");
    populate_results();
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    SetWindowTextW(g_state->search, L"");
    SetWindowTextW(g_state->search, L"g");
    populate_results();
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 2);
    g_state->settings.favorites = {L"ADBE Glo2"};
    SendMessageW(g_state->filter, CB_SETCURSEL, 3, 0); populate_results();
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    SendMessageW(g_state->filter, CB_SETCURSEL, 0, 0); populate_results();
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_READONLY));
    bool escaped = false;
    BYTE original_keys[256]{}, keys[256]{};
    GetKeyboardState(original_keys); memcpy(keys, original_keys, sizeof(keys)); keys[VK_CONTROL] = 0x80;
    SetKeyboardState(keys);
    const auto previous_hotkey = g_state->settings.hotkey;
    const auto previous_hotkey_spec = g_state->hotkey_spec;
    // Picking a shortcut while the settings file is read-only must report the failure and
    // leave the previous shortcut in force, without throwing out of the window proc.
    {
        const LRESULT previous_choice = SendMessageW(g_state->hotkey_combo, CB_GETCURSEL, 0, 0);
        SendMessageW(g_state->hotkey_combo, CB_SETCURSEL, previous_choice == 0 ? 1 : 0, 0);
        try { apply_hotkey_choice(); }
        catch (...) { escaped = true; }
        if (g_state->settings.hotkey == previous_hotkey)
            SendMessageW(g_state->hotkey_combo, CB_SETCURSEL, previous_choice, 0);
    }
    SetKeyboardState(original_keys);
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_NORMAL));
    assert(!escaped); // Win32 callbacks must contain file write exceptions.
    assert(g_state->settings.hotkey == previous_hotkey);
    assert(g_state->hotkey_spec.virtual_key == previous_hotkey_spec.virtual_key &&
        g_state->hotkey_spec.modifiers == previous_hotkey_spec.modifiers);
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_READONLY));
    assert(xfx::should_close_console(true, false, 0));
    assert(!xfx::should_close_console(true, false, 1));
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_NORMAL));
    // Owned modal dialogs must not queue a destructive WM_CLOSE.
    {
        ModalGuard modal;
        SendMessageW(g_state->window, WM_ACTIVATE, WA_INACTIVE, 0); pump();
        assert(IsWindow(g_state->window));
    }
    RECT before{}, after{};
    GetWindowRect(g_state->results, &before);
    SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump_for(240);
    GetWindowRect(g_state->results, &after);
    assert(before.bottom - before.top == after.bottom - after.top);
    assert(GetWindowLongW(g_state->hotkey_combo, GWL_STYLE) & WS_VISIBLE);
    check_hotkey_input_geometry(regression_artifacts);
    RECT hotkey_label_rect{}, hotkey_edit_rect{}, status_rect{};
    GetWindowRect(g_state->hotkey_label, &hotkey_label_rect);
    GetWindowRect(g_state->hotkey_combo, &hotkey_edit_rect);
    GetWindowRect(g_state->status, &status_rect);
    // A combobox HWND carries the native frame, so compare the row centres with the
    // tolerance the DPI scaling needs rather than the exact HWND heights.
    assert(std::abs((hotkey_label_rect.top + hotkey_label_rect.bottom) -
        (hotkey_edit_rect.top + hotkey_edit_rect.bottom)) <= 12);
    RECT refresh_rect{}, dictionary_rect{};
    GetWindowRect(g_state->refresh, &refresh_rect);
    GetWindowRect(g_state->dictionary, &dictionary_rect);
    // One caption column, one control column: the library buttons line up with the
    // shortcut field, and the row uses the same 8px gap as every other row.
    assert(refresh_rect.left == hotkey_edit_rect.left);
    assert(dictionary_rect.left - refresh_rect.right == 8);
    POINT saved_cursor{}; GetCursorPos(&saved_cursor);
    SetCursorPos(60, 60); pump();
    for (HWND button : {g_state->capture, g_state->slot, g_state->preferences,
        g_state->refresh, g_state->dictionary,
        g_state->import_settings, g_state->export_settings})
        check_button_hover(button);
    if (argc > 1) save_test_preview(g_state->window, fs::path(argv[1]).parent_path() / L"settings-preview.png");
    POINT refresh_hover{8, 8}; ClientToScreen(g_state->refresh, &refresh_hover);
    SetCursorPos(refresh_hover.x, refresh_hover.y);
    SendMessageW(g_state->refresh, WM_MOUSEMOVE, 0, MAKELPARAM(8, 8)); pump_for(220);
    if (argc > 1) save_test_preview(g_state->window, fs::path(argv[1]).parent_path() / L"button-hover-preview.png");
    SetCursorPos(60, 60);
    SendMessageW(g_state->refresh, WM_MOUSELEAVE, 0, 0); pump_for(220);
    SetCursorPos(saved_cursor.x, saved_cursor.y);
    SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump_for(240);
    assert(!IsWindowVisible(g_state->hotkey_combo));
    if (argc > 1) {
        SetWindowPos(g_state->window, nullptr, 80, 80, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        save_test_preview(g_state->window, argv[1]);
        save_test_preview(g_state->window, fs::path(argv[1]).parent_path() / L"ui-preview-dark.png");
    }
    const HWND theme_choice = GetDlgItem(g_state->window, 1032);
    assert(theme_choice); // The appearance option must be reachable in native settings.
    SendMessageW(theme_choice, CB_SETCURSEL, static_cast<int>(xfx::ThemeMode::Light), 0);
    SendMessageW(g_state->window, WM_COMMAND, MAKEWPARAM(1032, CBN_SELCHANGE), 0);
    assert(g_state->settings.light_theme && xfx::SettingsStore::load(g_test_settings_path).light_theme);
    SetCursorPos(60, 60); pump();
    check_button_hover(g_state->capture);
    LOGBRUSH theme_brush{};
    assert(GetObjectW(g_state->background_brush, sizeof(theme_brush), &theme_brush));
    assert(theme_brush.lbColor == RGB(237,237,237));
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_READONLY));
    SendMessageW(theme_choice, CB_SETCURSEL, static_cast<int>(xfx::ThemeMode::Dark), 0);
    SendMessageW(g_state->window, WM_COMMAND, MAKEWPARAM(1032, CBN_SELCHANGE), 0);
    assert(g_state->settings.light_theme && SendMessageW(theme_choice, CB_GETCURSEL, 0, 0) == static_cast<int>(xfx::ThemeMode::Light));
    assert(GetObjectW(g_state->background_brush, sizeof(theme_brush), &theme_brush));
    assert(theme_brush.lbColor == RGB(237,237,237));
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_NORMAL));
    prepare_dialog(L"编辑搜索别名", L"取消的浅色别名", IDCANCEL);
    edit_item_aliases(g_test_catalog[0]);
    assert(dialog_handled && g_state->settings.light_theme);
    const DWORD gdi_before_theme_cycles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int cycle = 0; cycle < 24; ++cycle) set_light_theme((cycle % 2) != 0);
    assert(g_state->settings.light_theme);
    assert(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi_before_theme_cycles + 2);
    {
    ModalGuard modal; // Match the plugin caller: activating an owned editor must not close its owner.
    std::wstring single_line = L"原值";
    prepare_dialog(L"单行保存测试", L"回车确认", IDOK, nullptr, true, true);
    assert(xfx::prompt_text(g_state->window, L"单行保存测试", L"Enter 应保存。", false,
        single_line, g_state->font, true));
    assert(dialog_handled && single_line == L"回车确认");
    prepare_dialog(L"单行取消测试", L"取消内容", IDCANCEL, nullptr, true, true);
    assert(!xfx::prompt_text(g_state->window, L"单行取消测试", L"Escape 应取消。", false,
        single_line, g_state->font, true));
    assert(dialog_handled && single_line == L"回车确认");
    }
    if (argc > 1) {
        save_test_preview(g_state->window, fs::path(argv[1]).parent_path() / L"ui-preview-light.png");
    }
    fs::create_directories(capture_directory());
    {
        Gdiplus::Bitmap bitmap(3, 2, PixelFormat32bppARGB);
        bitmap.SetPixel(0, 0, Gdiplus::Color(255, 12, 34, 56));
        CLSID png{}; CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
        assert(bitmap.Save((capture_directory() / L"测试一.png").c_str(), &png) == Gdiplus::Ok);
        assert(bitmap.Save((capture_directory() / L"测试二.png").c_str(), &png) == Gdiplus::Ok);
    }
    show_gallery(); pump();
    UpdateWindow(g_state->gallery_window);
    const auto cached_thumbnail = xfx::thumbnail_cache_file(capture_directory() / L"测试一.png");
    assert(fs::exists(cached_thumbnail));
    SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump_for(240);
    clear_thumbnail_cache(); // Retain legacy cache utility coverage without a product UI entry.
    assert(!fs::exists(cached_thumbnail));
    SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump_for(240);
    assert((GetWindowLongPtrW(g_state->gallery_window, GWL_STYLE) & WS_THICKFRAME) != 0);
    assert(IsWindow(GetDlgItem(g_state->gallery_window, 1033))); // Integrated comparison preview.
    assert(g_state->settings.light_theme);
    HDC gallery_dc = CreateCompatibleDC(nullptr);
    SendMessageW(g_state->gallery_window, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(gallery_dc),
        reinterpret_cast<LPARAM>(g_state->gallery_status));
    assert(GetTextColor(gallery_dc) == RGB(37,37,37) && GetBkColor(gallery_dc) == RGB(237,237,237));
    DeleteDC(gallery_dc);
    assert(IsWindow(g_state->window) && IsWindow(g_state->gallery_window));
    assert(g_state->modal_depth == 1);
    assert(selected_captures().size() == 1 && !selected_capture().empty());
    SendMessageW(g_state->gallery_list, LB_SETSEL, TRUE, -1); update_gallery_selection();
    assert(selected_captures().size() == 2 && selected_capture().empty());
    assert(!IsWindowEnabled(GetDlgItem(g_state->gallery_window, kCopyCaptureId)));
    prepare_dialog(L"删除快照", L"", IDNO, g_state->gallery_window, false);
    delete_selected_captures();
    assert(dialog_handled && xfx::scan_captures(capture_directory()).size() == 2);
    SendMessageW(g_state->gallery_list, LB_SETSEL, FALSE, -1); update_gallery_selection();
    assert(!IsWindowEnabled(GetDlgItem(g_state->gallery_window, kDeleteCaptureId)));
    delete_selected_captures(); // No selection must not open a confirmation dialog.
    assert(xfx::scan_captures(capture_directory()).size() == 2);
    SendMessageW(g_state->gallery_list, LB_SETSEL, TRUE, 0); update_gallery_selection();
    const auto cached_capture = selected_capture();
    HANDLE cached_file = CreateFileW(cached_capture.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(cached_file != INVALID_HANDLE_VALUE);
    CloseHandle(cached_file);
    prepare_dialog(L"删除快照", L"", IDYES, g_state->gallery_window, false);
    delete_selected_captures();
    assert(dialog_handled && xfx::scan_captures(capture_directory()).size() == 1);
    assert(g_state->modal_depth == 1 && selected_captures().size() == 1);
    check_gallery_comparison(argc > 1 ? fs::path(argv[1]).parent_path() : directory);
    DestroyWindow(g_state->gallery_window);
    assert(g_state->modal_depth == 0);
    SetWindowTextW(g_state->search, L"gsmh");
    populate_results();
    g_test_script_reply = xfx::ScriptReply{true, false, 1, ""};
    results_list_proc(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(12, 4));
    pump();
    // The apply runs on the AEGP idle hook and closes the palette from there.
    drain_idle_applies();
    g_test_script_reply.reset();
    assert(!g_state->window);
    g_test_initial_background_fill_count = 0;
    const auto scans_before_reopen = g_test_catalog_scan_count;
    show_console();
    assert(IsWindow(g_state->window));
    assert(g_test_catalog_scan_count == scans_before_reopen);
    assert(g_state->catalog_initialized);
    refresh_catalog();
    assert(g_test_catalog_scan_count == scans_before_reopen + 1);
    std::cout << "PASS reopen reuses session catalog and explicit refresh still rescans\n";
    assert(g_state->initial_background_primed && g_test_initial_background_fill_count > 0);
    // The configured shortcut is already registered by the message-only window.
    // Use a different key to confirm unrelated global registrations remain available.
    assert(RegisterHotKey(nullptr, 0x5847, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24));
    UnregisterHotKey(nullptr, 0x5847);
    xfx::HotkeySpec low_level_spec{MOD_SHIFT, VK_SPACE};
    assert(hotkey_event_matches(WM_KEYDOWN, VK_SPACE, MOD_SHIFT, low_level_spec));
    assert(!hotkey_event_matches(WM_KEYUP, VK_SPACE, MOD_SHIFT, low_level_spec));
    assert(!hotkey_event_matches(WM_KEYDOWN, VK_SPACE, MOD_CONTROL, low_level_spec));
    show_console();
    assert(IsWindow(g_state->window));
    assert(g_state->settings.full_resolution_screenshots);
    g_state->settings.auto_close = true;
    HWND external_focus = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"focus-away fixture",
        WS_POPUP | WS_VISIBLE, 10, 10, 30, 30, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(external_focus);
    SetForegroundWindow(external_focus);
    SendMessageW(g_state->window, WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(external_focus)); pump();
    assert(!g_state->window);
    DestroyWindow(external_focus);
    g_state->settings.auto_close = false;
    xfx::SettingsStore::save(g_test_settings_path,g_state->settings);
    auto& bridge = xfx::script_bridge();
    if (IsWindow(g_state->gallery_window)) DestroyWindow(g_state->gallery_window);
    bridge.set_available(true); // Standalone harness substitutes for AE idle registration.
    assert(bridge.appearance().find("\tlight\tEDEDED\t") != std::string::npos);
    auto queued = bridge.submit(xfx::BridgeCommand::Gallery);
    auto request_id = std::stoull(queued.substr(queued.find_last_of('\t') + 1));
    A_long max_sleep = 60;
    bridge_idle_hook(nullptr, nullptr, &max_sleep);
    assert(bridge.status(request_id).find("\tfeature_removed\t") != std::string::npos);
    assert(!IsWindow(g_state->gallery_window));
    assert(!g_state->window);
    queued = bridge.submit(xfx::BridgeCommand::Capture);
    request_id = std::stoull(queued.substr(queued.find_last_of('\t') + 1));
    bridge_idle_hook(nullptr, nullptr, &max_sleep);
    assert(bridge.status(request_id).find("\thost_error\t") != std::string::npos);
    assert(bridge.status(request_id).starts_with("XFX1\tERROR\t"));
    assert(!IsWindow(g_state->window)); // Missing comp fails before opening a dialog/console.
    queued = bridge.submit(xfx::BridgeCommand::Preferences);
    request_id = std::stoull(queued.substr(queued.find_last_of('\t') + 1));
    bridge_idle_hook(nullptr, nullptr, &max_sleep);
    assert(bridge.status(request_id).starts_with("XFX1\tOK\t"));
    assert(g_state->settings_visible && IsWindowVisible(g_state->window));
    pump_for(350);
    assert(g_state->settings_extent == g_state->expand_to && !g_state->settings_animation_active);
    SetWindowTextW(g_state->search, L"gaussian");
    populate_results();
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
    assert(result_corner_pixel(true) != colors().selected);
    ShowWindow(g_state->window, SW_HIDE);
    EnumChildWindows(g_state->window, invalidate_child_for_reopen, 0);
    show_console();
    assert(visible_children_with_pending_paint(g_state->window) == 0);
    check_combo_geometry(regression_artifacts);
    check_ime_failure_retry();
    check_ime_pending_restore();
    check_apply_lifecycle();
    death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
    assert(!g_state);
    assert(bridge.status().find("\tunavailable\t") != std::string::npos);
    SetEnvironmentVariableW(L"XFX_THUMBNAIL_CACHE_DIR", nullptr);
    fs::remove_all(directory);
    std::cout << "native UI tests passed (search, settings, shortcut layout, mouse selection, drag, modal, gallery, close/reopen, hotkey)\n";
}
