#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/version.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

namespace {

constexpr std::string_view kName = "hisol";
constexpr std::string_view kExpansion = "HTTPS IPMI serial-over-LAN";

struct Url {
    std::string host;
    std::string port;
    std::string target;
};

struct GetOptions {
    Url url;
    bool include_headers = false;
    bool insecure = false;
};

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string strip_brackets(std::string_view host)
{
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        return std::string(host.substr(1, host.size() - 2));
    }

    return std::string(host);
}

Url parse_https_url(std::string_view raw_url)
{
    constexpr std::string_view scheme = "https://";
    if (!starts_with(raw_url, scheme)) {
        throw std::invalid_argument("expected an https:// URL");
    }

    std::string_view rest = raw_url.substr(scheme.size());
    const auto target_start = rest.find_first_of("/?");
    std::string_view authority = rest.substr(0, target_start);
    std::string_view target_part =
        target_start == std::string_view::npos ? std::string_view{} : rest.substr(target_start);

    if (authority.empty()) {
        throw std::invalid_argument("URL host is empty");
    }

    if (authority.find('@') != std::string_view::npos) {
        throw std::invalid_argument("URL userinfo is not supported");
    }

    std::string_view host = authority;
    std::string_view port = "443";

    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) {
            throw std::invalid_argument("IPv6 address is missing ']'");
        }

        host = authority.substr(0, close + 1);
        const auto suffix = authority.substr(close + 1);
        if (!suffix.empty()) {
            if (suffix.front() != ':' || suffix.size() == 1) {
                throw std::invalid_argument("invalid port after IPv6 address");
            }
            port = suffix.substr(1);
        }
    } else {
        const auto first_colon = authority.find(':');
        const auto last_colon = authority.rfind(':');
        if (first_colon != last_colon) {
            throw std::invalid_argument("IPv6 addresses must be wrapped in []");
        }

        if (last_colon != std::string_view::npos) {
            if (last_colon == authority.size() - 1) {
                throw std::invalid_argument("URL port is empty");
            }
            host = authority.substr(0, last_colon);
            port = authority.substr(last_colon + 1);
        }
    }

    if (host.empty()) {
        throw std::invalid_argument("URL host is empty");
    }

    std::string target = target_part.empty() ? "/" : std::string(target_part);
    if (target.front() == '?') {
        target.insert(target.begin(), '/');
    }

    return Url{strip_brackets(host), std::string(port), std::move(target)};
}

void print_usage(std::ostream& out)
{
    out << kName << ": " << kExpansion << '\n';
    out << "Boost " << BOOST_LIB_VERSION << " is wired in.\n\n";
    out << "Usage:\n";
    out << "  hisol get [--include] [--insecure] https://host[:port][/path]\n\n";
    out << "Options:\n";
    out << "  --include   Write HTTP status and headers before the body.\n";
    out << "  --insecure  Disable certificate and hostname verification.\n";
}

GetOptions parse_get_options(int argc, char* argv[])
{
    GetOptions options;
    bool saw_url = false;

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--include" || arg == "-i") {
            options.include_headers = true;
        } else if (arg == "--insecure" || arg == "-k") {
            options.insecure = true;
        } else if (!saw_url) {
            options.url = parse_https_url(arg);
            saw_url = true;
        } else {
            throw std::invalid_argument("unexpected argument: " + std::string(arg));
        }
    }

    if (!saw_url) {
        throw std::invalid_argument("missing URL");
    }

    return options;
}

void configure_tls(ssl::context& context, beast::ssl_stream<beast::tcp_stream>& stream, const GetOptions& options)
{
    if (options.insecure) {
        stream.set_verify_mode(ssl::verify_none);
        return;
    }

    context.set_default_verify_paths();
#ifdef _WIN32
    if (SSL_CTX_load_verify_store(context.native_handle(), "org.openssl.winstore:") != 1) {
        const auto ssl_error = static_cast<int>(::ERR_get_error());
        throw beast::system_error(
            beast::error_code(ssl_error, asio::error::get_ssl_category()),
            "failed to load Windows certificate store");
    }
#endif
    stream.set_verify_mode(ssl::verify_peer);
    stream.set_verify_callback(ssl::host_name_verification(options.url.host));
}

std::string make_host_header(const Url& url)
{
    std::string host = url.host.find(':') == std::string::npos ? url.host : "[" + url.host + "]";
    if (url.port != "443") {
        host += ":" + url.port;
    }

    return host;
}

void set_server_name_indication(beast::ssl_stream<beast::tcp_stream>& stream, const std::string& host)
{
    if (SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()) != 1) {
        const auto ssl_error = static_cast<int>(::ERR_get_error());
        throw beast::system_error(
            beast::error_code(ssl_error, asio::error::get_ssl_category()),
            "failed to set TLS server name indication");
    }
}

int run_get(const GetOptions& options)
{
    asio::io_context io;
    ssl::context tls_context(ssl::context::tls_client);
    tcp::resolver resolver(io);
    beast::ssl_stream<beast::tcp_stream> stream(io, tls_context);

    configure_tls(tls_context, stream, options);
    set_server_name_indication(stream, options.url.host);

    const auto endpoints = resolver.resolve(options.url.host, options.url.port);
    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));
    beast::get_lowest_layer(stream).connect(endpoints);
    stream.handshake(ssl::stream_base::client);

    http::request<http::empty_body> request{http::verb::get, options.url.target, 11};
    request.set(http::field::host, make_host_header(options.url));
    request.set(http::field::user_agent, "hisol/" + std::string(BOOST_LIB_VERSION));
    request.set(http::field::accept, "*/*");

    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    if (options.include_headers) {
        std::cout << response.base() << "\r\n";
    }
    std::cout << response.body();

    beast::error_code shutdown_error;
    stream.shutdown(shutdown_error);
    if (shutdown_error == asio::error::eof ||
        shutdown_error == ssl::error::stream_truncated) {
        shutdown_error = {};
    }
    if (shutdown_error) {
        throw beast::system_error(shutdown_error, "TLS shutdown failed");
    }

    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        if (argc == 1 || std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h") {
            print_usage(std::cout);
            return EXIT_SUCCESS;
        }

        if (std::string_view(argv[1]) == "get") {
            return run_get(parse_get_options(argc, argv));
        }

        throw std::invalid_argument("unknown command: " + std::string(argv[1]));
    } catch (const std::exception& ex) {
        std::cerr << "hisol: " << ex.what() << '\n';
        std::cerr << "Try 'hisol --help'.\n";
        return EXIT_FAILURE;
    }
}
