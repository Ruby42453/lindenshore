#include "analysis.h"

namespace {

const char* kSandwichSql = R"SQL(
WITH ordered AS (
    SELECT
        chain, pool_address, block_number, tx_hash, log_index, sender, amount0,
        ROW_NUMBER() OVER (PARTITION BY chain, pool_address, block_number ORDER BY log_index) AS rn,
        COUNT(*) OVER (PARTITION BY chain, pool_address, block_number) AS swaps_in_block
    FROM swaps
),
first_last AS (
    SELECT
        chain, pool_address, block_number,
        MIN(CASE WHEN rn = 1 THEN sender END) AS first_sender,
        MIN(CASE WHEN rn = 1 THEN amount0 END) AS first_amount0,
        MAX(CASE WHEN rn = swaps_in_block THEN sender END) AS last_sender,
        MAX(CASE WHEN rn = swaps_in_block THEN amount0 END) AS last_amount0,
        MAX(swaps_in_block) AS swaps_in_block
    FROM ordered
    GROUP BY chain, pool_address, block_number
)
SELECT chain, pool_address, block_number, swaps_in_block, first_sender, last_sender
FROM first_last
WHERE swaps_in_block >= 3
  AND first_sender = last_sender
  AND (CAST(first_amount0 AS REAL) * CAST(last_amount0 AS REAL)) < 0
ORDER BY chain, block_number
LIMIT 50;
)SQL";

const char* kPriceImpactSql = R"SQL(
WITH deltas AS (
    SELECT
        chain,
        trade_value_quote_units,
        tick - LAG(tick) OVER (PARTITION BY chain, pool_address ORDER BY block_number, log_index) AS tick_delta
    FROM swaps
)
SELECT
    chain,
    CASE
        WHEN trade_value_quote_units < 100     THEN '0: <$100 (dust)'
        WHEN trade_value_quote_units < 10000   THEN '1: $100-$10k'
        WHEN trade_value_quote_units < 100000  THEN '2: $10k-$100k'
        WHEN trade_value_quote_units < 1000000 THEN '3: $100k-$1M'
        ELSE '4: >=$1M'
    END AS trade_size_bucket,
    COUNT(*) AS swap_count,
    ROUND(AVG(ABS(tick_delta)), 2) AS avg_abs_tick_move
FROM deltas
WHERE tick_delta IS NOT NULL
GROUP BY chain, trade_size_bucket
ORDER BY chain, trade_size_bucket;
)SQL";

const char* kHourlyVolumeSql = R"SQL(
SELECT
    chain,
    strftime('%Y-%m-%d %H:00', block_timestamp, 'unixepoch') AS hour_bucket,
    COUNT(*) AS swap_count,
    ROUND(SUM(ABS(CAST(amount0 AS REAL)) / POWER(10.0, token0_decimals)), 4)
        AS total_abs_amount0_human_units
FROM swaps
GROUP BY chain, hour_bucket
ORDER BY chain, hour_bucket;
)SQL";

const char* kCrossChainSql = R"SQL(
WITH bucketed AS (
    SELECT
        chain,
        (block_timestamp / 60) * 60 AS minute_bucket,
        AVG(price_quote_per_base) AS avg_price
    FROM swaps
    GROUP BY chain, minute_bucket
)
SELECT
    e.minute_bucket,
    datetime(e.minute_bucket, 'unixepoch') AS minute_utc,
    e.avg_price AS ethereum_price,
    b.avg_price AS bsc_price,
    ROUND(100.0 * (e.avg_price - b.avg_price) / b.avg_price, 4) AS pct_spread
FROM bucketed e
JOIN bucketed b
  ON e.minute_bucket = b.minute_bucket AND e.chain = 'ethereum' AND b.chain = 'bsc'
ORDER BY ABS(pct_spread) DESC
LIMIT 20;
)SQL";

}  // namespace

void runCoreAnalysis(SwapDatabase& db) {
    db.runAndPrint("Sandwich-pattern candidates (heuristic)", kSandwichSql);
    db.runAndPrint("Price impact by trade-size bucket", kPriceImpactSql);
    db.runAndPrint("Hourly swap volume", kHourlyVolumeSql);
}

void runCrossChainAnalysis(SwapDatabase& db) {
    db.runAndPrint("Cross-chain price divergence (Ethereum vs BSC, 1-min buckets)", kCrossChainSql);
}
