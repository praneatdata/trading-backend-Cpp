#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include "include/deribitApi.h"
#include "include/webServer.h"

using namespace std;
using json = nlohmann::json;
using namespace std::chrono; // Include for high-precision timing

int main() {
    tradeManager trader;

    // Read input from JSON file
    ifstream inputFile("../input.json"); // Update the location of the input file based on the relative path with respect to terminal dir
    if (!inputFile) {
        cerr << "Error: Unable to open input.json" << endl;
        return 1;
    }

    json input;
    inputFile >> input;
    inputFile.close();

    // Extract order details
    string instrument = input["instrument"];
    int quantity = input["quantity"];
    string orderType = input["order_type"];
    int buy_sell = input["buy_sell"];

    // Authentication
    json output;
    output["authentication_status"] = trader.authenticate();

    // Measure order placement latency
    auto start_time = high_resolution_clock::now(); // Start timer

    string response = trader.placeOrder(buy_sell, instrument, quantity, orderType);

    auto end_time = high_resolution_clock::now(); // End timer

    // Calculate latency in milliseconds
    auto latency = duration_cast<milliseconds>(end_time - start_time).count();
    output["order_placement_latency_ms"] = latency;

    // Parse response
    json orderResponse = json::parse(response);
    
    if (orderResponse.contains("result") && orderResponse["result"].contains("trades")) {
        output["order_id"] = orderResponse["result"]["trades"][0]["order_id"];
    } else {
        output["order_id"] = "N/A";
    }

    // Get Order Book
    output["order_book"] = json::parse(trader.getOrderBook(instrument, 1));

    // Get Active Positions
    output["positions"] = json::parse(trader.getPositions());

    // Write output to JSON file
    ofstream outputFile("../output.json"); // Update the location of the output file based on the relative path with respect to terminal dir
    if (!outputFile) {
        cerr << "Error: Unable to create output.json" << endl;
        return 1;
    }

    outputFile << output.dump(4); // Pretty print with 4 spaces
    outputFile.close();

    // Print latency to console
    cout << "Order Placement Latency: " << latency << " ms" << endl;

    cout << "Process completed. Output written to output.json" << endl;

    orderBookServer server;
    server.listen(8080);  // Start listening on port 8080
    server.run();
    
    return 0;
}