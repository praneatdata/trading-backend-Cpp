cmake_minimum_required(VERSION 3.10)
project(TradingSystem)

add_definitions(-DASIO_STANDALONE -D_WEBSOCKETPP_CPP11_STL_)

find_package(CURL REQUIRED)
find_package(websocketpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(Asio REQUIRED)

add_executable(main main.cpp)
add_executable(webServer webServer.cpp)

target_link_libraries(main PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)
target_link_libraries(webServer PRIVATE CURL::libcurl websocketpp::websocketpp OpenSSL::SSL OpenSSL::Crypto nlohmann_json::nlohmann_json Boost::system asio::asio Boost::boost)

if(MSVC)
    # Apply to all build types
    add_compile_options(/bigobj)
    
    # Reduce debug section bloat
    add_compile_options($<$<CONFIG:Debug>:
        /Zc:inline
        /GR-
    >)
endif()

target_compile_definitions(webServer PRIVATE
    _WEBSOCKETPP_CPP11_STL_
    ASIO_STANDALONE
)

add_compile_definitions(
    ASIO_STANDALONE  # Force standalone ASIO
    _WEBSOCKETPP_CPP11_STL_
)

target_compile_options(webServer PRIVATE /bigobj)