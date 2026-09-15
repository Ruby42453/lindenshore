#include "db.h"
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

SwapDatabase::SwapDatabase(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error("Failed to open SQLite DB '" + path + "': " + err);
    }
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA synchronous=NORMAL;");
}

SwapDatabase::~SwapDatabase() {
    if (db_) sqlite3_close(db_);
}

void SwapDatabase::exec(const std::string& sql) {
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string err = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw std::runtime_error("SQLite error: " + err + "\nSQL: " + sql);
    }
}

void SwapDatabase::initSchema() {
    exec(R"SQL(
        CREATE TABLE IF NOT EXISTS swaps (
            chain                    TEXT    NOT NULL,
            pool_address             TEXT    NOT NULL,
            block_number             INTEGER NOT NULL,
            block_timestamp          INTEGER NOT NULL,
            tx_hash                  TEXT    NOT NULL,
            log_index                INTEGER NOT NULL,
            sender                   TEXT    NOT NULL,
            recipient                TEXT    NOT NULL,
            amount0                  TEXT    NOT NULL,
            amount1                  TEXT    NOT NULL,
            sqrt_price_x96           TEXT    NOT NULL,
            liquidity                TEXT    NOT NULL,
            tick                     INTEGER NOT NULL,
            price_token1_per_token0  REAL    NOT NULL,
            price_quote_per_base     REAL    NOT NULL,
            token0_decimals          REAL    NOT NULL,
            trade_value_quote_units  REAL    NOT NULL,
            PRIMARY KEY (chain, tx_hash, log_index)
        );
        CREATE INDEX IF NOT EXISTS idx_swaps_chain_block
            ON swaps (chain, block_number, log_index);
        CREATE INDEX IF NOT EXISTS idx_swaps_chain_timestamp
            ON swaps (chain, block_timestamp);
        CREATE TABLE IF NOT EXISTS block_timestamps (
            chain         TEXT    NOT NULL,
            block_number  INTEGER NOT NULL,
            timestamp     INTEGER NOT NULL,
            PRIMARY KEY (chain, block_number)
        );
    )SQL");
}

void SwapDatabase::insertSwapsBatch(const std::vector<SwapEvent>& events) {
    if (events.empty()) return;

    exec("BEGIN TRANSACTION;");

    static const char* kInsertSql =
        "INSERT OR IGNORE INTO swaps "
        "(chain, pool_address, block_number, block_timestamp, tx_hash, log_index, "
        " sender, recipient, amount0, amount1, sqrt_price_x96, liquidity, tick, "
        " price_token1_per_token0, price_quote_per_base, token0_decimals, trade_value_quote_units) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kInsertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        exec("ROLLBACK;");
        throw std::runtime_error(std::string("Failed to prepare insert: ") + sqlite3_errmsg(db_));
    }

    for (const auto& e : events) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_text(stmt, 1, e.chain.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, e.pool_address.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(e.block_number));
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(e.block_timestamp));
        sqlite3_bind_text(stmt, 5, e.tx_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, e.log_index);
        sqlite3_bind_text(stmt, 7, e.sender.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, e.recipient.c_str(), -1, SQLITE_TRANSIENT);
        std::string amount0Str = int128ToString(e.amount0);
        std::string amount1Str = int128ToString(e.amount1);
        std::string sqrtPriceStr = uint128ToString(e.sqrt_price_x96);
        std::string liquidityStr = uint128ToString(e.liquidity);
        sqlite3_bind_text(stmt, 9, amount0Str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 10, amount1Str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 11, sqrtPriceStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 12, liquidityStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 13, e.tick);
        sqlite3_bind_double(stmt, 14, e.price_token1_per_token0);
        sqlite3_bind_double(stmt, 15, e.price_quote_per_base);
        sqlite3_bind_double(stmt, 16, e.token0_decimals);
        sqlite3_bind_double(stmt, 17, e.trade_value_quote_units);

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            exec("ROLLBACK;");
            throw std::runtime_error(std::string("Insert failed: ") + sqlite3_errmsg(db_));
        }
    }

    sqlite3_finalize(stmt);
    exec("COMMIT;");
}

void SwapDatabase::insertBlockTimestamp(const std::string& chain, uint64_t blockNumber,
                                         uint64_t timestamp) {
    static const char* kSql =
        "INSERT OR IGNORE INTO block_timestamps (chain, block_number, timestamp) VALUES (?,?,?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Failed to prepare timestamp insert: ") +
                                  sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, chain.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(blockNumber));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(timestamp));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void SwapDatabase::runAndPrint(const std::string& label, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[db] Failed to prepare query '" << label << "': " << sqlite3_errmsg(db_)
                  << "\n";
        return;
    }

    std::cout << "\n=== " << label << " ===\n";
    int columnCount = sqlite3_column_count(stmt);

    const int kColWidth = 46;

    for (int i = 0; i < columnCount; i++) {
        std::cout << std::left << std::setw(kColWidth) << sqlite3_column_name(stmt, i);
    }
    std::cout << "\n";

    int rowCount = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        rowCount++;
        for (int i = 0; i < columnCount; i++) {
            const unsigned char* text = sqlite3_column_text(stmt, i);
            std::cout << std::left << std::setw(kColWidth)
                       << (text ? reinterpret_cast<const char*>(text) : "");
        }
        std::cout << "\n";
    }
    if (rowCount == 0) {
        std::cout << "(no rows)\n";
    }

    sqlite3_finalize(stmt);
}
