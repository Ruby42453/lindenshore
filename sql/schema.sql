-- One row per decoded Swap event, from any configured chain.
--
-- amount0/amount1/sqrt_price_x96/liquidity are stored as TEXT (exact
-- decimal strings) since they don't fit in SQLite's 64-bit INTEGER and a
-- REAL would lose precision.
CREATE TABLE IF NOT EXISTS swaps (
    chain                    TEXT    NOT NULL,
    pool_address             TEXT    NOT NULL,
    block_number             INTEGER NOT NULL,
    block_timestamp          INTEGER NOT NULL,  -- unix seconds, interpolated (see main.cpp)
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
    price_quote_per_base     REAL    NOT NULL,  -- normalized across chains, see log_decoder.h
    token0_decimals          REAL    NOT NULL,
    trade_value_quote_units  REAL    NOT NULL,  -- trade size in quote-asset units
    PRIMARY KEY (chain, tx_hash, log_index)
);

CREATE INDEX IF NOT EXISTS idx_swaps_chain_block
    ON swaps (chain, block_number, log_index);

CREATE INDEX IF NOT EXISTS idx_swaps_chain_timestamp
    ON swaps (chain, block_timestamp);

-- Anchor timestamps used to interpolate every other block's timestamp.
CREATE TABLE IF NOT EXISTS block_timestamps (
    chain         TEXT    NOT NULL,
    block_number  INTEGER NOT NULL,
    timestamp     INTEGER NOT NULL,
    PRIMARY KEY (chain, block_number)
);
