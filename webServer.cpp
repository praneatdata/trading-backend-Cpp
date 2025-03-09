#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <mutex>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
namespace ssl = boost::asio::ssl;
namespace json = boost::json;
using tcp = boost::asio::ip::tcp;
using namespace std;

class OrderBookServer {
public:
    OrderBookServer(asio::io_context& ioc, uint16_t port)
        : acceptor_(ioc, tcp::endpoint(tcp::v4(), port)), socket_(ioc) {
        accept();
    }

    void run() {
        cout << "WebSocket server started on port " << acceptor_.local_endpoint().port() << endl;
    }

private:
    tcp::acceptor acceptor_;
    tcp::socket socket_;
    unordered_map<tcp::socket*, thread> clients_;
    mutex clients_mutex_;

    void accept() {
        acceptor_.async_accept(socket_, [this](beast::error_code ec) {
            if (!ec) {
                cout << "Client connected!" << endl;
                handle_client(move(socket_));
            }
            accept(); // Accept next connection
        });
    }

    void handle_client(tcp::socket socket) {
        auto ws = make_shared<websocket::stream<tcp::socket>>(move(socket));
        ws->async_accept([this, ws](beast::error_code ec) {
            if (!ec) {
                read_message(ws);
            }
        });
    }

    void read_message(shared_ptr<websocket::stream<tcp::socket>> ws) {
        auto buffer = make_shared<beast::flat_buffer>();
        ws->async_read(*buffer, [this, ws, buffer](beast::error_code ec, size_t) {
            if (!ec) {
                string msg = beast::buffers_to_string(buffer->data());
                handle_message(ws, msg);
                read_message(ws); // Read next message
            } else {
                cout << "Client disconnected" << endl;
            }
        });
    }

    void handle_message(shared_ptr<websocket::stream<tcp::socket>> ws, const string& msg) {
        try {
            json::value parsed = json::parse(msg);
            if (parsed.as_object().contains("method") && parsed.as_object().at("method").as_string() == "subscribe") {
                string symbol = parsed.as_object().at("symbol").as_string().c_str();
                int depth = parsed.as_object().contains("depth") ? parsed.as_object().at("depth").to_number<int>() : 5;
                int timeout = parsed.as_object().contains("timeout") ? max(1, parsed.as_object().at("timeout").to_number<int>()) : 5;

                cout << "Client subscribed to " << symbol << endl;
                thread(&OrderBookServer::fetch_order_book, this, ws, symbol, depth, timeout).detach();
            }
        } catch (const exception& e) {
            cerr << "Error parsing JSON message: " << e.what() << endl;
        }
    }

    void fetch_order_book(shared_ptr<websocket::stream<tcp::socket>> ws, string symbol, int depth, int timeout) {
        try {
            asio::io_context ioc;
            ssl::context ctx(ssl::context::tlsv12_client);
            tcp::resolver resolver(ioc);
            websocket::stream<ssl::stream<tcp::socket>> ws_client(ioc, ctx);

            auto const results = resolver.resolve("test.deribit.com", "443");
            asio::connect(ws_client.next_layer().next_layer(), results);
            ws_client.next_layer().handshake(ssl::stream_base::client);
            ws_client.handshake("test.deribit.com", "/ws/api/v2");

            while (true) {
                json::value request = {
                    {"jsonrpc", "2.0"},
                    {"method", "public/get_order_book"},
                    {"params", {{"instrument_name", symbol}, {"depth", depth}}}
                };

                ws_client.write(asio::buffer(request.as_string()));
                beast::flat_buffer buffer;
                ws_client.read(buffer);

                string response = beast::buffers_to_string(buffer.data());
                ws->write(asio::buffer(response));

                this_thread::sleep_for(chrono::seconds(timeout));
            }
        } catch (const exception& e) {
            cerr << "Error fetching order book: " << e.what() << endl;
        }
    }
};

int main() {
    try {
        asio::io_context ioc;
        OrderBookServer server(ioc, 8080);
        server.run();
        ioc.run();
    } catch (const exception& e) {
        cerr << "Server error: " << e.what() << endl;
    }
    return 0;
}