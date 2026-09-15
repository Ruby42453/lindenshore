#pragma once
#include <string>
#include <cstdint>
#include "../third_party/nlohmann/json.hpp"

std::string int128ToString(__int128 value);
std::string uint128ToString(unsigned __int128 value);

struct SwapEvent {
    std::string chain;
    std::string pool_address;
    uint64_t block_number = 0;
    uint64_t block_timestamp = 0;
    std::string tx_hash;
    int log_index = 0;
    std::string sender;
    std::string recipient;

    __int128 amount0 = 0;
    __int128 amount1 = 0;
    unsigned __int128 sqrt_price_x96 = 0;
    unsigned __int128 liquidity = 0;
    int32_t tick = 0;
    double token0_decimals = 0.0;
    double price_token1_per_token0 = 0.0;
    double price_quote_per_base = 0.0;
    double trade_value_quote_units = 0.0;
};

SwapEvent decodeSwapLog(const nlohmann::json& log,
                         const std::string& chain,
                         double token0Decimals,
                         double token1Decimals,
                         bool token0IsBase);
