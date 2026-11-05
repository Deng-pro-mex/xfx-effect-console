#include <windows.h>
#include <commdlg.h>
#include "../src/ae_capture.h"
#include "../src/themed_menu.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace xfx {
CaptureCompName snapshot_test_name(SPBasicSuite*, AEGP_PluginID);
ScriptReply snapshot_test_render(SPBasicSuite*, AEGP_PluginID, const std::filesystem::path&, bool);
UINT snapshot_test_menu(HMENU, UINT, int, int, HWND, const ThemeColors&, HFONT);
}
static BOOL WINAPI snapshot_test_dialog(LPOPENFILENAMEW dialog);
static int WINAPI snapshot_test_message(HWND, LPCWSTR, LPCWSTR, UINT);
#define MessageBoxW snapshot_test_message
#define XFX_CONSOLE_TEST
#define GetSaveFileNameW snapshot_test_dialog
#define current_capture_comp_name snapshot_test_name
#define capture_comp_png snapshot_test_render
#define track_themed_popup snapshot_test_menu
#include "../plugin/XFXNativeConsole.cpp"
#undef GetSaveFileNameW
#undef MessageBoxW
#undef current_capture_comp_name
#undef capture_comp_png
#undef track_themed_popup

static unsigned dialog_calls = 0, render_calls = 0;
static bool accept_dialog = false, valid_comp = true, render_succeeds = true;
static int render_output = 0; // 0 valid PNG; 1 no file; 2 empty; 3 corrupt.
static unsigned message_calls = 0;
static std::wstring result_message;
static UINT result_message_flags;
static int WINAPI snapshot_test_message(HWND, LPCWSTR text, LPCWSTR, UINT flags) {
    ++message_calls; result_message = text ? text : L""; result_message_flags = flags;
    return IDOK;
}
static std::wstring default_filename, dialog_title;
static DWORD dialog_flags;
static fs::path chosen_path, rendered_path;
static bool rendered_full_resolution;
static unsigned idle_epoch = 0, dialog_epoch = 0;
static bool enforce_capture_context = true;
static void require_save(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
namespace xfx {
UINT snapshot_test_menu(HMENU, UINT, int, int, HWND, const ThemeColors&, HFONT) { return 1; }
CaptureCompName snapshot_test_name(SPBasicSuite*, AEGP_PluginID) {
    require_save(g_bridge_processing && idle_epoch > 0, "metadata queried outside AE Idle callback");
    return {{valid_comp, false, valid_comp ? 1U : 0U, valid_comp ? "" : "没有活动合成"}, L"当前 合成🎬"};
}
ScriptReply snapshot_test_render(SPBasicSuite*, AEGP_PluginID, const fs::path& target, bool full_resolution) {
    if (enforce_capture_context) require_save(g_bridge_processing && idle_epoch > dialog_epoch,
        "render must use a fresh host Idle callback after the OS Save As dialog has returned");
    ++render_calls; rendered_path = target; rendered_full_resolution = full_resolution;
    if (!render_succeeds) return {false, false, 0, "测试渲染失败"};
    if (render_output == 1) return {true, false, 1, ""};
    if (render_output == 2 || render_output == 3) {
        std::ofstream output(target, std::ios::binary);
        if (render_output == 3) output << "not a PNG";
        return {true, false, 1, ""};
    }
    Gdiplus::Bitmap image(3, 2, PixelFormat32bppARGB);
    CLSID png{}; CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &png);
    require_save(image.Save(target.c_str(), &png) == Gdiplus::Ok, "fixture PNG write failed");
    return {true, false, 1, ""};
}
}
static BOOL WINAPI snapshot_test_dialog(LPOPENFILENAMEW dialog) {
    ++dialog_calls; dialog_epoch = idle_epoch;
    require_save(g_bridge_processing, "save path selection must originate in AE Idle");
    // A modal Win32 dialog may pump nested host idle callbacks. No reentry.
    A_long sleep = 20; bridge_idle_hook(nullptr, nullptr, &sleep);
    require_save(dialog_calls == 1 && render_calls == 0, "modal reentry consumed or rendered another capture");
    require_save(render_calls == 0, "snapshot was rendered before the user confirmed Save As");
    default_filename = dialog->lpstrFile;
    dialog_title = dialog->lpstrTitle ? dialog->lpstrTitle : L"";
    dialog_flags = dialog->Flags;
    require_save(g_state->modal_depth > 0, "save dialog was not protected against console focus loss");
    if (!accept_dialog) return FALSE;
    wcscpy_s(dialog->lpstrFile, dialog->nMaxFile, chosen_path.c_str());
    return TRUE;
}
static void reset_save() {
    dialog_calls = render_calls = 0;
    accept_dialog = false; valid_comp = render_succeeds = true;
    default_filename.clear(); dialog_title.clear(); rendered_path.clear();
    render_output = 0; message_calls = 0; result_message.clear();
    idle_epoch = dialog_epoch = 0; enforce_capture_context = true;
}
static void idle() { ++idle_epoch; A_long sleep = 20; bridge_idle_hook(nullptr, nullptr, &sleep); }
static xfx::BridgeCompletion drive_capture() {
    const auto response = xfx::script_bridge().submit(xfx::BridgeCommand::Capture);
    require_save(response.starts_with("XFX1\tQUEUED\t"), "capture was not queued");
    const auto id = std::stoull(response.substr(12));
    require_save(dialog_calls == 0 && render_calls == 0, "enqueue accessed AE immediately");
    idle();
    if (accept_dialog && valid_comp) {
        require_save(dialog_calls == 1 && render_calls == 0 && g_pending_snapshot.has_value(),
            "first Idle must select a path and return to AE before rendering");
        require_save(xfx::script_bridge().status(id).starts_with("XFX1\tPENDING\t"), "request completed before rendering");
        idle();
    }
    const auto fields = split_tsv(xfx::script_bridge().status(id));
    require_save(fields.size() >= 5 && (fields[1] == "OK" || fields[1] == "ERROR"), "missing actual host result");
    return {fields[1] == "OK", fields[1] == "OK" ? static_cast<unsigned>(std::stoul(fields[3])) : 0U,
        fields[1] == "OK" ? "" : fields[3], fields[4]};
}
int wmain() {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    std::cout << "STAGE snapshot fixture setup\n";
    // Plugin entry initializes GDI+ before any BusyFeedback/LoadingSurface. This
    // fixture bypasses entry, so it must establish the same process prerequisite.
    struct GraphicsRuntime {
        ULONG_PTR token = 0;
        Gdiplus::Status status;
        GraphicsRuntime() {
            Gdiplus::GdiplusStartupInput input;
            status = Gdiplus::GdiplusStartup(&token, &input, nullptr);
        }
        ~GraphicsRuntime() { if (token) Gdiplus::GdiplusShutdown(token); }
    } graphics;
    if (graphics.status != Gdiplus::Ok) { std::cerr << "FAIL fixture GDI+ startup\n"; return 1; }
    const auto directory = fs::temp_directory_path() / (L"xfx-snapshot-save-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(directory);
    g_test_settings_path = directory / L"profile" / L"settings.ini";
    g_state = new PluginState();
    xfx::script_bridge().set_available(true);
    g_state->window = CreateWindowExW(0, L"STATIC", L"snapshot test", 0, 0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_state->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0,
        g_state->window, nullptr, GetModuleHandleW(nullptr), nullptr);
    chosen_path = directory / L"手工重命名.png";
    int exit_code = 0;
    try {
        std::cout << "STAGE cancelled Save As\n";
        reset_save();
        const auto cancelled = drive_capture();
        require_save(dialog_calls == 1, "capture command did not open Save As");
        require_save(!cancelled.succeeded && cancelled.code == "cancelled" && render_calls == 0,
            "cancelled Save As rendered a snapshot or returned an error instead of cancellation");
        require_save(default_filename == L"当前 合成🎬.png", "Save As default is not the current composition name");
        require_save(dialog_title == L"保存当前合成快照", "wrong snapshot save dialog");
        require_save((dialog_flags & (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR)) ==
            (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR), "Save As lacks file protections");
        require_save(!fs::exists(directory / L"profile" / L"Capture") && !fs::exists(chosen_path),
            "cancelled Save As created files or a gallery folder");
        std::cout << "PASS cancelled Save As has the comp default and creates no snapshot\n";

        std::cout << "STAGE confirmed Save As\n";
        reset_save(); accept_dialog = true; g_state->settings.full_resolution_screenshots = false;
        const auto saved = drive_capture();
        require_save(saved.succeeded && saved.count == 1 && dialog_calls == 1 && render_calls == 1,
            "confirmed Save As did not save exactly one snapshot");
        require_save(rendered_path == chosen_path && rendered_full_resolution,
            "snapshot ignored the user's filename/path or resolution setting");
        require_save(fs::exists(chosen_path) && !fs::exists(directory / L"profile" / L"Capture"),
            "snapshot was routed through a hidden gallery instead of the chosen file");
        require_save(g_state->modal_depth == 0, "snapshot leaked its modal guard");
        std::cout << "PASS snapshot saves directly to the chosen Unicode filename\n";

        std::cout << "STAGE missing composition\n";
        reset_save(); valid_comp = false;
        const auto unavailable = drive_capture();
        require_save(!unavailable.succeeded && dialog_calls == 0 && render_calls == 0,
            "missing composition opened Save As or rendered");
        std::cout << "PASS missing composition is reported before Save As\n";

        std::cout << "STAGE renderer failure\n";
        reset_save(); accept_dialog = true; render_succeeds = false;
        const auto failed = drive_capture();
        require_save(!failed.succeeded && failed.count == 0 && render_calls == 1 &&
            failed.message.find("测试渲染失败") != std::string::npos, "render failure became a success");
        std::ifstream previous(chosen_path, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(previous)), {});
        require_save(bytes.size() > 8 && bytes.substr(0, 8) == std::string("\x89PNG\r\n\x1a\n", 8),
            "failed render damaged the previous PNG");
        previous.close();
        std::cout << "PASS render failure preserves the previous file and reports failure\n";

        std::cout << "STAGE false renderer success\n";
        const auto original_path = chosen_path;
        for (int kind : {1, 2, 3}) {
            reset_save(); accept_dialog = true; render_output = kind;
            chosen_path = directory / (L"invalid-" + std::to_wstring(kind) + L".png");
            const auto invalid = drive_capture();
            require_save(!invalid.succeeded && invalid.count == 0,
                "missing, empty or corrupt output was reported as saved");
        }
        chosen_path = original_path;
        std::cout << "PASS a renderer success without a decodable PNG is never a saved snapshot\n";

        std::cout << "STAGE native result feedback\n";
        reset_save(); accept_dialog = true; render_succeeds = false;
        window_proc(g_state->window, WM_COMMAND, kCaptureId, 0);
        require_save(dialog_calls == 0 && render_calls == 0 && message_calls == 0, "native WM_COMMAND called host screenshot synchronously");
        window_proc(g_state->window, WM_COMMAND, kCaptureId, 0); // Rapid duplicate is ignored.
        idle(); require_save(render_calls == 0, "native first Idle rendered in the modal dialog callback");
        idle(); idle();
        require_save(message_calls == 1 && render_calls == 1 && (result_message_flags & MB_ICONERROR) &&
            result_message.find(L"测试渲染失败") != std::wstring::npos &&
            result_message.find(chosen_path.wstring()) != std::wstring::npos,
            "native snapshot button silently discarded the save failure or target path");
        reset_save(); accept_dialog = true;
        window_proc(g_state->window, WM_COMMAND, kCaptureId, 0);
        idle(); idle(); idle();
        require_save(message_calls == 1 && render_calls == 1 && (result_message_flags & MB_ICONINFORMATION) &&
            result_message.find(chosen_path.wstring()) != std::wstring::npos,
            "native snapshot success did not display the actual saved path");
        std::cout << "PASS native camera reports failure and verified destination explicitly\n";

        std::cout << "STAGE native capture button\n";
        reset_save();
        window_proc(g_state->window, WM_COMMAND, kCaptureId, 0);
        require_save(dialog_calls == 0 && render_calls == 0, "native camera bypassed AE Idle");
        idle(); idle();
        require_save(dialog_calls == 1 && render_calls == 0 && message_calls == 0, "native cancelled capture rendered or displayed an error");
        const auto removed = dispatch_bridge_request({5, xfx::BridgeCommand::Gallery, -1});
        require_save(!removed.succeeded && !IsWindow(g_state->gallery_window), "removed gallery can still open");
        std::cout << "PASS native capture button shares Save As and gallery is unavailable\n";

        std::cout << "STAGE cancelled native queue\n";
        reset_save();
        window_proc(g_state->window, WM_COMMAND, kCaptureId, 0);
        require_save(g_native_snapshot_requests.size() == 1, "native queue missing");
        xfx::script_bridge().cancel(*g_native_snapshot_requests.begin()); idle();
        require_save(g_native_snapshot_requests.empty() && !g_pending_snapshot && dialog_calls == 0 && render_calls == 0 && message_calls == 0,
            "cancelled native queue leaked request state or accessed AE");
        std::cout << "PASS native queue cancellation releases state without rendering or a false error\n";

        std::cout << "STAGE temporary frame export\n";
        reset_save(); accept_dialog = true; render_output = 3; enforce_capture_context = false;
        // This separate legacy export fixture is not a native-camera timing test.
        ++idle_epoch; g_bridge_processing = true;
        export_current_frame();
        g_bridge_processing = false; // The injected renderer writes a non-image, so export fails safely.
        require_save(render_calls == 1 && rendered_path.parent_path() != capture_directory(),
            "export still writes to the removed gallery");
        require_save(!fs::exists(rendered_path), "export failure retained a temporary snapshot");
        std::cout << "PASS export uses a disposable frame instead of a hidden gallery\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; exit_code = 1;
    }
    std::cout << "STAGE snapshot fixture cleanup\n";
    DestroyWindow(g_state->window); delete g_state; g_state = nullptr;
    if (!fs::equivalent(directory.parent_path(), fs::temp_directory_path())) {
        std::cerr << "FAIL test cleanup escaped temp\n";
        return 1;
    }
    fs::remove_all(directory);
    return exit_code;
}
