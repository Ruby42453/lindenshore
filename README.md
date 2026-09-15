## What it does

1. Reads `config/config.json` (chains, pool addresses, RPC endpoints, lookback window).
2. Per chain: finds the current block, computes a lookback window, and pulls `Swap` events in RPC-limit-sized chunks via `eth_getLogs`, filtered server-side by contract address and event topic.
3. Decodes each log with fixed-offset hex slicing and batch-inserts into SQLite, one transaction per chunk.
4. Runs the queries in `sql/queries.sql` and prints results: a sandwich-pattern heuristic, price impact by trade size, hourly volume, and (once both chains have data) cross-chain price divergence.

## Requirements

- CMake >= 3.10, a C++17 compiler with GCC/Clang `__int128` support (not supported on MSVC)
- `libcurl` dev headers (`libcurl4-openssl-dev` on Debian/Ubuntu)
- `libsqlite3` dev headers (`libsqlite3-dev` on Debian/Ubuntu)
- `third_party/nlohmann/json.hpp` is already vendored (single header, MIT licensed)

## Build

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

Produces `build/discover`.

## Configure before running

Open `config/config.json`:

- The BSC entry needs a real pool address and its own Ankr key. Can get a free one from www.ankr.com/rpc/

### Finding the BSC pool address

1. Go to `https://bscscan.com/address/0x0BFbCF9fa4f9C56B0F40a671Ad40E0805A091865#readContract`
2. Find `getPool(address tokenA, address tokenB, uint24 fee)`.
3. For ETH/USDC: `tokenA` = `0x2170Ed0880ac9A755fd29B2688956BD959F933F8`, `tokenB` = `0x8AC76a51cc950d9822D68b83fE1Ad97B32Cd580d`, `fee` = `500`
4. The returned address is the `pool_address`.
5. Compare the two token addresses numerically to determine token0/token1 order. For ETH vs USDC on BSC, ETH is smaller, so ETH is token0 (`token0_is_base: true`).

## Run

```bash
./build/discover config/config.json
```

Prints progress per chain, then the analysis tables. Data lands in `data/swaps.db`:

```bash
sqlite3 data/swaps.db
```

## Repo layout

```
config/config.json      # chains, pools, RPC endpoints
src/
  main.cpp               # orchestration: fetch -> decode -> store -> analyze
  rpc_client.{h,cpp}      # libcurl JSON-RPC client with endpoint fallback
  log_decoder.{h,cpp}     # fixed-offset hex decoding of Swap events
  db.{h,cpp}              # SQLite wrapper: schema, batched inserts, query printer
  analysis.{h,cpp}        # the analysis queries
sql/
  schema.sql              # human-readable copy of the schema (embedded in db.cpp)
  queries.sql             # human-readable copy of the analysis queries (embedded in analysis.cpp)
third_party/nlohmann/json.hpp  # vendored single-header JSON library (MIT licensed)
data/swaps.db            # created on first run
writeup.md
all_queries.md
CMakeLists.txt
```