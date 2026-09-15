# Blockchain Data Discovery: Uniswap V3 vs PancakeSwap V3, ETH/USD Pools

Dataset: Swap events from the Uniswap V3 USDC/WETH 0.05% pool on Ethereum (72-hour window, Sept 11-14 2026) and the PancakeSwap V3 ETH/USDC 0.05% pool on BSC (12-hour window, same period). 18,162 Ethereum swaps and 1,604 BSC swaps were collected via free public RPC endpoints and stored in SQLite. Query numbers below refer to all_queries.md.

## Why this data is interesting

Uniswap V3 Swap events were chosen because they're relatively easy to collect and but still has a lot to analyze. The event has a fixed, five-field layout, so decoding doesn't need a general ABI library, just fixed-offset hex slicing. eth_getLogs filtering by contract address and event signature happens server-side, so no full blocks or irrelevant transactions get downloaded. 

ETH priced against a USD-pegged stablecoin was chosen as the pair because it's actively traded on both Ethereum and BSC through similar V3 pools, so a cross-chain comparison is actually meaningful. And because both pools see real bot activity, the same dataset covers three angles (MEV, risk/liquidity, arbitrage) without needing separate collection efforts for each.

## What was learned

Ethereum's pool is a heavily contested MEV target; BSC's is not. 16.81% of Ethereum blocks with any swap activity (1,750 of 10,412) matched the sandwich-pattern heuristic, versus 0.06% on BSC (1 of 1,571; query 5). This reflects that Ethereum's USDC/WETH pool is one of the most valuable MEV targets in DeFi, while the BSC pool, though real and liquid, isn't lucrative enough to draw comparable searcher competition.

A small number of addresses account for most Ethereum volume, and their behavior looks automated. The top three addresses represent 69.4% of all Ethereum trading volume despite being 3 of 318 unique senders (query 8). The largest, 0x51c72848..., made 1,849 swaps totaling $69.5M, averaging about $37,500 per trade with that kind of consistency, which isn't manual trading. Ethereum also shows more trading intensity per address than BSC (57.1 vs 29.7 average swaps per sender, query 7).

BSC's pool has measurably thinner liquidity than Ethereum's. 66.9% of BSC swaps are under $100 in value, versus 35.7% on Ethereum (query 6). This matches the price-impact data (query 2): Ethereum's sub-$100 trades move price by an average of 0.0 ticks, negligible against an $8,600 average trade size, while BSC's sub-$100 trades already move price by 1.29 ticks on average. Impact increases sharply with size on both chains, but BSC's market has less depth to absorb even small orders.

Cross-chain ETH/USD pricing stays tightly arbitraged at the minute timescale. Across 462 matched one-minute windows, the average absolute spread was 0.026%, the max was 0.24%, and only 3 minutes (0.65%) exceeded 0.15% (query 9). Persistent divergence gets closed quickly, most likely by arbitrage bots, leaving only small residual gaps.

The largest events in the dataset look like specific strategies rather than noise. The two biggest trades, $757,993 and $520,114, came from the same address at the exact same second (2026-09-11 19:46:17), consistent with one multi-hop or split order rather than two separate large trades (query 10). Address 0x4c82d1fb... also shows up repeatedly among the largest trades at similar times of day across different dates (around 03:00 UTC and 17:21 UTC), which looks like a scheduled or periodic rebalancer rather than opportunistic trading.

## Applications

Arbitrage: the cross-chain spread numbers (query 9) are a usable baseline for a cross-chain strategy. Average spreads around 0.026%, with 99.35% of minutes under 0.15%, mean a bot needs low execution/bridging costs and fast reaction time to profit. The price-impact table (query 2) tells such a bot how large a position it can take before its own trade erodes the spread it's chasing.

MEV detection: the sandwich frequency comparison (query 5) is the basis for a pre-trade risk warning. A trader on Ethereum's pool faces roughly a 1-in-6 chance their block matches the sandwich pattern, versus roughly 1-in-1,600 on BSC. Run continuously, the same heuristic becomes a live signal for which pools are currently contested.

Risk/liquidity analysis: the dust-trade percentage (query 6) and price-impact curve (query 2) together are a cheap, ongoing proxy for a pool's depth. A rising dust percentage or steeper impact curve over time would flag thinning liquidity before a large trade gets an unexpectedly bad price.

Market structure: the volume concentration finding (query 8, top 3 addresses = 69.4% of Ethereum volume) is a risk signal on its own. A pool this dependent on a handful of automated participants is more exposed if any one of them stops trading, which matters for anyone relying on this pool's depth.

## Limitations

The sandwich heuristic flags patterns, not proven MEV extraction. It misses router-mediated sandwiches (where every leg shares the router's address) and can occasionally flag an address that traded twice in one block for unrelated reasons. The two chains' windows differ in length (72h vs 12h) because BSC's real block time (0.45 seconds) is about 27x faster than Ethereum's, so equal-hour windows would cost disproportionately more RPC calls on BSC given free-tier rate limits.