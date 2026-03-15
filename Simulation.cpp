#include "Simulation.h"
#include <random>

Simulation::Simulation() : rng_(std::random_device{}()) {
    sim_start_wall_ = std::chrono::steady_clock::now();

    // Randomize per-trader latency 1000–3000 ms
    std::uniform_real_distribution<double> lat_dist(1000.0, 3000.0);
    for (int i = 0; i < NUM_TRADERS; ++i)
        trader_avg_latency_[i] = lat_dist(rng_);

    // ── Create Market Makers (IDs 1-5) ────────────────────────────────────────
    // ID, latency, accum_thresh, part_thresh, init_bid, init_ask (in cents)
    struct MMParams { int accum; int part; Price bid; Price ask; SimTime patience; };
    static const MMParams mm_params[5] = {
        {200,   200,  4990, 5001,  5.0},
        {500,   400,  4991, 5002, 10.0},
        {1000,  600,  4993, 5005, 20.0},
        {2000,  800,  4996, 5008, 15.0},
        {5000, 1000,  4999, 5010,  5.0},
    };
    for (int i = 0; i < NUM_MARKET_MAKERS; ++i) {
        const auto& p = mm_params[i];
        traders_.push_back(std::make_unique<MarketMaker>(
            i + 1,
            trader_avg_latency_[i],
            p.accum, p.part,
            p.bid, p.ask,
            p.patience
        ));
    }

    // ── Create Investors (IDs 6-10) ───────────────────────────────────────────
    // Analyst weights (must sum to 1.0)
    struct InvParams { int max_pos; std::array<double,NUM_ANALYSTS> weights; };
    static const InvParams inv_params[5] = {
        {50,   {0.55, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05}},
        {100,  {0.05, 0.30, 0.30, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05}},
        {300,  {0.05, 0.05, 0.05, 0.05, 0.40, 0.20, 0.05, 0.05, 0.05, 0.05}},
        {500,  {0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.25, 0.25, 0.10, 0.10}},
        {2000, {0.10, 0.10, 0.10, 0.10, 0.10, 0.10, 0.10, 0.10, 0.10, 0.10}},
    };
    for (int i = 0; i < NUM_INVESTORS; ++i) {
        const auto& p = inv_params[i];
        traders_.push_back(std::make_unique<Investor>(
            i + NUM_MARKET_MAKERS + 1,
            trader_avg_latency_[i + NUM_MARKET_MAKERS],
            p.max_pos, p.weights
        ));
    }
}

SimTime Simulation::sampleLatency(TraderID tid, SimTime now) const {
    double avg_ms = trader_avg_latency_[tid - 1];
    return now + avg_ms / 1000.0;
}

ExchangeView Simulation::buildView() const {
    const auto& book = exchange_.getBook();
    ExchangeView v;
    v.has_bid  = book.hasBid();
    v.has_ask  = book.hasAsk();
    v.best_bid = book.bestBid();
    v.best_ask = book.bestAsk();
    v.analyst_opinions = analysts_.getOpinions();
    return v;
}

void Simulation::tick() {
    using namespace std::chrono;
    auto now_wall = steady_clock::now();
    current_sim_time_ = duration<double>(now_wall - sim_start_wall_).count();
    SimTime now = current_sim_time_;

    // ── 1. Deliver inbound messages to Exchange ───────────────────────────────
    while (!inbound_to_exchange_.empty() &&
           getDeliveryTime(inbound_to_exchange_.top()) <= now) {
        TraderToExchMsg msg = inbound_to_exchange_.top();
        inbound_to_exchange_.pop();
        std::visit([&](const auto& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, NewOrderMsg>)
                exchange_.processNewOrder(m, now);
            else if constexpr (std::is_same_v<T, CancelMsg>)
                exchange_.processCancel(m, now);
            else if constexpr (std::is_same_v<T, ModifyMsg>)
                exchange_.processModify(m, now);
        }, msg);
    }

    // ── 2. Apply latency to Exchange outbound trader messages ─────────────────
    {
        auto msgs = exchange_.drainTraderMessages();
        for (auto& [tid, msg] : msgs) {
            SimTime delivery = sampleLatency(tid, now);
            outbound_to_traders_.push({delivery, outbound_seq_++, tid, msg});
        }
    }

    // ── 3. Collect LOB updates (broadcast immediately / no latency) ───────────
    latest_lob_updates_ = exchange_.drainLobUpdates();
    // Distribute to all traders (they track LOB state)
    // For simplicity: traders receive LOB updates instantly (no latency for broadcast)
    // This is a design choice — real exchanges have this be low-latency but not zero
    // We'll skip per-trader LOB knowledge for now (traders use ExchangeView instead)

    // ── 4. Deliver outbound messages to traders ───────────────────────────────
    while (!outbound_to_traders_.empty() &&
           outbound_to_traders_.top().delivery_time <= now) {
        auto [dt, seq, tid, msg] = outbound_to_traders_.top();
        outbound_to_traders_.pop();
        traders_[tid - 1]->onMessage(msg, now);
    }

    // ── 5. Ask all traders for new orders ─────────────────────────────────────
    ExchangeView view = buildView();
    for (auto& trader : traders_) {
        auto new_msgs = trader->onTick(now, view);
        for (auto& m : new_msgs)
            inbound_to_exchange_.push(m);
    }

    // ── 6. Update analysts ────────────────────────────────────────────────────
    {
        bool has_mid = view.has_bid && view.has_ask;
        double midquote = has_mid
            ? (centsToDouble(view.best_bid) + centsToDouble(view.best_ask)) / 2.0
            : 0.0;
        analysts_.update(now, midquote, has_mid);
    }

    // ── 7. Drain exchange global log → our rolling log (cap 20) ──────────────
    auto new_logs = exchange_.drainGlobalLog();
    for (auto& entry : new_logs) {
        global_log_.push_back(entry);
        while ((int)global_log_.size() > 20)
            global_log_.pop_front();
    }
}
