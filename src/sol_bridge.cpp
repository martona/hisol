#include "sol_bridge.hpp"

#include "stdio.hpp"

#include <boost/asio/post.hpp>
#include <boost/beast/core/buffers_to_string.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace hisol {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;

SolBridge::SolBridge(asio::io_context& io, SolWebSocketStream& ws, bool debug_frames)
    : io_(io), ws_(ws), debug_frames_(debug_frames)
{
}

void SolBridge::start()
{
    set_stdout_binary();
    set_stdin_binary();
    read_next_message();
}

void SolBridge::read_stdin()
{
    char ch = 0;
    bool drop_next_lf = false;
    while (input_active_) {
        if (!std::cin.get(ch)) {
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

        std::string chunk(1, ch);
        asio::post(io_, [self = shared_from_this(), chunk = std::move(chunk)]() mutable {
            self->queue_write(std::move(chunk));
        });
    }
}

int SolBridge::exit_code() const
{
    return exit_code_;
}

void SolBridge::stop_input()
{
    input_active_ = false;
}

void SolBridge::read_next_message()
{
    ws_.async_read(read_buffer_, [this](beast::error_code error, std::size_t) {
        if (error == websocket::error::closed) {
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

void SolBridge::fail(std::string_view message, const beast::error_code& error)
{
    exit_code_ = EXIT_FAILURE;
    closing_ = true;
    std::cerr << "hisol: " << message << ": " << error.message() << '\n';
    io_.stop();
}

} // namespace hisol
