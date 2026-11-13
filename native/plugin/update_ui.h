// Included inside the plugin's private namespace, after UI helpers are defined.
#ifdef XFX_CONSOLE_TEST
xfx::UpdateInfo g_test_update_info;
#else
std::unique_ptr<xfx::UpdateClient> g_update_client;
ULONGLONG g_update_last_tick = 0, g_update_last_check = 0;
bool g_user_update_active = false;
bool g_user_check_active = false;
// Silent automatic update: no click, no dialog. Download starts on its own; the
// installer is launched only after AE has fully exited, never while it runs.
bool g_auto_download_started = false;
bool g_auto_install_pending = false;
bool g_auto_prompt_shown = false;
#endif

xfx::UpdateInfo update_info() {
#ifdef XFX_CONSOLE_TEST
    return g_test_update_info;
#else
    return g_update_client ? g_update_client->status() : xfx::UpdateInfo{};
#endif
}
const char* update_state_name(xfx::UpdateInfo::State state) {
    switch (state) {
    case xfx::UpdateInfo::Checking: return "checking";
    case xfx::UpdateInfo::Current: return "current";
    case xfx::UpdateInfo::Available: return "available";
    case xfx::UpdateInfo::Downloading: return "downloading";
    case xfx::UpdateInfo::Ready: return "ready";
    case xfx::UpdateInfo::Failed: return "failed";
    default: return "idle";
    }
}
// True when the failure is transient (rate limit, offline, timeout) rather than a
// real configuration problem. These must not look like errors to the user: GitHub's
// unauthenticated API allows only 60 requests/hour per IP, and a shared connection
// can exhaust that at any time. Nothing is wrong with the plugin or the user's setup.
bool update_failure_is_transient(const std::string& message) {
    std::string lower = message;
    for (auto& ch : lower) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    for (const char* needle : {"rate limit", "403", "forbidden", "timed out", "timeout",
             "connect", "network", "unreachable", "resolve", "dns", "reset", "offline"}) {
        if (lower.find(needle) != std::string::npos) return true;
    }
    return false;
}
void refresh_update_ui() {
    const auto info = update_info();
    xfx::script_bridge().set_update_status(update_state_name(info.state), info.version);
    if (!g_state || !IsWindow(g_state->update_button)) return;
    const bool visible = !info.version.empty() && (info.state == xfx::UpdateInfo::Available ||
        info.state == xfx::UpdateInfo::Downloading || info.state == xfx::UpdateInfo::Ready);
    const auto label = info.state == xfx::UpdateInfo::Downloading ? std::wstring(L"下载中…") :
        info.state == xfx::UpdateInfo::Checking ? std::wstring(L"检查中…") :
        info.state == xfx::UpdateInfo::Ready ? std::wstring(L"安装更新") :
        visible ? L"更新至 " + utf8_to_wide(info.version.c_str()) : std::wstring(L"检查更新");
    wchar_t old[128]{}; GetWindowTextW(g_state->update_button, old, 128);
    const bool label_changed = label != old;
    if (label_changed) SetWindowTextW(g_state->update_button, label.c_str());
    EnableWindow(g_state->update_button, info.state != xfx::UpdateInfo::Downloading && info.state != xfx::UpdateInfo::Checking);
    if (g_state->update_available != visible || label_changed) {
        g_state->update_available = visible;
        // Layout owns visibility: settings always has the entry, while the
        // original homepage footer reveals it only when an update is available.
        layout_controls(g_state->window);
        RedrawWindow(g_state->window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
    }
}
void tick_updates() {
#ifndef XFX_CONSOLE_TEST
    const auto now = GetTickCount64();
    if (g_update_last_tick && now - g_update_last_tick < 1000) return;
    g_update_last_tick = now;
    if (!g_update_client) {
        PWSTR local = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return;
        const fs::path cache = fs::path(local) / L"LoYoi" / L"XFXNative" / L"Updates";
        CoTaskMemFree(local);
        g_update_client = std::make_unique<xfx::UpdateClient>(kProductVersion, cache);
        g_update_client->check(); g_update_last_check = now;
    }
    auto info = g_update_client->status();
    if (now - g_update_last_check >= 6ULL * 60 * 60 * 1000 &&
            (info.state == xfx::UpdateInfo::Current || info.state == xfx::UpdateInfo::Failed || info.state == xfx::UpdateInfo::Idle)) {
        g_update_client->check(); g_update_last_check = now; info = g_update_client->status();
    }
    if (g_user_check_active && (info.state == xfx::UpdateInfo::Current ||
            info.state == xfx::UpdateInfo::Available || info.state == xfx::UpdateInfo::Failed)) {
        g_user_check_active = false;
        // A rate limit or an offline machine is not an error worth a warning box: it
        // resolves itself and nothing is misconfigured. Report it in the status line
        // and only use a dialog for actionable outcomes.
        // Every check-update outcome is informational, never a decision the user
        // must click through, so it belongs in the bottom-left status line of the
        // console instead of a modal dialog.
        if (info.state == xfx::UpdateInfo::Failed) {
            set_status(update_failure_is_transient(info.message)
                ? L"暂时无法检查更新（GitHub 限流或网络不可用），稍后自动重试；这不代表已是最新"
                : L"检查更新未完成：" + utf8_to_wide(info.message.c_str()), false);
        } else if (info.state == xfx::UpdateInfo::Current) {
            set_status(L"没有发现更高版本的正式效果控制台发行版", true);
        } else {
            set_status(L"发现正式新版 " + utf8_to_wide(info.version.c_str()) + L"，正在后台下载", true);
        }
    }
    if (g_user_update_active && (info.state == xfx::UpdateInfo::Failed || info.state == xfx::UpdateInfo::Ready)) {
        g_user_update_active = false;
        // Same rule: download outcomes are status information. Installing stays an
        // explicit, user-initiated action elsewhere.
        set_status(info.state == xfx::UpdateInfo::Ready
            ? L"更新已下载并校验；关闭 AE 后自动安装"
            : L"更新下载未完成：" + utf8_to_wide(info.message.c_str()) + L"（现有插件未改变）", info.state == xfx::UpdateInfo::Ready);
    }
    // Silent path: as soon as a formal release is known, start downloading it.
    // No dialog, no button press. A failure only leaves the manual entry usable.
    if (info.state == xfx::UpdateInfo::Available && !g_auto_download_started) {
        g_auto_download_started = true;
        g_auto_install_pending = true;
        try { g_update_client->download(); } catch (...) { g_auto_install_pending = false; }
        info = g_update_client->status();
    }
    // One non-blocking toast-style notice the first time a verified installer is
    // ready. It is NOT a question: nothing is required from the user.
    if (info.state == xfx::UpdateInfo::Ready && !g_auto_prompt_shown) {
        g_auto_prompt_shown = true;
        if (g_state && IsWindow(g_state->window)) {
            set_status(L"新版 " + utf8_to_wide(info.version.c_str()) + L" 已下载并校验；关闭 AE 后会自动安装。", true);
        }
    }
    if (info.state == xfx::UpdateInfo::Ready) g_auto_install_pending = true;
    if (info.state == xfx::UpdateInfo::Failed) g_auto_install_pending = false;
#endif
    refresh_update_ui();
}
// Silent automatic install. Runs ONLY from the plugin death hook, after AE has
// released its plugin and is shutting down, so no loaded AEX is replaced. The
// installer is started elevated and waits for the remaining AE processes itself.
bool launch_auto_install() {
#ifdef XFX_CONSOLE_TEST
    return false;
#else
    if (!g_auto_install_pending || !g_update_client) return false;
    const auto info = g_update_client->status();
    if (info.state != xfx::UpdateInfo::Ready || info.installer.empty()) return false;
    std::vector<wchar_t> host(32768, L'\0');
    if (!GetModuleFileNameW(nullptr, host.data(), 32768)) return false;
    const auto parameters = L"--wait-for-ae-exit --target \"" + fs::path(host.data()).parent_path().wstring() + L"\"";
    try {
        // Re-hash the cached installer and hold a deny-write lease through process
        // creation, including the user's UAC decision.
        const auto lease = g_update_client->lock_ready_installer();
        SHELLEXECUTEINFOW launch{sizeof(launch)};
        launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        launch.lpVerb = L"runas";
        launch.lpFile = info.installer.c_str();
        launch.lpParameters = parameters.c_str();
        launch.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&launch)) return false;
        if (launch.hProcess) CloseHandle(launch.hProcess);
        return true;
    } catch (...) { return false; }
