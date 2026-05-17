#include "sol_bridge.hpp"

#include "stdio.hpp"

#include <boost/asio/post.hpp>
#include <boost/beast/core/buffers_to_string.hpp>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hisol {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;

namespace {

#ifdef _WIN32

bool is_ctrl_pressed(const KEY_EVENT_RECORD& key)
{
    return (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
}

bool is_alt_pressed(const KEY_EVENT_RECORD& key)
{
    return (key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
}

std::string utf16_to_utf8(wchar_t value)
{
    if (value == 0) {
        return {};
    }

    char output[8]{};
    const int written = WideCharToMultiByte(CP_UTF8, 0, &value, 1, output, sizeof(output), nullptr, nullptr);
    if (written <= 0) {
        return {};
    }

    return std::string(output, static_cast<std::size_t>(written));
}

std::optional<std::string> map_ctrl_key(const KEY_EVENT_RECORD& key)
{
    const auto vk = key.wVirtualKeyCode;
    if (vk >= 'A' && vk <= 'Z') {
        return std::string(1, static_cast<char>(vk - 'A' + 1));
    }

    switch (vk) {
    case '2':
        return std::string(1, '\x00');
    case '6':
        return std::string(1, '\x1e');
    case VK_OEM_4:
        return std::string(1, '\x1b');
    case VK_OEM_5:
        return std::string(1, '\x1c');
    case VK_OEM_MINUS:
        return std::string(1, '\x1f');
    case VK_OEM_2:
        return std::string(1, '\x7f');
    default:
        return std::nullopt;
    }
}

std::optional<std::string> map_special_key(const KEY_EVENT_RECORD& key)
{
    switch (key.wVirtualKeyCode) {
    case VK_BACK:
        return std::string(1, '\x08');
    case VK_TAB:
        return std::string(1, '\x09');
    case VK_RETURN:
        return std::string(1, '\x0d');
    case VK_ESCAPE:
        return std::string(1, '\x1b');
    case VK_PRIOR:
        return "\x1b[5~";
    case VK_NEXT:
        return "\x1b[6~";
    case VK_END:
        return "\x1b[F";
    case VK_HOME:
        return "\x1b[H";
    case VK_LEFT:
        return "\x1b[D";
    case VK_UP:
        return "\x1b[A";
    case VK_RIGHT:
        return "\x1b[C";
    case VK_DOWN:
        return "\x1b[B";
    case VK_INSERT:
        return "\x1b[2~";
    case VK_DELETE:
        return "\x1b[3~";
    case VK_F1:
        return "\x1b[[A";
    case VK_F2:
        return "\x1b[[B";
    case VK_F3:
        return "\x1b[[C";
    case VK_F4:
        return "\x1b[[D";
    case VK_F5:
        return "\x1b[[E";
    case VK_F6:
        return "\x1b[17~";
    case VK_F7:
        return "\x1b[18~";
    case VK_F8:
        return "\x1b[19~";
    case VK_F9:
        return "\x1b[20~";
    case VK_F10:
        return "\x1b[21~";
    case VK_F11:
        return "\x1b[23~";
    case VK_F12:
        return "\x1b[24~";
    default:
        return std::nullopt;
    }
}

std::optional<std::string> map_console_key_event(const KEY_EVENT_RECORD& key, bool& local_exit)
{
    if (!key.bKeyDown) {
        return std::nullopt;
    }

    const bool ctrl_pressed = is_ctrl_pressed(key);
    const wchar_t unicode = key.uChar.UnicodeChar;

    if (ctrl_pressed && (unicode == L'\x1d' || key.wVirtualKeyCode == VK_OEM_6)) {
        local_exit = true;
        return std::nullopt;
    }

    if (ctrl_pressed) {
        if (auto mapped = map_ctrl_key(key)) {
            return mapped;
        }
    }

    if (unicode != 0) {
        std::string text = utf16_to_utf8(unicode == L'\n' ? L'\r' : unicode);
        if (is_alt_pressed(key) && !text.empty() && static_cast<unsigned char>(text.front()) >= 0x20) {
            text.insert(text.begin(), '\x1b');
        }
        return text;
    }

    return map_special_key(key);
}

std::string repeat_sequence(std::string_view sequence, WORD repeat_count)
{
    std::string repeated;
    repeated.reserve(sequence.size() * repeat_count);
    for (WORD i = 0; i < repeat_count; ++i) {
        repeated += sequence;
    }
    return repeated;
}

#endif

} // namespace

SolBridge::SolBridge(asio::io_context& io, SolWebSocketStream& ws, bool debug_frames, bool raw_mode)
    : io_(io), ws_(ws), debug_frames_(debug_frames), raw_mode_(raw_mode)
{
#ifdef _WIN32
    input_stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#endif
}

SolBridge::~SolBridge()
{
#ifdef _WIN32
    if (input_stop_event_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(input_stop_event_));
    }
#endif
}

void SolBridge::start()
{
    set_stdout_binary();
    set_stdin_binary();
    read_next_message();
}

void SolBridge::read_stdin()
{
    if (raw_mode_) {
        read_raw_stdin();
        return;
    }

    read_interactive_stdin();
}

void SolBridge::read_raw_stdin()
{
    char ch = 0;
    while (input_active_) {
        if (!std::cin.get(ch)) {
            set_exit_status(SolBridgeExitKind::InputEnded, "stdin ended");
            asio::post(io_, [self = shared_from_this()] {
                self->close_after_writes();
            });
            return;
        }

        if (!input_active_) {
            return;
        }

        std::string chunk(1, ch);
        asio::post(io_, [self = shared_from_this(), chunk = std::move(chunk)]() mutable {
            self->queue_write(std::move(chunk));
        });
    }
}

void SolBridge::read_interactive_stdin()
{
#ifdef _WIN32
    const HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (stdin_handle == INVALID_HANDLE_VALUE) {
        set_exit_status(SolBridgeExitKind::InputEnded, "stdin is not available");
        asio::post(io_, [self = shared_from_this()] {
            self->close_after_writes();
        });
        return;
    }
    if (input_stop_event_ == nullptr) {
        set_exit_status(SolBridgeExitKind::InputEnded, "input stop event is not available");
        asio::post(io_, [self = shared_from_this()] {
            self->close_after_writes();
        });
        return;
    }

    while (input_active_) {
        HANDLE handles[2] = {
            stdin_handle,
            static_cast<HANDLE>(input_stop_event_),
        };
        const DWORD wait_result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (!input_active_) {
            return;
        }
        if (wait_result == WAIT_OBJECT_0 + 1) {
            return;
        }
        if (wait_result != WAIT_OBJECT_0) {
            set_exit_status(SolBridgeExitKind::InputEnded, "stdin wait failed");
            asio::post(io_, [self = shared_from_this()] {
                self->close_after_writes();
            });
            return;
        }

        INPUT_RECORD record{};
        DWORD records_read = 0;
        if (!ReadConsoleInputW(stdin_handle, &record, 1, &records_read) || records_read == 0) {
            continue;
        }
        if (record.EventType != KEY_EVENT) {
            continue;
        }

        bool local_exit = false;
        auto sequence = map_console_key_event(record.Event.KeyEvent, local_exit);
        if (local_exit) {
            asio::post(io_, [self = shared_from_this()] {
                self->exit_local();
            });
            return;
        }
        if (!sequence || sequence->empty()) {
            continue;
        }

        std::string chunk = repeat_sequence(*sequence, record.Event.KeyEvent.wRepeatCount);
        asio::post(io_, [self = shared_from_this(), chunk = std::move(chunk)]() mutable {
            self->queue_write(std::move(chunk));
        });
    }
#else
    char ch = 0;
    bool drop_next_lf = false;
    while (input_active_) {
        if (!std::cin.get(ch)) {
            set_exit_status(SolBridgeExitKind::InputEnded, "stdin ended");
            asio::post(io_, [self = shared_from_this()] {
                self->close_after_writes();
            });
            return;
        }

        if (!input_active_) {
            return;
        }

        if (drop_next_lf && ch == '\n') {
            drop_next_lf = false;
            continue;
        }
        drop_next_lf = false;

        if (ch == '\r') {
            drop_next_lf = true;
        } else if (ch == '\n') {
            ch = '\r';
        }

        if (ch == '\x1d') {
            asio::post(io_, [self = shared_from_this()] {
                self->exit_local();
            });
            return;
        }

        std::string chunk(1, ch);
        asio::post(io_, [self = shared_from_this(), chunk = std::move(chunk)]() mutable {
            self->queue_write(std::move(chunk));
        });
    }
#endif
}

int SolBridge::exit_code() const
{
    return exit_code_;
}

SolBridgeExitStatus SolBridge::exit_status() const
{
    return exit_status_;
}

void SolBridge::stop_input()
{
    input_active_ = false;
#ifdef _WIN32
    if (input_stop_event_ != nullptr) {
        SetEvent(static_cast<HANDLE>(input_stop_event_));
    }
#endif
}

void SolBridge::read_next_message()
{
    ws_.async_read(read_buffer_, [this](beast::error_code error, std::size_t) {
        if (error == websocket::error::closed) {
            set_exit_status(SolBridgeExitKind::RemoteClosed, "remote websocket closed cleanly");
            io_.stop();
            return;
        }
        if (error) {
            fail("WebSocket read failed", error);
            return;
        }

        const std::string data = beast::buffers_to_string(read_buffer_.data());
        if (debug_frames_) {
            std::cerr << "hisol: rx " << data.size()
                      << (ws_.got_text() ? " text" : " binary")
                      << " bytes\n";
        }
        std::cout.write(data.data(), static_cast<std::streamsize>(data.size()));
        std::cout.flush();
        read_buffer_.consume(read_buffer_.size());
        read_next_message();
    });
}

void SolBridge::queue_write(std::string chunk)
{
    if (closing_) {
        return;
    }

    const bool idle = write_queue_.empty();
    write_queue_.push_back(std::move(chunk));
    if (idle) {
        write_next_message();
    }
}

void SolBridge::write_next_message()
{
    if (write_queue_.empty()) {
        if (close_requested_) {
            close_now();
        }
        return;
    }

    ws_.text(true);
    if (debug_frames_) {
        std::cerr << "hisol: tx " << write_queue_.front().size() << " text bytes\n";
    }
    ws_.async_write(asio::buffer(write_queue_.front()), [this](beast::error_code error, std::size_t) {
        if (error) {
            fail("WebSocket write failed", error);
            return;
        }

        write_queue_.pop_front();
        write_next_message();
    });
}

void SolBridge::close_after_writes()
{
    close_requested_ = true;
    if (write_queue_.empty()) {
        close_now();
    }
}

void SolBridge::exit_local()
{
    set_exit_status(SolBridgeExitKind::LocalExit, "local Ctrl+] escape requested");
    close_after_writes();
}

void SolBridge::close_now()
{
    if (closing_) {
        return;
    }

    closing_ = true;
    ws_.async_close(websocket::close_code::normal, [this](beast::error_code error) {
        if (error && error != websocket::error::closed) {
            fail("WebSocket close failed", error);
            return;
        }
        io_.stop();
    });
}

void SolBridge::set_exit_status(SolBridgeExitKind kind, std::string message)
{
    if (exit_status_set_) {
        return;
    }

    exit_status_set_ = true;
    exit_status_.kind = kind;
    exit_status_.message = std::move(message);
}

void SolBridge::fail(std::string_view message, const beast::error_code& error)
{
    exit_code_ = EXIT_FAILURE;
    closing_ = true;
    set_exit_status(SolBridgeExitKind::Error, std::string(message) + ": " + error.message());
    io_.stop();
}

} // namespace hisol
