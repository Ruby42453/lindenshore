// Fetches Uniswap V3 / PancakeSwap V3 Swap events for the pools in
// config.json, decodes them, stores them in SQLite, and runs the
// analysis queries.
//
// Timestamps are only fetched for the two ends of each chain's block
// window and interpolated in between, instead of fetched per swap, to
// keep RPC call count independent of swap count. Inserts are batched
// per chunk instead of per row.
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>

#include "../third_party/nlohmann/json.hpp"
#include "rpc_client.h"
#include "log_decoder.h"
#include "db.h"
#include "analysis.h"

using json = nlohmann::json;

namespace {

bool isPlaceholderAddress(const std::string& addr) {
    std::string lower = addr;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return lower == "0x0000000000000000000000000000000000000000";
}

// Linear interpolation between two known (block, timestamp) points.
// Block production is roughly constant-interval, so this is accurate
// enough for minute-level bucketing without an RPC call per block.
uint64_t interpolateTimestamp(uint64_t block, uint64_t fromBlock, uint64_t fromTs,
                               uint64_t toBlock, uint64_t toTs) {
    if (toBlock == fromBlock) return fromTs;
    double fraction = static_cast<double>(block - fromBlock) /
                       static_cast<double>(toBlock - fromBlock);
    return fromTs + static_cast<uint64_t>(fraction * static_cast<double>(toTs - fromTs));
}

// Fetches, decodes, and stores swaps for one chain. Returns true if any
// swaps were stored.
bool processChain(const json& chainCfg, int defaultLookbackHours, int maxRpcCalls, SwapDatabase& db) {
    std::string name = chainCfg.at("name").get<std::string>();
    std::string poolAddress = chainCfg.at("pool_address").get<std::string>();

    if (isPlaceholderAddress(poolAddress)) {
        std::cout << "[" << name << "] pool_address is still the config placeholder -- "
                  << "skipping. Fill in a verified pool address in config/config.json.\n";
        return false;
    }

    std::vector<std::string> endpoints = chainCfg.at("rpc_endpoints").get<std::vector<std::string>>();
    RpcClient rpc(endpoints, maxRpcCalls);

    std::string topic0 = chainCfg.at("swap_event_topic0").get<std::string>();

    double blockTimeSeconds = chainCfg.at("block_time_seconds").get<double>();
    int chunkSize = chainCfg.at("getlogs_max_block_range").get<int>();
    double token0Decimals = chainCfg.at("token0_decimals").get<double>();
    double token1Decimals = chainCfg.at("token1_decimals").get<double>();
    bool token0IsBase = chainCfg.at("token0_is_base").get<bool>();
    int lookbackHours = chainCfg.value("lookback_hours", defaultLookbackHours);

    try {
        uint64_t latestBlock = rpc.getBlockNumber();
        uint64_t windowBlocks =
            static_cast<uint64_t>((static_cast<double>(lookbackHours) * 3600.0) / blockTimeSeconds);
        uint64_t fromBlock = (windowBlocks < latestBlock) ? (latestBlock - windowBlocks) : 0;
        uint64_t toBlock = latestBlock;

        std::cout << "[" << name << "] scanning blocks " << fromBlock << " -> " << toBlock
                  << " (" << (toBlock - fromBlock) << " blocks, ~" << lookbackHours
                  << "h) on pool " << poolAddress << "\n";

        uint64_t fromTs = rpc.getBlockTimestamp(fromBlock);
        uint64_t toTs = rpc.getBlockTimestamp(toBlock);
        db.insertBlockTimestamp(name, fromBlock, fromTs);
        db.insertBlockTimestamp(name, toBlock, toTs);

        uint64_t totalSwaps = 0;
        for (uint64_t chunkStart = fromBlock; chunkStart <= toBlock;
             chunkStart += static_cast<uint64_t>(chunkSize)) {
            uint64_t chunkEnd = std::min(chunkStart + static_cast<uint64_t>(chunkSize) - 1, toBlock);

            json logs;
            try {
                logs = rpc.getLogs(poolAddress, topic0, chunkStart, chunkEnd);
            } catch (const std::exception& e) {
                std::cerr << "[" << name << "] stopping fetch early: " << e.what() << "\n";
                break;
            }

            std::vector<SwapEvent> batch;
            batch.reserve(logs.size());
            for (const auto& log : logs) {
                try {
                    SwapEvent ev =
                        decodeSwapLog(log, name, token0Decimals, token1Decimals, token0IsBase);
                    ev.block_timestamp =
                        interpolateTimestamp(ev.block_number, fromBlock, fromTs, toBlock, toTs);
                    batch.push_back(std::move(ev));
                } catch (const std::exception& e) {
                    std::cerr << "[" << name << "] skipping undecodable log: " << e.what() << "\n";
                }
            }

            db.insertSwapsBatch(batch);
            totalSwaps += batch.size();

            std::cout << "[" << name << "] blocks " << chunkStart << "-" << chunkEnd << ": "
                      << batch.size() << " swaps (rpc calls used: " << rpc.callsMade() << "/"
                      << rpc.callBudget() << ")\n";
        }

        std::cout << "[" << name << "] done: " << totalSwaps << " swaps stored.\n";
        return totalSwaps > 0;

    } catch (const std::exception& e) {
        std::cerr << "[" << name << "] fatal error, skipping chain: " << e.what() << "\n";
        return false;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string configPath = (argc > 1) ? argv[1] : "config/config.json";

    std::ifstream configFile(configPath);
    if (!configFile.is_open()) {
        std::cerr << "Could not open config file: " << configPath << "\n"
                  << "Usage: " << argv[0] << " [path/to/config.json]\n";
        return 1;
    }

    json config;
    try {
        configFile >> config;
    } catch (const json::parse_error& e) {
        std::cerr << "Failed to parse config: " << e.what() << "\n";
        return 1;
    }

    int lookbackHours = config.value("lookback_hours", 24);
    int maxRpcCalls = config.value("max_rpc_calls_per_chain", 60);
    std::string dbPath = config.value("sqlite_path", "data/swaps.db");
    
    std::filesystem::path dbFsPath(dbPath);
    if (dbFsPath.has_parent_path()) {
        std::filesystem::create_directories(dbFsPath.parent_path());
    }

    std::unique_ptr<SwapDatabase> dbPtr;
    try {
        dbPtr = std::make_unique<SwapDatabase>(dbPath);
        dbPtr->initSchema();
    } catch (const std::exception& e) {
        std::cerr << "Failed to open/initialize database at '" << dbPath << "': " << e.what()
                  << "\n";
        return 1;
    }
    SwapDatabase& db = *dbPtr;

    std::vector<std::string> chainsWithData;
    for (const auto& chainCfg : config.at("chains")) {
        std::string name = chainCfg.at("name").get<std::string>();
        bool got = processChain(chainCfg, lookbackHours, maxRpcCalls, db);
        if (got) chainsWithData.push_back(name);
    }

    if (chainsWithData.empty()) {
        std::cout << "\nNo chains produced data (check pool addresses / RPC endpoints in "
                  << "config/config.json). Nothing to analyze.\n";
        return 0;
    }

    runCoreAnalysis(db);

    bool hasEthereum = std::find(chainsWithData.begin(), chainsWithData.end(), "ethereum") !=
                        chainsWithData.end();
    bool hasBsc = std::find(chainsWithData.begin(), chainsWithData.end(), "bsc") !=
                  chainsWithData.end();
    if (hasEthereum && hasBsc) {
        runCrossChainAnalysis(db);
    } else {
        std::cout << "\n(Skipping cross-chain divergence analysis -- need data from both "
                  << "'ethereum' and 'bsc'. Fill in a verified BSC pool address in "
                  << "config/config.json to enable it.)\n";
    }

    std::cout << "\nDone. Raw data is in " << dbPath
              << " -- open it with `sqlite3 " << dbPath << "` for further ad-hoc queries.\n";
    return 0;
}
