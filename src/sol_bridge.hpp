#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <string_view>

namespace hisol {

using SolWebSocketStream =
    boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;

enum class SolBridgeExitKind {
    RemoteClosed,
    LocalExit,
    InputEnded,
    Error,
};

struct SolBridgeExitStatus {
    SolBridgeExitKind kind = SolBridgeExitKind::RemoteClosed;
    std::string message = "remote websocket closed";
};

class SolBridge : public std::enable_shared_from_this<SolBridge> {
public:
    SolBridge(boost::asio::io_context& io, SolWebSocketStream& ws, bool debug_frames);

    void start();
    void read_stdin();
    [[nodiscard]] int exit_code() const;
    [[nodiscard]] SolBridgeExitStatus exit_status() const;
    void stop_input();

private:
    void read_next_message();
    void queue_write(std::string chunk);
    void write_next_message();
    void close_after_writes();
    void exit_local();
    void close_now();
    void set_exit_status(SolBridgeExitKind kind, std::string message);
    void fail(std::string_view message, const boost::beast::error_code& error);

    boost::asio::io_context& io_;
    SolWebSocketStream& ws_;
    boost::beast::flat_buffer read_buffer_;
    std::deque<std::string> write_queue_;
    bool debug_frames_ = false;
    bool close_requested_ = false;
    bool closing_ = false;
    std::atomic_bool input_active_ = true;
    SolBridgeExitStatus exit_status_;
    int exit_code_ = 0;
};

} // namespace hisol
