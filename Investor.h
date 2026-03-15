#pragma once
#include "Trader.h"
#include <optional>

class Investor : public Trader {
public:
    Investor(TraderID id, double avg_latency_ms,
             int max_position,
             std::array<double, NUM_ANALYSTS> analyst_weights);

    std::vector<TraderToExchMsg> onTick(SimTime now, const ExchangeView& view) override;
    void onMessage(const ExchToTraderMsg& msg, SimTime now) override;

    double getFairValue(const std::array<double, NUM_ANALYSTS>& opinions) const override {
        return computeFairValue(opinions);
    }

private:
    int    max_position_;
    std::array<double, NUM_ANALYSTS> weights_;

    // At most one active order at a time
    std::optional<OrderID> active_order_id_;
    Side     active_side_  = Side::BUY;
    Price    active_price_ = 0;
    Qty      active_qty_   = 0;
    SimTime  order_sent_time_ = -1.0;
    bool     waiting_for_ack_ = false;

    static constexpr SimTime MODIFY_TIMEOUT = 5.0;  // seconds before trying to modify
    static constexpr Qty     ORDER_QTY      = 50;   // size per order

    double computeFairValue(const std::array<double, NUM_ANALYSTS>& opinions) const;

    void handleAck      (const NewAckMsg&    msg, SimTime now);
    void handleFill     (const FillMsg&      msg, SimTime now);
    void handleCancelled(const CancelledMsg& msg, SimTime now);
};