#endif
}
xfx::BridgeCompletion perform_update(bool check_only) {
    ModalGuard modal;
#ifdef XFX_CONSOLE_TEST
    return {false, 0, "test_isolation", "更新操作在原生 UI 隔离测试中禁用"};
#else
    if (!g_update_client) { g_update_last_tick = 0; tick_updates(); }
    if (!g_update_client) return {false, 0, "update_error", "无法读取本机更新目录"};
    const auto info = g_update_client->status();
    if (check_only || info.state == xfx::UpdateInfo::Idle || info.state == xfx::UpdateInfo::Current || info.state == xfx::UpdateInfo::Failed) {
        if (info.state == xfx::UpdateInfo::Downloading) return {true, 0, "", "正在下载更新，请稍候…"};
        g_user_check_active = true;
        const auto now = GetTickCount64();
        if (now - g_update_last_check >= 60000 || info.state == xfx::UpdateInfo::Idle) {
            g_update_client->check(); g_update_last_check = now;
        }
        refresh_update_ui();
        return {true, 0, "", "正在检查更新（60秒内复用最近结果）…"};
    }
    if (info.state == xfx::UpdateInfo::Available) {
        // Downloading is safe and reversible, so it starts on its own without asking.
        // Only the installer launch below requires an explicit answer.
        g_update_client->download(); g_user_update_active = true; refresh_update_ui();
        return {true, 0, "", "已开始后台下载新版 " + info.version + "，下载不影响当前 AE"};
    }
    if (info.state == xfx::UpdateInfo::Ready) {
        if (MessageBoxW(IsWindowVisible(g_state->window) ? g_state->window : GetForegroundWindow(),
            L"将启动更新安装器。\n\n请先保存所有工程，再自行退出所有 AE；回到安装器点击确定，并再次确认安装。\n程序不会关闭 AE，也不会清空个人配置。\n\n现在启动安装器？",
            L"安装更新", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return {false, 0, "cancelled", "已取消安装更新"};
        std::vector<wchar_t> host(32768, L'\0');
        if (!GetModuleFileNameW(nullptr, host.data(), 32768)) return {false, 0, "update_error", "无法确认当前 AE 安装目录"};
        const auto parameters = L"--wait-for-ae-exit --target \"" + fs::path(host.data()).parent_path().wstring() + L"\"";
        try {
            // Revalidate the selected release and hold a deny-write/delete file
            // lease through process creation, including the user's UAC decision.
            const auto lease = g_update_client->lock_ready_installer();
            SHELLEXECUTEINFOW launch{sizeof(launch)};
            launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            launch.lpVerb = L"runas"; launch.lpFile = info.installer.c_str();
            launch.lpParameters = parameters.c_str(); launch.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&launch)) return {false, 0, "update_error", "未能启动安装器或已取消管理员授权；现有插件未改变"};
            if (launch.hProcess) CloseHandle(launch.hProcess);
        } catch (const std::exception& error) {
            return {false, 0, "update_error", wide_to_utf8(L"更新安装包已改变或无法安全验证，未启动安装：" + utf8_to_wide(error.what()))};
        }
        return {true, 0, "", "安装器已启动，请自行保存退出 AE 后确认更新"};
    }
    return {true, 0, "", info.state == xfx::UpdateInfo::Downloading ? "正在下载更新…" : "正在检查更新…"};
#endif
}
