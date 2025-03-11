#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>
#include <atomic>
#include <websocketpp/config/asio.hpp>
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
            if (auto sp = hdl.lock()) {
                return hash<void*>()(sp.get());
            }
            return 0;
        }
    };
}

// ======== Latency Tracking Structures ========
struct LatencyMetrics {
    atomic<uint64_t> min{ULLONG_MAX};
    atomic<uint64_t> max{0};
    atomic<uint64_t> total{0};
    atomic<uint64_t> count{0};
    
    void record(uint64_t latency) noexcept {
        uint64_t current_min = min.load(std::memory_order_relaxed);
        while (latency < current_min && 
              !min.compare_exchange_weak(current_min, latency));
        
        uint64_t current_max = max.load(std::memory_order_relaxed);
        while (latency > current_max && 
              !max.compare_exchange_weak(current_max, latency));
        
        total.fetch_add(latency, std::memory_order_relaxed);
        count.fetch_add(1, std::memory_order_relaxed);
    }
};

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
            server_.run();
        }

        // ------ Latency Reporting ------
        void print_latency_stats() const {
            static mutex file_mutex;
            lock_guard<mutex> lock(file_mutex);
        
            ofstream file("../output.txt", ios::app);
            if (!file) return;
        
            // Write header if empty
            if (file.tellp() == 0) {
                file << "metric,min_us,max_us,avg_us,samples\n";
            }
        
            // Get timestamp

            time_t now = time(nullptr);
            struct tm tm_info {};
            struct tm* tm_ptr = localtime(&now);
            if(tm_ptr) tm_info = *tm_ptr;

            file << put_time(&tm_info, "%FT%T") << ",";
            
            for (const auto& [name, metric] : latency_stats) {
                const uint64_t count = metric.count.load(std::memory_order_relaxed);
                if (count == 0) continue;
        
                file << put_time(&tm, "%FT%T") << ","
                     << name << ","
                     << static_cast<uint64_t>(metric.min.load()) << ","
                     << static_cast<uint64_t>(metric.max.load()) << ","
                     << static_cast<uint64_t>(metric.total.load())/count << ","
                     << static_cast<uint64_t>(count) << "\n";
            }
        }

    private:
        // ------ Core Components ------
        server server_;
        mutex clients_mutex_;
        mutex subscriptions_mutex_;
        unordered_map<string, LatencyMetrics> latency_stats;

        unordered_map<string, 
            unordered_set<connection_hdl, hash<connection_hdl>,
                function<bool(const connection_hdl&, const connection_hdl&)>
            >> subscriptions;

        unordered_map<string, shared_ptr<client>> deribit_connections;
        unordered_map<uint64_t, chrono::high_resolution_clock::time_point> request_timestamps;
        mutex timestamp_mutex_;

        // ------ Latency Instrumentation ------
        void record_latency(const string& name, uint64_t latency) {
            lock_guard<mutex> lock(timestamp_mutex_);
            latency_stats[name].record(latency);
        }

        // ------ Connection Handlers ------
        void on_open(connection_hdl hdl) {
            lock_guard<mutex> lock(clients_mutex_);
            server_.send(hdl, "connected", websocketpp::frame::opcode::text);
        }

        void on_message(connection_hdl hdl, server::message_ptr msg) {
            const auto now = chrono::high_resolution_clock::now();
            const auto receive_time = chrono::time_point_cast<chrono::microseconds>(now);

            try {
                auto json_msg = json::parse(msg->get_payload());
                
                // Measure WebSocket propagation delay
                if (json_msg.contains("client_ts")) {
                    const auto client_send = json_msg["client_ts"].get<uint64_t>();
                    const auto server_recv = receive_time.time_since_epoch().count();
                    record_latency("ws_propagation", server_recv - client_send);
                    return;
                }

                if (json_msg["method"] == "subscribe" && json_msg.contains("symbol")) {
                    string symbol = json_msg["symbol"];
                    int depth = json_msg.value("depth", 5);
                    int timeout = max(1, json_msg.value("timeout", 5));
                    
                    {
                        lock_guard<mutex> sub_lock(subscriptions_mutex_);
                        subscriptions[symbol].insert(hdl);
                    }

                    if (!deribit_connections.count(symbol)) {
                        thread(&orderBookServer::connect_to_deribit, 
                             this, symbol, depth, timeout).detach();
                    }
                }
            } catch (const exception& e) {
                cerr << "JSON Error: " << e.what() << endl;
            }
        }

        void on_close(connection_hdl hdl) {
            lock_guard<mutex> lock(clients_mutex_);
            server_.send(hdl, "disconnected", websocketpp::frame::opcode::text);
        }

        // ------ Deribit Integration ------
        void connect_to_deribit(const string& symbol, int depth, int timeout) {
            auto deribit_client = make_shared<client>();
            deribit_client->init_asio();

            auto ctx = websocketpp::lib::make_shared<websocketpp::lib::asio::ssl::context>(
                websocketpp::lib::asio::ssl::context::sslv23
            );
            ctx->set_options(websocketpp::lib::asio::ssl::context::default_workarounds);
            ctx->set_verify_mode(websocketpp::lib::asio::ssl::verify_none);
            
            deribit_client->set_tls_init_handler([ctx](connection_hdl) {
                return ctx;
            });

            auto isConnected = make_shared<atomic<bool>>(true);

            deribit_client->set_message_handler(
            [this, isConnected, symbol](connection_hdl, client::message_ptr msg) {
                const auto processing_start = chrono::high_resolution_clock::now();
                
                // Measure market data processing latency
                auto json_msg = json::parse(msg->get_payload());
                const auto deribit_send = json_msg["usOut"].get<uint64_t>();
                const auto processing_end = chrono::high_resolution_clock::now();
                
                const auto processing_latency = chrono::duration_cast<chrono::microseconds>(
                    processing_end - processing_start).count();
                record_latency("data_processing", processing_latency);

                // Measure end-to-end latency
                const auto server_recv = chrono::time_point_cast<chrono::microseconds>(
                    processing_start).time_since_epoch().count();
                record_latency("e2e_latency", server_recv - deribit_send);

                broadcast_to_clients(symbol, msg->get_payload());
            });

            // Handle Deribit connection establishment
            deribit_client->set_open_handler(
            [this, symbol, depth, timeout, isConnected, deribit_client](connection_hdl ws_hdl) {
                thread([this, symbol, depth, timeout, ws_hdl, isConnected, deribit_client]() {
                    uint64_t request_id = 0;
                    while (*isConnected) {
                        json msg = {
                            {"jsonrpc", "2.0"},
                            {"id", request_id},
                            {"method", "public/get_order_book"},
                            {"params", {{"instrument_name", symbol}, {"depth", depth}}}
                        };

                        // Measure order placement latency
                        {
                            lock_guard<mutex> lock(timestamp_mutex_);
                            request_timestamps[request_id] = chrono::high_resolution_clock::now();
                        }

                        deribit_client->send(ws_hdl, msg.dump(), 
                                           websocketpp::frame::opcode::text);
                        
                        // Track request/response cycle
                        request_id = (request_id + 1) % 1000000;
                        this_thread::sleep_for(chrono::seconds(timeout));
                    }
                    deribit_client->close(ws_hdl, websocketpp::close::status::normal, "Connection closure");
                }).detach();
            });

            websocketpp::lib::error_code ec;
            auto con = deribit_client->get_connection("wss://test.deribit.com/ws/api/v2", ec);
            
            if (ec) {
                cerr << "Connection Error: " << ec.message() << endl;
                return;
            }

            {
                lock_guard<mutex> lock(subscriptions_mutex_);
                deribit_connections[symbol] = deribit_client;
            }

            deribit_client->connect(con);
            deribit_client->run();
        }

        // ------ Broadcast System ------
        void broadcast_to_clients(const string& symbol, const string& message) {
            const auto send_start = chrono::high_resolution_clock::now();
            
            {
                lock_guard<mutex> lock(subscriptions_mutex_);
                if (!subscriptions.count(symbol)) return;

                json msg = json::parse(message);
                msg["server_ts"] = chrono::time_point_cast<chrono::microseconds>(
                    send_start).time_since_epoch().count();

                for (auto& hdl : subscriptions[symbol]) {
                    server_.send(hdl, msg.dump(), websocketpp::frame::opcode::text);
                }
            }
            
            const auto send_duration = chrono::duration_cast<chrono::microseconds>(
                chrono::high_resolution_clock::now() - send_start).count();
            record_latency("broadcast_latency", send_duration);
        }
};