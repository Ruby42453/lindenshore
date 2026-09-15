#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>
#include "log_decoder.h"

class SwapDatabase {
public:
    explicit SwapDatabase(const std::string& path);
    ~SwapDatabase();

    void initSchema();

    void insertSwapsBatch(const std::vector<SwapEvent>& events);

    void insertBlockTimestamp(const std::string& chain, uint64_t blockNumber, uint64_t timestamp);

    void runAndPrint(const std::string& label, const std::string& sql);

    sqlite3* raw() { return db_; }

private:
    sqlite3* db_ = nullptr;
    void exec(const std::string& sql);
};
