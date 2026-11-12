#pragma once

#include <windows.h>
#include <imm.h>

namespace xfx {

class ImeController {
public:
    bool capture_original(HWND input_window, HWND original_focus = nullptr);
    bool enable_english_mode();
    // Forces the native (Chinese) conversion mode on, for the "Chinese on open" option.
    bool enable_chinese_mode();
    // Leaves the current input state as English, without restoring it later.
    bool commit_english();
    // Leaves the current input state as Chinese, without restoring it later.
    bool commit_chinese();
    void restore();
#ifdef XFX_CONSOLE_TEST
    HWND test_target_window() const { return target_.window; }
#endif

private:
    struct InputState {
        HWND window = nullptr;
        HIMC context = nullptr;
        DWORD conversion = 0;
        DWORD sentence = 0;
        bool has_conversion = false;
        bool open = false;
        bool valid = false;
    };

    static InputState capture(HWND window);
    static bool restore_state(const InputState& state);

    InputState target_;
    InputState original_focus_;
    bool captured_ = false;
    bool changed_ = false;
};

} // namespace xfx
