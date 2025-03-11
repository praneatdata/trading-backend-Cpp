# **Deribit API Client**  

## **Overview**  
This project is a high-performance **order execution and management system** for trading on **Deribit**. It utilizes **REST API for trading actions** and **WebSockets for real-time market data**.  

## **Requirements**  

### **1. Build System**
- **CMake** (Minimum version: `3.10`)  

### **2. Libraries & Dependencies**
The project relies on the following external libraries:  

| Dependency       | Purpose |
|-----------------|---------|
| **CURL**        | For HTTP requests |
| **websocketpp** | For WebSocket communication |
| **nlohmann_json** | For JSON parsing & serialization |
| **OpenSSL**     | For secure communication |
| **Boost** (`Boost.System`) | For error handling & networking |
| **Asio**        | For networking operations |

Ensure these dependencies are installed before building the project.  

## **Installation of Dependencies**  

### **Ubuntu (Debian-based systems)**  
```sh
sudo apt update
sudo apt install libcurl4-openssl-dev libssl-dev libboost-system-dev
```

### **Windows (via vcpkg)**  
```sh
vcpkg install curl websocketpp nlohmann-json openssl boost-asio boost-system
```

### **MacOS (via Homebrew)**  
```sh
brew install curl openssl boost nlohmann-json
```

## **Setup & Build**  

### **1. Clone the Repository**  
```sh
git clone https://github.com/praneatdata/trading-backend-Cpp.git
cd trading-backend-Cpp
```

### **2. Create Environment File & Build Directory**  
```sh
mkdir build
cd build

# Create .env file
touch .env
echo "DERIBIT_CLIENT_ID={client_id}" >> .env
echo "DERIBIT_CLIENT_SECRET={client_secret}" >> .env
```
💡 **Get your Deribit API credentials from:**  
![Deribit API](https://i.imgur.com/poRb5xD.png)  

### **3. Build the Application**  
```sh
cmake .. -DCMAKE_TOOLCHAIN_FILE={/path/to/vcpkg/installation}/scripts/buildsystems/vcpkg.cmake
cmake --build .
```

## **Basic Commands**  

### **1. Order Execution Commands**  
The **order execution system** is implemented using `tradeManager`, which interacts with **Deribit API** via **HTTPClient**.

| Command | Description |
|---------|------------|
| `placeOrder(int buy, string symbol, double amount, string type = "market")` | Executes a buy(1)/sell(0) order. |
| `cancelOrder(string order_id)` | Cancels an existing order. |
| `modifyOrder(string order_id, double amount)` | Modifies an existing order. |
| `getOrderBook(string symbol, long long depth = 0)` | Fetches the order book for a given symbol. |
| `getPositions()` | Retrieves the current positions of the user. |

Executable location may vary based on the platform.
Run the order execution system:  
```sh
./main.exe
```
---

### **2. WebSocket Server Commands**  
The **order book server** is implemented using `orderBookServer`, which interacts with **Deribit API** via **WebSocketClient**.

| Command | Description |
|---------|------------|
| `listen(uint16_t port)` | Starts listening on the specified port. |
| `run()` | Runs the WebSocket server. |

---

### **3. Testing the WebSocket Server with Postman**  
Once the server is running, you can test the WebSocket connection using **Postman**:

- **WebSocket Endpoint**  
  ```sh
  ws://localhost:8080
  ```
- **Sample Message to subscribe**  
  ```json
  {
      "method": "subscribe",
      "symbol": "ETH-PERPETUAL",
      "depth": 1,
      "timeout": 3
  }
  ```
- **Sample Message to unsubscribe**
  ```json
  {
      "method": "unsubscribe",
      "symbol": "ETH-PERPETUAL"
  }
  ```
---

## **Project Structure**  

```
trading-backend-Cpp/
│── build/                     # Build directory
|   |── .env                   # Environment file
│── include/
│   │── deribitApi.h           # API communication logic
│   │── webServer.h            # WebSocket server implementation
│── main.cpp                   # Main order execution system
│── input.json                 # Order details (input file)
│── output.json                # Order response & market data
│── CMakeLists.txt             # CMake configuration file
```

---