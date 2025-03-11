#include <iostream>
#include <unordered_map>
#include <fstream>
#include <string>
#include <sstream>
#include <curl/curl.h>

using namespace std;

// Callback function to capture response
size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

// Sends an HTTP request (GET, POST) 
// For the given task only GET and POST is necessary
string sendRequest(const string& method, long timeout, const string& url, const string& payload = "", const string& auth = "") {
    static bool curlInitialized = false;
    if (!curlInitialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curlInitialized = true;
    }

    CURL* curl = curl_easy_init();
    string response;

    if (curl) {
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        if (!auth.empty()) {
            string authHeader = "Authorization: " + auth;
            headers = curl_slist_append(headers, authHeader.c_str());
        }

        // Set cURL options
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // HTTP methods
        // Default is GET method so that is not explicitly mentioned
        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        }

        // Perform request
        CURLcode status = curl_easy_perform(curl);
        if (status != CURLE_OK) {
            cerr << method << " Error - " << curl_easy_strerror(status) << endl;
        }

        // Cleanup
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return response;
}

// Reads environment variables from a `.env` file
unordered_map<string, string> readEnv(const string& fileName) {
    unordered_map<string, string> env;
    ifstream file(fileName);

    if (!file.is_open()) {
        cerr << "Error: Could not open file - " << fileName << endl;
        return env;
    }

    string line;
    while (getline(file, line)) {
        istringstream var(line);
        string key, value;
        if (getline(var, key, '=') && getline(var, value)) {
            env[key] = value;
        }
    }

    return env;
}