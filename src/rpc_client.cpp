#include "rpc_client.h"
#include <curl/curl.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <chrono>

using json = nlohmann::json;

namespace {
size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}
}

RpcClient::RpcClient(std::vector<std::string> endpoints, int callBudget)
    : endpoints_(std::move(endpoints)), callBudget_(callBudget) {
    if (endpoints_.empty()) {
        throw std::invalid_argument("RpcClient requires at least one endpoint");
    }
}

std::string RpcClient::toHexBlock(uint64_t block) {
    std::ostringstream oss;
    oss << "0x" << std::hex << block;
    return oss.str();
}

json RpcClient::call(const std::string& method, const json& params) {
    if (callsMade_ >= callBudget_) {
        throw std::runtime_error(
            "RPC call budget (" + std::to_string(callBudget_) + ") exhausted");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    json requestBody = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", method},
        {"params", params}
    };
    std::string requestStr = requestBody.dump();

    std::string lastError;
    for (const auto& endpoint : endpoints_) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            lastError = "curl_easy_init failed";
            continue;
        }

        std::string responseBody;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestStr.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res = curl_easy_perform(curl);
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            lastError = std::string("curl error: ") + curl_easy_strerror(res);
            std::cerr << "[rpc_client] " << endpoint << " failed (" << lastError
                      << "), trying next endpoint\n";
            continue;
        }
        if (httpCode < 200 || httpCode >= 300) {
            lastError = "HTTP " + std::to_string(httpCode);
            std::cerr << "[rpc_client] " << endpoint << " returned " << lastError
                      << ", trying next endpoint\n";
            continue;
        }

        json parsed;
        try {
            parsed = json::parse(responseBody);
        } catch (const json::parse_error& e) {
            lastError = std::string("invalid JSON response: ") + e.what();
            std::cerr << "[rpc_client] " << endpoint << " " << lastError
                      << ", trying next endpoint\n";
            continue;
        }

        if (parsed.contains("error")) {
            lastError = "RPC error: " + parsed["error"].dump();
            std::cerr << "[rpc_client] " << endpoint << " " << lastError
                      << ", trying next endpoint\n";
            continue;
        }

        callsMade_++;
        return parsed["result"];
    }

    throw std::runtime_error("All RPC endpoints failed for method '" + method +
                              "'. Last error: " + lastError);
}

uint64_t RpcClient::getBlockNumber() {
    json result = call("eth_blockNumber", json::array());
    return std::stoull(result.get<std::string>(), nullptr, 16);
}

json RpcClient::getLogs(const std::string& address,
                        const std::string& topic0,
                        uint64_t fromBlock,
                        uint64_t toBlock) {
    json filter = {
        {"address", address},
        {"topics", json::array({topic0})},
        {"fromBlock", toHexBlock(fromBlock)},
        {"toBlock", toHexBlock(toBlock)}
    };
    return call("eth_getLogs", json::array({filter}));
}

uint64_t RpcClient::getBlockTimestamp(uint64_t blockNumber) {
    json params = json::array({toHexBlock(blockNumber), false});
    json result = call("eth_getBlockByNumber", params);
    std::string tsHex = result["timestamp"].get<std::string>();
    return std::stoull(tsHex, nullptr, 16);
}
