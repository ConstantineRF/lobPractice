#pragma once
#include "Exchange.h"
#include "Analyst.h"
#include "Trader.h"
#include "MarketMaker.h"
#include "Investor.h"
#include <chrono>
#include <queue>
#include <memory>
#include <deque>
#include <array>
#include <vector>

class Simulation {
public:
    Simulation();

    // Call once per frame
    void tick();

    // ── Read-only accessors for Renderer ──────────────────────────────────────
    SimTime              getSimTime()    const { return current_sim_time_; }
    const Exchange&      getExchange()   const { return exchange_; }
    const AnalystSystem& getAnalysts()   const { return analysts_; }
    const Trader&        getTrader(int idx) const { return *traders_[idx]; }  // idx 0-9
    const std::deque<LogEntry>& getGlobalLog() const { return global_log_; }

    // Latest LOB updates (for potential subscriber display)
    const std::vector<LobUpdateMsg>& getLatestLobUpdates() const { return latest_lob_updates_; }

private:
    Exchange      exchange_;
    AnalystSystem analysts_;
    std::vector<std::unique_ptr<Trader>> traders_;  // indices 0-9 → IDs 1-10

    std::chrono::steady_clock::time_point sim_start_wall_;
    SimTime current_sim_time_ = 0.0;

    // ── Inbound to exchange (trader → exchange) min-heap ──────────────────────
    std::priority_queue<TraderToExchMsg,
                        std::vector<TraderToExchMsg>,
                        InboundComparator> inbound_to_exchange_;

    // ── Outbound to traders (exchange → trader) min-heap ──────────────────────
    // seq preserves generation order for messages with identical delivery times
    struct TimedTraderMsg {
        SimTime         delivery_time;
        uint64_t        seq;
        TraderID        trader_id;
        ExchToTraderMsg msg;
        bool operator>(const TimedTraderMsg& o) const {
            if (delivery_time != o.delivery_time) return delivery_time > o.delivery_time;
            return seq > o.seq;
        }
    };
    std::priority_queue<TimedTraderMsg,
                        std::vector<TimedTraderMsg>,
                        std::greater<TimedTraderMsg>> outbound_to_traders_;
    uint64_t outbound_seq_ = 0;

    std::deque<LogEntry> global_log_;       // last 20 non-LOB messages (for UI)
    std::vector<LobUpdateMsg> latest_lob_updates_;

    // Per-trader average latency (ms), randomized 1000-3000 at startup
    std::array<double, NUM_TRADERS> trader_avg_latency_{};

    // ── Helpers ───────────────────────────────────────────────────────────────
    ExchangeView buildView() const;
    SimTime sampleLatency(TraderID tid, SimTime now) const;

    mutable std::mt19937 rng_;
};
