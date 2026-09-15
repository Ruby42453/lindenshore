#include "log_decoder.h"
#include <cmath>
#include <stdexcept>

using json = nlohmann::json;

namespace {

int hexDigitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::invalid_argument("invalid hex digit");
}

unsigned __int128 hexToU128(const std::string& hex) {
    unsigned __int128 result = 0;
    for (char c : hex) {
        result = (result << 4) | static_cast<unsigned __int128>(hexDigitValue(c));
    }
    return result;
}

std::string stripHexPrefix(const std::string& s) {
    return (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
               ? s.substr(2)
               : s;
}

std::string addressFromTopic(const std::string& topic) {
    std::string hex = stripHexPrefix(topic);
    if (hex.size() < 40) throw std::invalid_argument("topic too short for address");
    return "0x" + hex.substr(hex.size() - 40);
}

}  // namespace

std::string uint128ToString(unsigned __int128 value) {
    if (value == 0) return "0";
    std::string digits;
    while (value > 0) {
        digits.push_back(static_cast<char>('0' + static_cast<int>(value % 10)));
        value /= 10;
    }
    std::string result(digits.rbegin(), digits.rend());
    return result;
}

std::string int128ToString(__int128 value) {
    if (value < 0) {
        unsigned __int128 magnitude = static_cast<unsigned __int128>(-(value + 1)) + 1;
        return "-" + uint128ToString(magnitude);
    }
    return uint128ToString(static_cast<unsigned __int128>(value));
}

SwapEvent decodeSwapLog(const json& log,
                        const std::string& chain,
                        double token0Decimals,
                        double token1Decimals,
                        bool token0IsBase) {
    SwapEvent ev;
    ev.chain = chain;
    ev.pool_address = log.at("address").get<std::string>();
    ev.block_number = std::stoull(stripHexPrefix(log.at("blockNumber").get<std::string>()),
                                   nullptr, 16);
    ev.tx_hash = log.at("transactionHash").get<std::string>();
    ev.log_index = static_cast<int>(
        std::stoul(stripHexPrefix(log.at("logIndex").get<std::string>()), nullptr, 16));

    const auto& topics = log.at("topics");
    ev.sender = addressFromTopic(topics.at(1).get<std::string>());
    ev.recipient = addressFromTopic(topics.at(2).get<std::string>());

    std::string data = stripHexPrefix(log.at("data").get<std::string>());
    if (data.size() < 5 * 64) {
        throw std::invalid_argument("Swap log data shorter than expected 5 words");
    }

    auto lowWord = [&](int wordIndex) {
        size_t start = static_cast<size_t>(wordIndex) * 64 + 32;
        return data.substr(start, 32);
    };

    unsigned __int128 amount0Raw = hexToU128(lowWord(0));
    unsigned __int128 amount1Raw = hexToU128(lowWord(1));

    ev.amount0 = static_cast<__int128>(amount0Raw);
    ev.amount1 = static_cast<__int128>(amount1Raw);

    ev.sqrt_price_x96 = hexToU128(lowWord(2));
    ev.liquidity = hexToU128(lowWord(3));

    std::string tickHex = data.substr(4 * 64 + 64 - 6, 6);
    uint32_t tickRaw = 0;
    for (char c : tickHex) tickRaw = (tickRaw << 4) | static_cast<uint32_t>(hexDigitValue(c));
    if (tickRaw & 0x800000u) {
        tickRaw |= 0xFF000000u;  // sign-extend
    }
    ev.tick = static_cast<int32_t>(tickRaw);

    double sqrtPriceDouble = static_cast<double>(ev.sqrt_price_x96);
    double rawPrice = sqrtPriceDouble / std::pow(2.0, 96.0);
    rawPrice = rawPrice * rawPrice;
    ev.price_token1_per_token0 = rawPrice * std::pow(10.0, token0Decimals - token1Decimals);

    ev.price_quote_per_base =
        token0IsBase ? ev.price_token1_per_token0 : (1.0 / ev.price_token1_per_token0);

    ev.token0_decimals = token0Decimals;

    double amount0Human = std::fabs(static_cast<double>(ev.amount0)) / std::pow(10.0, token0Decimals);
    ev.trade_value_quote_units =
        token0IsBase ? (amount0Human * ev.price_quote_per_base) : amount0Human;

    return ev;
}
