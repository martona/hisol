#include "commands.hpp"

#include "app_info.hpp"
#include "console.hpp"
#include "http_client.hpp"
#include "sol_bridge.hpp"
#include "sol_session.hpp"
#include "stdio.hpp"
#include "tls.hpp"
#include "trace.hpp"
#include "url.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/version.hpp>

#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hisol {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace ssl = asio::ssl;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {

struct SolRunResult {
    int exit_code = EXIT_SUCCESS;
    SolBridgeExitStatus status;
};

std::string timestamp_now()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif

    std::ostringstream out;
    out << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

void warn_if_console_too_small()
{
    const auto size = current_console_size();
    if (!size.has_value()) {
        return;
    }

    if (size->columns < 80 || size->rows < 25) {
        std::cerr << "hisol: warning: local terminal is "
                  << size->columns << "x" << size->rows
                  << "; BIOS/SOL screens usually expect at least 80x25\n";
    }
}

void log_disconnect_status(const SolBridgeExitStatus& status)
{
    std::cerr << "hisol: [" << timestamp_now() << "] disconnected\n";
    std::cerr << "hisol: status: " << status.message << '\n';
}

bool is_prompt_exit_key(
#ifdef _WIN32
    const KEY_EVENT_RECORD& key
#else
    char ch
#endif
)
{
#ifdef _WIN32
    const bool ctrl_pressed = (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    return key.bKeyDown && ctrl_pressed && (key.uChar.UnicodeChar == L'\x1d' || key.wVirtualKeyCode == VK_OEM_6);
#else
    return ch == '\x1d';
#endif
}

bool prompt_reconnect_or_exit()
{
    std::cerr << "hisol: Press Enter to reconnect or Ctrl+] to exit.\n";

#ifdef _WIN32
    const HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (stdin_handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    while (true) {
        INPUT_RECORD record{};
        DWORD records_read = 0;
        if (!ReadConsoleInputW(stdin_handle, &record, 1, &records_read) || records_read == 0) {
            continue;
        }
        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
            continue;
        }

        if (record.Event.KeyEvent.wVirtualKeyCode == VK_RETURN) {
            return true;
        }
        if (is_prompt_exit_key(record.Event.KeyEvent)) {
            return false;
        }
    }
#else
    char ch = 0;
    while (std::cin.get(ch)) {
        if (ch == '\r' || ch == '\n') {
            return true;
        }
        if (is_prompt_exit_key(ch)) {
            return false;
        }
    }
    return false;
#endif
}

bool should_prompt_after_bridge(const SolBridgeExitStatus& status)
{
    return status.kind == SolBridgeExitKind::RemoteClosed || status.kind == SolBridgeExitKind::Error;
}

SolRunResult run_sol_once(const SolOptions& options)
{
    SolSession session = open_sol_session(options);

    asio::io_context io;
    ssl::context tls_context(ssl::context::tls_client);
    tcp::resolver resolver(io);
    SolWebSocketStream ws(io, tls_context);

    configure_tls(tls_context, ws.next_layer(), options.base_url.host, options.insecure);
    set_server_name_indication(ws.next_layer(), options.base_url.host);

    const auto endpoints = resolver.resolve(options.base_url.host, options.base_url.port);
    beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(30));
    beast::get_lowest_layer(ws).connect(endpoints);
    ws.next_layer().handshake(ssl::stream_base::client);
    beast::get_lowest_layer(ws).expires_never();

    ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    ws.set_option(websocket::stream_base::decorator([&](websocket::request_type& request) {
        request.set(http::field::user_agent, "hisol/" + std::string(BOOST_LIB_VERSION));
        request.set(http::field::origin, make_origin(options.base_url));
        request.set(http::field::cookie, session.cookies.header());
    }));

    std::cerr << "hisol: logged in; connecting to wss://" << make_host_header(options.base_url) << "/sol\n";
    log_websocket_request(options.base_url, session.cookies, options.verbose);
    websocket::response_type handshake_response;
    ws.handshake(handshake_response, make_host_header(options.base_url), "/sol");
    log_websocket_response(handshake_response, options.verbose);
    std::cerr << "hisol: connected; bridging stdin/stdout\n";
    std::cerr << "hisol: local escape: Ctrl+] exits the bridge; Ctrl+C is sent to the remote console\n";
    warn_if_console_too_small();

    ConsoleModeGuard console_mode;
    auto bridge = std::make_shared<SolBridge>(io, ws, options.debug_frames);
    bridge->start();
    std::thread input_thread([bridge] {
        bridge->read_stdin();
    });

    io.run();
    bridge->stop_input();
#ifdef _WIN32
    if (input_thread.joinable()) {
        input_thread.join();
    }
#else
    if (input_thread.joinable()) {
        input_thread.detach();
    }
#endif

    return SolRunResult{bridge->exit_code(), bridge->exit_status()};
}

} // namespace

int run_get(const GetOptions& options)
{
    const std::vector<Header> headers{
        Header{http::field::accept, {}, "*/*"},
    };

    auto response = https_request(
        options.url,
        options.insecure,
        http::verb::get,
        options.url.target,
        {},
        {},
        nullptr,
        headers,
        options.verbose);

    if (options.include_headers) {
        std::cout << response.base() << "\r\n";
    }

    const std::string body = decode_response_body(response);
    set_stdout_binary();
    std::cout.write(body.data(), static_cast<std::streamsize>(body.size()));

    return EXIT_SUCCESS;
}

int run_sol(const SolOptions& options)
{
    while (true) {
        SolRunResult result = run_sol_once(options);
        if (!should_prompt_after_bridge(result.status)) {
            return result.exit_code;
        }

        log_disconnect_status(result.status);
        if (!prompt_reconnect_or_exit()) {
            return result.exit_code;
        }
    }
}

} // namespace hisol
