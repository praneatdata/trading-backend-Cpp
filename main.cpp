#include <iostream>
#include <ctime>
#include <nlohmann/json.hpp>
#include "essentials/deribitApi.h"
using namespace std;

// Example usage
int main(){
    tradeManager trader = tradeManager();

    // authenticating
    cout << trader.authenticate() << endl;

    // placing a buy order and printing the order id of the same
    string response = trader.placeOrder(1, "ETH-PERPETUAL", 40, "market");
    auto output = nlohmann::json::parse(response);
    string order_id =  output["result"]["trades"][0]["order_id"];
    cout << "placed order successfully - " << order_id << endl;

    // get the order book
    cout << trader.getOrderBook("ETH-PERPETUAL", 1) << endl;

    // get active positions 
    cout << trader.getPositions() << endl;

    return 0;
}