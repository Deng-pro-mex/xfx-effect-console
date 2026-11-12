#pragma once

// Settings are saved from the dropdown, so a test that changes the mode must change the
// control as well or the next save silently reverts it.
static void force_ime_mode(xfx::ImeMode mode) {
    SendMessageW(g_state->ime_mode_combo, CB_SETCURSEL, static_cast<int>(mode), 0);
    g_state->settings.ime_mode = mode;
    g_state->settings.use_english_mode =
        mode == xfx::ImeMode::Restore || mode == xfx::ImeMode::EnglishAlways;
}

static void check_ime_failure_retry() {
    const bool previous_mode = g_state->settings.use_english_mode;
    // Start from a clean controller: an earlier block may have left a session open, and
    // this check is about the rejection path, not about inheriting that state.
    g_state->ime.restore();
    g_state->ime_session_active = false;
    g_state->ime = xfx::ImeController{};
    // The controller is inactive in Disabled mode, which short-circuits the switch; the
    // rejection path only exists in a mode that actually asks for English.
    force_ime_mode(xfx::ImeMode::Restore);
    assert(g_state->ime.capture_original(g_state->search));
    g_state->ime_session_active = true;
    // Temporarily unavailable real contexts must not leave the outer session
    // marked usable after the backend has rejected the switch.
    HIMC detached = ImmAssociateContext(g_state->search, nullptr);
    assert(detached);
    const bool first_rejected = !ensure_search_english_mode();
    const bool outer_session_reset = !g_state->ime_session_active;
    ImmAssociateContext(g_state->search, detached);
    // Model a completed backend rollback before focus retries the operation.
    g_state->ime.restore();
    const bool retry_succeeded = ensure_search_english_mode();
    g_state->ime.restore();
    g_state->ime_session_active = false;
    g_state->settings.use_english_mode = previous_mode;
    if (previous_mode) ensure_search_english_mode();
    assert(first_rejected);
    assert(outer_session_reset);
    assert(retry_succeeded);
    std::cout << "PASS rejected IME switch resets its outer session and succeeds on retry\n";
}

static HIMC prepare_pending_ime_restore() {
    g_state->ime.restore();
    g_state->ime_session_active = false;
    HIMC context = ImmGetContext(g_state->search);
    assert(context);
    ImmSetOpenStatus(context, TRUE);
    assert(ImmGetOpenStatus(context));
    ImmReleaseContext(g_state->search, context);
    assert(g_state->ime.capture_original(g_state->search));
    g_state->ime_session_active = true;
    force_ime_mode(xfx::ImeMode::Restore);
    assert(ensure_search_english_mode());
    const HIMC detached = ImmAssociateContext(g_state->search, nullptr);
    assert(detached == context);
    assert(!ensure_search_english_mode());
    assert(!g_state->ime_session_active);
    ImmAssociateContext(g_state->search, detached);
    return context;
}

static HIMC pending_restore_context = nullptr;
static bool pending_restore_observed = false;
static void observe_pending_ime_before_script() {
    observe_apply_script_boundary();
    pending_restore_observed = ImmGetOpenStatus(pending_restore_context) != FALSE;
}

static void check_ime_pending_restore() {
    const auto original_settings = g_state->settings;
    const auto original_reply = g_test_script_reply;
    const auto original_observer = g_test_before_script;
    const HIMC close_context = prepare_pending_ime_restore();
    save_and_restore();
    assert(ImmGetOpenStatus(close_context));
    SetWindowTextW(g_state->search, L"gaussian");
    SendMessageW(g_state->filter, CB_SETCURSEL, 0, 0);
    populate_results();
    pending_restore_context = prepare_pending_ime_restore();
    pending_restore_observed = false;
    g_test_script_reply = xfx::ScriptReply{true, false, 1, ""};
    g_test_before_script = observe_pending_ime_before_script;
    assert(execute_selected());
    // The apply runs on the idle hook, which is also where the pending IME restore is
    // retried; drive it the way the host does.
    drain_idle_applies();
    assert(pending_restore_observed && !g_state->window);
    g_test_before_script = original_observer;
    g_test_script_reply = original_reply;
    pending_restore_context = nullptr;
    g_state->settings = original_settings;
    xfx::SettingsStore::save(g_test_settings_path, original_settings);
    show_console();
    std::cout << "PASS pending IME restoration is retried on close and before applying an effect\n";
}
