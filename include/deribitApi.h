#include <unordered_map>
#include <ctime>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include "utils.h"

using namespace std;
using json = nlohmann::json;

// Constants for timeout and token refresh settings
const long long DEFAULT_TIMEOUT_MS = 10000;               // 10 seconds
const long long TOKEN_REFRESH_OFFSET_S = 60;              // 60 seconds before expiration
const string ENV_FIlE = ".env";                           // Environment file to read credentials

// TradeManager Class: Manages authentication, token verification, and trading operations
class tradeManager {
    private:
        string authToken = "";       // Authentication token
        long long expiresOn = 0;     // Expiration time of the authentication token

    public:
        // Method to authenticate and generate a new authentication token
        bool authenticate() {
            // Read environment variables from the file
            unordered_map<string, string> env = readEnv(ENV_FIlE);

            // Ensure that the necessary credentials are available
            if (!env.count("DERIBIT_CLIENT_ID") || !env.count("DERIBIT_CLIENT_SECRET")) {
                cerr << "Please create a '" << ENV_FIlE << "' file and add 'DERIBIT_CLIENT_ID' and 'DERIBIT_CLIENT_SECRET' to it." << endl;
                return false;
            }

            const string CLIENT_ID = env["DERIBIT_CLIENT_ID"];
            const string CLIENT_SECRET = env["DERIBIT_CLIENT_SECRET"];

            // Prepare the request payload for authentication
            string req = "POST";
            string url = "https://test.deribit.com/api/v2/public/auth";
            string payload = R"({
                "method": "public/auth",
                "params": {
                    "grant_type": "client_credentials",
                    "client_id": ")" + CLIENT_ID + R"(",
                    "client_secret": ")" + CLIENT_SECRET + R"("
                }
            })";

            long long startTime = (long long)(time(0));  // Get the current time
            string response = sendRequest(req, DEFAULT_TIMEOUT_MS, url, payload);  // Send authentication request

            // Parse the response JSON and extract the token information
            auto parsed = json::parse(response);
            if (parsed.contains("result") && parsed["result"].contains("access_token") && parsed["result"].contains("expires_in")) {
                authToken = "Bearer " + parsed["result"]["access_token"].get<string>();  // Store the access token
                expiresOn = startTime + (long long)(parsed["result"]["expires_in"]);      // Calculate expiration time
                // cout << "Authorization: " << authToken << endl;  // Uncomment the line to get bearer token
                cout << "Token expires in - " << (long long)(parsed["result"]["expires_in"]) << " seconds" << endl;
                return true;  // Successful authentication
            }
            else {
                cerr << "Authorization Failed. Response: " << response << endl;
                return false;  // Authentication failed
            }
        }

        // Method to verify the token and refresh if it's about to expire
        bool verifyToken() {
            if (((long long)(time(0)) + TOKEN_REFRESH_OFFSET_S) <= expiresOn) {
                return true;  // Token is still valid, no need to refresh
            }

            cout << "Authorization Expired: Trying to Refresh" << endl;
            return authenticate();  // Refresh the token
        }

        // --- TRADING FUNCTIONS BEGIN HERE ---

        // A. Method to place an order (buy or sell)
        string placeOrder(int buy, string symbol, double amount, string type = "market") {
            string req = "POST";
            string method = (buy) ? "private/buy" : "private/sell";  // Determine whether it's a buy or sell
            string url = "https://test.deribit.com/api/v2/" + method;

            // Prepare the payload for the order request
            string payload = R"({
                "method": ")" + method + R"(",
                "params": {
                    "instrument_name": ")" + symbol + R"(",
                    "amount": )" + to_string(amount) + R"(, 
                    "type": ")" + type + R"(" 
                }
            })";

            // Verify the token and send the order request
            if (verifyToken()) {
                return sendRequest(req, DEFAULT_TIMEOUT_MS, url, payload, authToken);  // Send request with the authentication token
            }
            return "Authorization Failed";  // Token verification failed
        }

        // B. Method to cancel an existing order
        string cancelOrder(string order_id) {
            string req = "POST";
            string url = "https://test.deribit.com/api/v2/private/cancel";

            // Prepare the payload for the cancel order request
            string payload = R"({
                "method": "private/cancel",
                "params": {
                    "order_id": ")" + order_id + R"("
                }
            })";

            // Verify the token and send the cancel order request
            if (verifyToken()) {
                return sendRequest(req, DEFAULT_TIMEOUT_MS, url, payload, authToken);
            }
            return "Authorization Failed";  // Token verification failed
        }

        // C. Method to modify an existing order
        string modifyOrder(string order_id, double amount) {
            string req = "POST";
            string url = "https://test.deribit.com/api/v2/private/edit";

            // Prepare the payload for modifying the order
            string payload = R"({
                "method": "private/cancel",
                "params": {
                    "order_id": ")" + order_id + R"(",
                    "amount": )" + to_string(amount) + R"(
                }
            })";

            // Verify the token and send the modify order request
            if (verifyToken()) {
                return sendRequest(req, DEFAULT_TIMEOUT_MS, url, payload, authToken);
            }
            return "Authorization Failed";  // Token verification failed
        }

        // D. Method to get the order book for a given symbol
        string getOrderBook(string symbol, long long depth = 0) {
            string req = "POST";
            string url = "https://test.deribit.com/api/v2/public/get_order_book";

            // Prepare the payload for the order book request
            string payload = R"({
                "method": "public/get_order_book",
                "params": {
                    "instrument_name": ")" + symbol + R"(")" +
                    ((depth > 0) ? R"(,"depth": )" + to_string(depth) : "") + R"(
                }
            })";

            // Send the request to get the order book
            return sendRequest(req, DEFAULT_TIMEOUT_MS, url, payload);
        }

        // E. Method to get all the positions
        string getPositions() {
            string req = "POST";
            string url = "https://test.deribit.com/api/v2/private/get_positions";

            // Prepare the payload for the get positions request
            string payload = R"({
                "method": "private/get_positions",
                "params": {}
            })";

            // Verify the token and send the request for positions
            if (verifyToken()) {
                return sendRequest(req, DEFAULT_TIMEOUT_MS, url, payload, authToken);
            }
            return "Authorization Failed";  // Token verification failed
        }
};