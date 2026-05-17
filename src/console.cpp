#include "console.hpp"

#ifdef _WIN32
#include <atomic>
#include <windows.h>
#endif

#include <iostream>
#include <stdexcept>
#include <string>

namespace hisol {
namespace {

#ifdef _WIN32
class StdinModeRestore {
public:
    StdinModeRestore(HANDLE handle, DWORD mode)
        : handle_(handle), mode_(mode)
    {
    }

    StdinModeRestore(const StdinModeRestore&) = delete;
    StdinModeRestore& operator=(const StdinModeRestore&) = delete;

    ~StdinModeRestore()
    {
        SetConsoleMode(handle_, mode_);
    }

private:
    HANDLE handle_;
    DWORD mode_;
};

std::string utf8_from_wide(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }

    const int byte_count = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (byte_count <= 0) {
        throw std::runtime_error("failed to convert console input to UTF-8");
    }

    std::string result(static_cast<std::size_t>(byte_count), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        byte_count,
        nullptr,
        nullptr);
    return result;
}
#endif

} // namespace

struct ConsoleModeGuard::Impl {
#ifdef _WIN32
    Impl()
    {
        stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
        stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);

        if (stdin_handle != INVALID_HANDLE_VALUE && GetConsoleMode(stdin_handle, &stdin_mode)) {
            has_stdin_mode = true;
            DWORD raw_mode = stdin_mode;
            raw_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
            raw_mode |= ENABLE_WINDOW_INPUT;
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
            "\x1b[?25h"   // show cursor
            "\x1b[0m"     // reset text attributes
            "\x1b[?7h"    // enable line wrap
            "\x1b[?1l"    // normal cursor keys
            "\x1b>"        // normal keypad mode
            "\r\n";        // leave the shell prompt on a fresh line at column 0

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

std::optional<ConsoleSize> current_console_size()
{
#ifdef _WIN32
    const HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (stdout_handle == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(stdout_handle, &info)) {
        return std::nullopt;
    }

    return ConsoleSize{
        info.srWindow.Right - info.srWindow.Left + 1,
        info.srWindow.Bottom - info.srWindow.Top + 1,
    };
#else
    return std::nullopt;
#endif
}

std::string read_password_from_console(const std::string& prompt)
{
#ifdef _WIN32
    const HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD original_mode = 0;
    if (stdin_handle == INVALID_HANDLE_VALUE || !GetConsoleMode(stdin_handle, &original_mode)) {
        throw std::runtime_error(
            "cannot prompt for password because stdin is not an interactive console");
    }

    DWORD masked_mode = original_mode;
    masked_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    masked_mode |= ENABLE_PROCESSED_INPUT;
    if (!SetConsoleMode(stdin_handle, masked_mode)) {
        throw std::runtime_error("failed to disable console echo for password prompt");
    }
    StdinModeRestore restore(stdin_handle, original_mode);

    std::cerr << prompt;
    std::cerr.flush();

    std::wstring password;
    while (true) {
        wchar_t ch = L'\0';
        DWORD chars_read = 0;
        if (!ReadConsoleW(stdin_handle, &ch, 1, &chars_read, nullptr)) {
            throw std::runtime_error("failed to read password from console");
        }
        if (chars_read == 0) {
            continue;
        }

        if (ch == L'\r' || ch == L'\n') {
            std::cerr << "\r\n";
            return utf8_from_wide(password);
        }

        if (ch == L'\b' || ch == 0x7f) {
            if (!password.empty()) {
                password.pop_back();
                std::cerr << "\b \b";
                std::cerr.flush();
            }
            continue;
        }

        if (ch < L' ') {
            continue;
        }

        password.push_back(ch);
        std::cerr << '*';
        std::cerr.flush();
    }
#else
    (void)prompt;
    throw std::runtime_error(
        "interactive password prompting is only implemented on Windows; pass --password or --password-env");
#endif
}

} // namespace hisol
