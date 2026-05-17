#include "console.hpp"

#ifdef _WIN32
#include <atomic>
#include <windows.h>
#endif

namespace hisol {

struct ConsoleModeGuard::Impl {
#ifdef _WIN32
    Impl()
    {
        stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
        stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);

        if (stdin_handle != INVALID_HANDLE_VALUE && GetConsoleMode(stdin_handle, &stdin_mode)) {
            has_stdin_mode = true;
            DWORD raw_mode = stdin_mode;
            raw_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
            SetConsoleMode(stdin_handle, raw_mode);
        }

        if (stdout_handle != INVALID_HANDLE_VALUE && GetConsoleMode(stdout_handle, &stdout_mode)) {
            has_stdout_mode = true;
            SetConsoleMode(
                stdout_handle,
                stdout_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);
        }

        active_guard.store(this);
        SetConsoleCtrlHandler(&Impl::handle_control, TRUE);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl()
    {
        restore();
        active_guard.store(nullptr);
        SetConsoleCtrlHandler(&Impl::handle_control, FALSE);
    }

    void restore()
    {
        reset_display();
        if (has_stdin_mode) {
            SetConsoleMode(stdin_handle, stdin_mode);
        }
        if (has_stdout_mode) {
            SetConsoleMode(stdout_handle, stdout_mode);
        }
    }

    void reset_display()
    {
        if (!has_stdout_mode) {
            return;
        }

        static constexpr char reset_sequence[] =
            "\x1b[?1049l" // leave alternate screen
            "\x1b[?25h"   // show cursor
            "\x1b[0m"     // reset text attributes
            "\x1b[?7h"    // enable line wrap
            "\x1b[?1l"    // normal cursor keys
            "\x1b>";      // normal keypad mode

        DWORD written = 0;
        WriteFile(
            stdout_handle,
            reset_sequence,
            static_cast<DWORD>(sizeof(reset_sequence) - 1),
            &written,
            nullptr);
    }

    static BOOL WINAPI handle_control(DWORD control_type)
    {
        switch (control_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (Impl* guard = active_guard.load()) {
                guard->restore();
            }
            return FALSE;
        default:
            return FALSE;
        }
    }

    HANDLE stdin_handle = INVALID_HANDLE_VALUE;
    HANDLE stdout_handle = INVALID_HANDLE_VALUE;
    DWORD stdin_mode = 0;
    DWORD stdout_mode = 0;
    bool has_stdin_mode = false;
    bool has_stdout_mode = false;
    inline static std::atomic<Impl*> active_guard = nullptr;
#endif
};

ConsoleModeGuard::ConsoleModeGuard()
    : impl_(std::make_unique<Impl>())
{
}

ConsoleModeGuard::~ConsoleModeGuard() = default;

} // namespace hisol
