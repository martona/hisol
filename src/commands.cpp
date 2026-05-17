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
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace hisol {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace ssl = asio::ssl;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

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

    ConsoleModeGuard console_mode;
    auto bridge = std::make_shared<SolBridge>(io, ws, options.debug_frames);
    bridge->start();
    std::thread input_thread([bridge] {
        bridge->read_stdin();
    });
    input_thread.detach();

    io.run();
    bridge->stop_input();
    return bridge->exit_code();
}

} // namespace hisol
