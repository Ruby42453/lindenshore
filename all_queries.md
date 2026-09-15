# SQL Queries

### 1. Sandwich heuristic

```sql
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
```

### 2. Price impact by trade size

```sql
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
```

### 3. Hourly volume

```sql
SELECT
    chain,
    strftime('%Y-%m-%d %H:00', block_timestamp, 'unixepoch') AS hour_bucket,
    COUNT(*) AS swap_count,
    ROUND(SUM(ABS(CAST(amount0 AS REAL)) / POWER(10.0, token0_decimals)), 4)
        AS total_abs_amount0_human_units
FROM swaps
GROUP BY chain, hour_bucket
ORDER BY chain, hour_bucket;
```

### 4. Cross-chain price divergence

```sql
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
```

### 5. Sandwich frequency as % of active blocks

```sql
WITH blocks_with_swaps AS (
    SELECT chain, block_number, COUNT(*) AS n
    FROM swaps GROUP BY chain, block_number
),
sandwich_blocks AS (
    SELECT chain, block_number FROM swaps
    GROUP BY chain, block_number
    HAVING COUNT(*) >= 3
)
SELECT
    b.chain,
    COUNT(DISTINCT b.block_number) AS total_active_blocks,
    (SELECT COUNT(*) FROM sandwich_blocks s WHERE s.chain = b.chain) AS candidate_sandwich_blocks,
    ROUND(100.0 * (SELECT COUNT(*) FROM sandwich_blocks s WHERE s.chain = b.chain)
          / COUNT(DISTINCT b.block_number), 2) AS pct_of_blocks
FROM blocks_with_swaps b
GROUP BY b.chain;
```

### 6. Dust-trade proportion per chain

```sql
SELECT
    chain,
    COUNT(*) AS total_swaps,
    SUM(CASE WHEN trade_value_quote_units < 100 THEN 1 ELSE 0 END) AS dust_swaps,
    ROUND(100.0 * SUM(CASE WHEN trade_value_quote_units < 100 THEN 1 ELSE 0 END) / COUNT(*), 1) AS pct_dust
FROM swaps
GROUP BY chain;
```

### 7. Market breadth

```sql
SELECT chain, COUNT(*) AS total_swaps, COUNT(DISTINCT sender) AS unique_senders,
       ROUND(1.0 * COUNT(*) / COUNT(DISTINCT sender), 1) AS avg_swaps_per_sender
FROM swaps
GROUP BY chain;
```

### 8. Top addresses by volume

```sql
WITH totals AS (SELECT chain, SUM(trade_value_quote_units) AS chain_total FROM swaps GROUP BY chain)
SELECT s.chain, s.sender, COUNT(*) AS num_swaps,
       ROUND(SUM(s.trade_value_quote_units), 2) AS volume_usd,
       ROUND(100.0 * SUM(s.trade_value_quote_units) / t.chain_total, 2) AS pct_of_chain_volume
FROM swaps s JOIN totals t ON s.chain = t.chain
GROUP BY s.chain, s.sender
ORDER BY volume_usd DESC
LIMIT 10;
```

### 9. Cross-chain spread distribution

```sql
SELECT
    COUNT(*) AS matched_minutes,
    ROUND(AVG(ABS(pct_spread)), 4) AS avg_abs_pct_spread,
    ROUND(MAX(ABS(pct_spread)), 4) AS max_abs_pct_spread,
    SUM(CASE WHEN ABS(pct_spread) > 0.15 THEN 1 ELSE 0 END) AS minutes_over_0_15pct
FROM (
    WITH bucketed AS (
        SELECT chain, (block_timestamp/60)*60 AS minute_bucket, AVG(price_quote_per_base) AS avg_price
        FROM swaps GROUP BY chain, minute_bucket
    )
    SELECT ROUND(100.0*(e.avg_price-b.avg_price)/b.avg_price, 4) AS pct_spread
    FROM bucketed e JOIN bucketed b ON e.minute_bucket=b.minute_bucket AND e.chain='ethereum' AND b.chain='bsc'
);
```

### 10. Biggest single trades

```sql
SELECT chain, datetime(block_timestamp,'unixepoch') AS when_utc, sender,
       ROUND(trade_value_quote_units, 2) AS usd_value
FROM swaps
ORDER BY trade_value_quote_units DESC
LIMIT 10;
```
