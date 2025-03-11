#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>
#include <websocketpp/config/asio.hpp>       // WebSocket++ ASIO integration
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/client.hpp>
#include <nlohmann/json.hpp>                // JSON parsing/manipulation

using namespace std;
using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
using json = nlohmann::json;

// WebSocket server/client types
using server = websocketpp::server<websocketpp::config::asio>;
using client = websocketpp::client<websocketpp::config::asio_tls_client>;

// Custom hash specialization for WebSocket++ connection handles
namespace std {
    template<>
    struct hash<websocketpp::connection_hdl> {
        size_t operator()(const websocketpp::connection_hdl& hdl) const {
            auto sp = hdl.lock();  // Convert weak_ptr to shared_ptr for hashing
            return hash<decltype(sp)>()(sp);
        }
    };
}
namespace std {
    template<>
    struct equal_to<websocketpp::connection_hdl> {
        bool operator()(const websocketpp::connection_hdl& lhs,
                       const websocketpp::connection_hdl& rhs) const {
            return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
        }
    };
}

// ======== orderBookServer Class ========
class orderBookServer {
    public:
        orderBookServer() {
            // Initialize server components
            server_.init_asio();
            
            // Register handler callbacks
            server_.set_open_handler(bind(&orderBookServer::on_open, this, _1));
            server_.set_message_handler(bind(&orderBookServer::on_message, this, _1, _2));
            server_.set_close_handler(bind(&orderBookServer::on_close, this, _1));
        }

        // ------ Public Interface ------
        void listen(uint16_t port) {
            cout << "Listening on PORT: " << port << endl;
            server_.listen(port);
            server_.start_accept();  // Begin accepting connections
        }

        void run() {
            cout << "WebSocket server started!" << endl;
            server_.run();  // Start the ASIO event loop
        }

    private:
        // ------ Core Components ------
        server server_;  // WebSocket server instance
        mutex clients_mutex_;       // Protects client connections
        mutex subscriptions_mutex_; // Protects subscription data

        // Subscription tracking: <Symbol, Connected Clients>
        unordered_map<string, 
        unordered_set<connection_hdl, 
        hash<connection_hdl>,
        equal_to<connection_hdl>  // Explicit equal_to specification
        >> subscriptions;
        unordered_map<connection_hdl, unordered_set<string>, 
        hash<connection_hdl>, 
        equal_to<connection_hdl>  // Add equality comparison
        > connection_symbols;

        // Active Deribit connections: <Symbol, WebSocket Client>
        unordered_map<string, shared_ptr<client>> deribit_connections;

        // ------ Connection Handlers ------
        void on_open(connection_hdl hdl) {
            lock_guard<mutex> lock(clients_mutex_);
            connection_symbols[hdl] = {};
        }

        void on_message(connection_hdl hdl, server::message_ptr msg) {
            lock_guard<mutex> lock(clients_mutex_);
            string payload = msg->get_payload();

            try {
                auto json_msg = json::parse(payload);
                
                // Handle subscription requests
                if (json_msg["method"] == "subscribe" && json_msg.contains("symbol")) {
                    string symbol = json_msg["symbol"];
                    int depth = json_msg.value("depth", 5);       // Default 5 levels
                    int timeout = max(1, json_msg.value("timeout", 5));  // Min 1 sec
                    
                    cout << "New subscription to " << symbol << endl;
                    {
                        lock_guard<mutex> sub_lock(subscriptions_mutex_);
                        subscriptions[symbol].insert(hdl);  // Add client to symbol group
                    }

                    // Create Deribit connection if first subscriber
                    if (!deribit_connections.count(symbol)) {
                        thread(&orderBookServer::connect_to_deribit, 
                             this, symbol, depth, timeout).detach();
                    }
                }
                else if (json_msg["method"] == "unsubscribe" && json_msg.contains("symbol")) {
                    string symbol = json_msg["symbol"];
                    {
                        lock_guard<mutex> sub_lock(subscriptions_mutex_);
                        if (subscriptions.count(symbol)) {
                            subscriptions[symbol].erase(hdl);
                            
                            // Remove symbol entry if no subscribers left
                            if (subscriptions[symbol].empty()) {
                                subscriptions.erase(symbol);
                                
                                // Cleanup Deribit connection
                                if (deribit_connections.count(symbol)) {
                                    deribit_connections[symbol]->stop();
                                    deribit_connections.erase(symbol);
                                }
                            }
                        }
                    }
                    cout << "Unsubscribed from " << symbol << endl;
                }
            } catch (const exception& e) {
                cerr << "JSON Error: " << e.what() << endl;
            }
        }

        void on_close(connection_hdl hdl) {
            lock_guard<mutex> lock(clients_mutex_);
            // Cleanup all subscriptions for disconnected client
            for (auto& symbol : connection_symbols[hdl]) {
                subscriptions[symbol].erase(hdl);
            }
            connection_symbols.erase(hdl);
        }

        // ------ Deribit Integration ------
        void connect_to_deribit(const string& symbol, int depth, int timeout) {
            shared_ptr<client> deribit_client = make_shared<client>();
            deribit_client->init_asio();

            // Configure SSL context for WSS connection
            auto ctx = websocketpp::lib::make_shared<websocketpp::lib::asio::ssl::context>(
                websocketpp::lib::asio::ssl::context::sslv23
            );
            ctx->set_options(websocketpp::lib::asio::ssl::context::default_workarounds);
            ctx->set_verify_mode(websocketpp::lib::asio::ssl::verify_none);
            
            deribit_client->set_tls_init_handler([ctx](connection_hdl) {
                return ctx;
            });

            auto isConnected = make_shared<bool>(true);

            // Handle incoming Deribit messages
            deribit_client->set_message_handler(
            [this, isConnected, symbol](connection_hdl, client::message_ptr msg) {
                broadcast_to_clients(symbol, msg->get_payload());
            });

            // Handle Deribit connection establishment
            deribit_client->set_open_handler(
            [this, symbol, depth, timeout, isConnected, deribit_client](connection_hdl ws_hdl) {
                thread([this, symbol, depth, timeout, ws_hdl, isConnected, deribit_client]() {
                    // Regular order book polling
                    while (*isConnected) {
                        json msg = {
                            {"jsonrpc", "2.0"},
                            {"method", "public/get_order_book"},
                            {"params", {
                                {"instrument_name", symbol}, 
                                {"depth", depth}
                            }}
                        };
                        deribit_client->send(ws_hdl, msg.dump(), 
                                           websocketpp::frame::opcode::text);
                        this_thread::sleep_for(chrono::seconds(timeout));
                    }
                    // Cleanup closed connection
                    deribit_client->close(ws_hdl, 
                        websocketpp::close::status::normal, 
                        "Connection closure");
                }).detach();
            });

            // Establish Deribit connection
            websocketpp::lib::error_code ec;
            auto con = deribit_client->get_connection(
                "wss://test.deribit.com/ws/api/v2", ec);
            
            if (ec) {
                cerr << "Connection Error: " << ec.message() << endl;
                return;
            }

            {
                lock_guard<mutex> lock(subscriptions_mutex_);
                deribit_connections[symbol] = deribit_client;
            }

            deribit_client->connect(con);
            deribit_client->run();  // Start Deribit client event loop
        }

        // ------ Broadcast System ------
        void broadcast_to_clients(const string& symbol, const string& message) {
            lock_guard<mutex> lock(subscriptions_mutex_);
            
            if (!subscriptions.count(symbol)) return;

            // Send to all subscribed clients
            for (auto& hdl : subscriptions[symbol]) {
                server_.send(hdl, message, websocketpp::frame::opcode::text);
            }
        }
};