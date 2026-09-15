#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../third_party/nlohmann/json.hpp"

class RpcClient {
public:
    RpcClient(std::vector<std::string> endpoints, int callBudget);

    uint64_t getBlockNumber();

    nlohmann::json getLogs(const std::string& address,
                           const std::string& topic0,
                           uint64_t fromBlock,
                           uint64_t toBlock);

    uint64_t getBlockTimestamp(uint64_t blockNumber);

    int callsMade() const { return callsMade_; }
    int callBudget() const { return callBudget_; }

private:
    std::vector<std::string> endpoints_;
    int callBudget_;
    int callsMade_ = 0;

    nlohmann::json call(const std::string& method, const nlohmann::json& params);

    static std::string toHexBlock(uint64_t block);
};
