#include <iostream>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>
#include <websocketpp/config/asio.hpp> 
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/client.hpp>
#include <nlohmann/json.hpp>

using namespace std;
using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
using json = nlohmann::json;
using server = websocketpp::server<websocketpp::config::asio>;
using client = websocketpp::client<websocketpp::config::asio_tls_client>;

class orderBookServer {
    public:
        orderBookServer() {
            server_.init_asio();
            server_.set_open_handler(bind(&orderBookServer::on_open, this, _1));
            server_.set_message_handler(bind(&orderBookServer::on_message, this, _1, _2));
            server_.set_close_handler(bind(&orderBookServer::on_close, this, _1));
        }

        void listen(uint16_t port) {
            cout << "listening on PORT: " << port << endl;
            server_.listen(port);
            server_.start_accept();
        }

        void run() {
            cout << "web socket server started !!" << endl;
            server_.run();
        }

    private:
        server server_;
        mutex clients_mutex_;

        void on_open(connection_hdl hdl) {
            lock_guard<mutex> lock(clients_mutex_);
            cout << "Client connected" << endl;
        }

        void on_message(connection_hdl hdl, server::message_ptr msg) {
            lock_guard<mutex> lock(clients_mutex_);
            string payload = msg->get_payload();

            try {
                auto json_msg = json::parse(payload);
                if (json_msg["method"] == "subscribe" && json_msg.contains("symbol")) {
                    string symbol = json_msg["symbol"];
                    int depth = (json_msg.contains("depth")) ? json_msg["depth"].get<int>() : 5;                 // default - 5 entries
                    int timeout = (json_msg.contains("timeout")) ? max(1, json_msg["timeout"].get<int>()) : 5;   // default - 5 seconds and should be 1 sec atleast
                    cout << "Client subscribed to " << symbol << std::endl;

                    thread(&orderBookServer::send_data, this, hdl, symbol, depth, timeout).detach();
                }
            } catch (const exception& e) {
                cerr << "Error parsing JSON message: " << e.what() << endl;
            }
        }

        void on_close(connection_hdl hdl) {
            lock_guard<mutex> lock(clients_mutex_);
            cout << "Client disconnected" << endl;
        }

        void send_data(connection_hdl hdl, const string& symbol, const int& depth, const int& timeout){
            client deribit_client;
            string base_url = "wss://test.deribit.com/ws/api/v2";
            deribit_client.init_asio();
            
            // setting up ssl for wss connections
            websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> ctx = websocketpp::lib::make_shared<websocketpp::lib::asio::ssl::context>(websocketpp::lib::asio::ssl::context::sslv23);
            ctx->set_options(websocketpp::lib::asio::ssl::context::default_workarounds);
            ctx->set_verify_mode(websocketpp::lib::asio::ssl::verify_none); 
            deribit_client.set_tls_init_handler([ctx](websocketpp::connection_hdl) {
                return ctx;
            });

            auto isConnected = make_shared<bool>(true);

            deribit_client.set_message_handler([this, isConnected, hdl](websocketpp::connection_hdl ws_hdl, client::message_ptr msg){
                string rcvd_data = msg->get_payload();
                try {
                    server_.send(hdl, rcvd_data, websocketpp::frame::opcode::text);
                } catch (const websocketpp::exception& e){
                    *isConnected = false;
                    cerr << "Error forwarding data to client - DESUBSCRIBING: " << e.what() << endl;
                }
            });
            
            deribit_client.set_open_handler([this, symbol, depth, timeout, isConnected, &deribit_client](websocketpp::connection_hdl ws_hdl){
                thread([this, symbol, depth, timeout, ws_hdl, isConnected, &deribit_client]() {
                    while (*isConnected) {                        
                        json msg = {
                            {"jsonrpc", "2.0"},
                            {"method", "public/get_order_book"},
                            {"params", {{"instrument_name", symbol}, {"depth", depth}}}
                        };
                        deribit_client.send(ws_hdl, msg.dump(), websocketpp::frame::opcode::text);
                        this_thread::sleep_for(chrono::seconds(timeout)); // Sleep for {timeout} / {default: 5} seconds
                    }
                    deribit_client.close(ws_hdl, websocketpp::close::status::normal, "Closing Deribit connection due to client disconnection.");
                }).detach();  // in order to run independently 
            });

            websocketpp::lib::error_code ec;
            auto con = deribit_client.get_connection(base_url, ec);
            if (ec) {
                cerr << "Error initiating WebSocket connection: " << ec.message() << endl;
                return;
            }

            deribit_client.connect(con);
            deribit_client.run();
        }
};

int main() {
    orderBookServer server;
    server.listen(8080);
    server.run();

    return 0;
}