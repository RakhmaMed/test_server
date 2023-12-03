#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <cstdlib>
#include <iostream>

namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace beast = boost::beast;
namespace http = beast::http;

int http_request(net::io_context& ioc, std::string host, std::string port)
try
{
    tcp::resolver resolver(ioc);
    tcp::socket socket(ioc);

    auto const results = resolver.resolve(host, port);

    boost::asio::connect(socket, results.begin(), results.end());

    http::request<http::string_body> req{http::verb::get, "/", 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    http::write(socket, req);

    boost::beast::flat_buffer buffer;

    http::response<http::dynamic_body> res;

    http::read(socket, buffer, res);

    std::cout << res << std::endl;

    boost::system::error_code ec;
    socket.shutdown(tcp::socket::shutdown_both, ec);

    if(ec && ec != boost::system::errc::not_connected)
        throw boost::system::system_error{ec};

    return EXIT_SUCCESS;

} catch(std::exception const& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
}


net::awaitable<void> handle_session(tcp::socket socket) {
    for (;;) {
        beast::error_code ec;  // Declare error_code inside the coroutine
        beast::flat_buffer buffer;

        http::request<http::string_body> req;
        // The operation needs to co_await to wait for completion
        co_await http::async_read(socket, buffer, req, net::redirect_error(net::use_awaitable, ec));
        if (ec == http::error::end_of_stream)
            break;
        if (ec)
            co_return;

        // Handle the request here

        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "Hello, world!";
        res.prepare_payload();

        // The operation needs to co_await to wait for completion
        co_await http::async_write(socket, res, net::redirect_error(net::use_awaitable, ec));
        if (ec)
            co_return;
    }

    beast::error_code ec;  // Declare a new error_code for the shutdown operation
    socket.shutdown(tcp::socket::shutdown_send, ec);
    // Handle the shutdown error if needed
}

net::awaitable<void> listener(tcp::endpoint endpoint) {
    beast::error_code ec; // Declare error_code before use
    auto executor = co_await net::this_coro::executor;
    tcp::acceptor acceptor(executor, endpoint);

    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(net::redirect_error(net::use_awaitable, ec));
        if (ec)
            co_return;

        net::co_spawn(executor, handle_session(std::move(socket)), net::detached);
    }
}

int main(int argc, char* argv[]) {
    try {
        net::io_context ioc{1};
        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](auto, auto) { ioc.stop(); });

        net::co_spawn(ioc, listener({tcp::v4(), 8080}), net::detached);

        ioc.run();
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}