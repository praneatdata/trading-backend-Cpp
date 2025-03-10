#include <iostream>
#include <ctime>
#include <nlohmann/json.hpp>
#include "essentials/deribitApi.h"
using namespace std;
using json = nlohmann::json;

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

    // Authentication
    json output;
    output["authentication_status"] = trader.authenticate();

    // Place Order
    string response = trader.placeOrder(1, instrument, quantity, orderType);
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

    cout << "Process completed. Output written to output.json" << endl;
    return 0;
}