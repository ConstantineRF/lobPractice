#pragma once
#include "Types.h"
#include "Message.h"
#include "Analyst.h"
#include <unordered_map>
#include <deque>
#include <vector>
#include <array>
#include <random>

struct ExchangeView {
    Price best_bid = 0;
    Price best_ask = 0;
    bool  has_bid  = false;
    bool  has_ask  = false;
    std::array<double, NUM_ANALYSTS> analyst_opinions = {};
};

class Trader {
public:
    Trader(TraderID id, double avg_latency_ms);
    virtual ~Trader() = default;

    // Called each frame; return any messages to send to exchange
    virtual std::vector<TraderToExchMsg> onTick(SimTime now, const ExchangeView& view) = 0;
    // Called when exchange delivers a message to this trader
    virtual void onMessage(const ExchToTraderMsg& msg, SimTime now) = 0;

    TraderID id()     const { return id_; }
    double   cash()   const { return cash_; }
    int      shares() const { return shares_; }
    const std::deque<LogEntry>& getLog() const { return log_; }

    // Fair value estimate — base: uniform average; overridden by Investor with weights
    virtual double getFairValue(const std::array<double, NUM_ANALYSTS>& opinions) const {
        double sum = 0.0;
        for (double o : opinions) sum += o;
        return sum / NUM_ANALYSTS;
    }

protected:
    TraderID id_;
    double   avg_latency_ms_;

    double cash_   = 1'000'000.0;
    int    shares_ = 0;

    struct LiveOrder {
        OrderID  id;
        Side     side;
        Qty      qty;
        Price    price;
        SimTime  sent_time;
    };
    std::unordered_map<OrderID, LiveOrder> live_orders_;  // orders with pending ack or in LOB

    std::deque<LogEntry> log_;
    static constexpr int LOG_CAP = 12;

    void    addLog(SimTime now, const std::string& text);
    SimTime sampleDelivery(SimTime now) const;

    NewOrderMsg makeNewOrder(Side side, Qty qty, Price price, SimTime now) const;
    CancelMsg   makeCancel  (OrderID order_id, SimTime now) const;
    ModifyMsg   makeModify  (OrderID order_id, Side side, Qty qty, Price price, SimTime now) const;

    // RNG (shared per-trader instance)
    mutable std::mt19937 rng_;
};
