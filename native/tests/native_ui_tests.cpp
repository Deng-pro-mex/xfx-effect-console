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
            g_test_drag_button_held = false;
            g_test_drag_cursor = POINT{0, 0};
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
            for (const auto& item : g_state->catalog) {
                if (item.item_type != L"effect") continue;
                for (int slot : {0, 2, 5, 8}) {
                    if (!g_state->settings.row_slots[static_cast<size_t>(slot)].empty()) continue;
                    g_state->settings.row_slots[static_cast<size_t>(slot)] = item_key(item);
                }
                if (!g_state->settings.row_slots[0].empty() &&
                    !g_state->settings.row_slots[2].empty() &&
                    !g_state->settings.row_slots[5].empty() &&
                    !g_state->settings.row_slots[8].empty()) break;
            }
            SetWindowTextW(g_state->search, L"");
            populate_results();
            layout_controls(g_state->window);
            pump_for(240);
            if (argc > 1) {
                save_test_preview(g_state->window,
                    fs::path(argv[1]).parent_path() / L"second-strip-preview.png");
            }
            assert(IsWindowVisible(g_state->slot_row_buttons[0]));
            // Bound cells must show a label; the tooltip table is what the user reads.
            // Every bound cell must resolve to a visible label, and every cell must have
            // a non-empty tooltip (that is the only explanation of what the row does).
            for (int slot = 0; slot < 9; ++slot) {
                const auto label = slot_row_display_label(slot);
                assert(!label.empty());
                const bool bound = !g_state->settings.row_slots[static_cast<size_t>(slot)].empty();
                if (bound) assert(label != L"空");
            }
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
                // And the window itself is clipped to a rounded region, like every other
                // surface in the console.
                // Capture the popup from the SCREEN. PrintWindow on a system popup gives
                // a blank sliver, and the hand-rendered WM_DRAWITEM path never proved the
                // real window was painted - which is exactly how the white list survived
                // several rounds of "verified" fixes.
                {
                    RECT screen_rect{};
                    if (GetWindowRect(popup, &screen_rect)) {
                        const int w = screen_rect.right - screen_rect.left;
                        const int h = screen_rect.bottom - screen_rect.top;
                        if (w > 8 && h > 8) {
                            HDC screen = GetDC(nullptr);
                            HDC memory = CreateCompatibleDC(screen);
                            HBITMAP shot = CreateCompatibleBitmap(screen, w, h);
                            const auto previous = SelectObject(memory, shot);
                            BitBlt(memory, 0, 0, w, h, screen, screen_rect.left,
                                screen_rect.top, SRCCOPY);
                            size_t white = 0, sampled = 0;
                            for (int y = 0; y < h; ++y)
                                for (int x = 0; x < w; ++x, ++sampled)
                                    if (GetPixel(memory, x, y) == RGB(255, 255, 255)) ++white;
                            std::cout << "  screen id=" << id << " " << w << "x" << h
                                      << " whitePct=" << (sampled ? white * 100 / sampled : 0);
                            assert(white * 100 < sampled * 60);
                            if (argc > 1) {
                                const auto destination = fs::path(argv[1]).parent_path() /
                                    (L"combo-screen-" + std::to_wstring(id) + L".png");
                                Gdiplus::Bitmap image(shot, nullptr);
                                CLSID png{0x557cf406, 0x1a04, 0x11d3,
                                    {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
                                image.Save(destination.c_str(), &png, nullptr);
                            }
                            SelectObject(memory, previous);
                            DeleteObject(shot); DeleteDC(memory); ReleaseDC(nullptr, screen);
                        }
                    }
                }
                // And it has a real height, not the collapsed sliver that reads as blank.
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
