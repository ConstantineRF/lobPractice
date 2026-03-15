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
    bool     cancelling_      = false;  // cancel sent, awaiting CancelledMsg or CancelRejectMsg

    static constexpr SimTime MODIFY_TIMEOUT = 5.0;  // seconds before trying to modify

    double computeFairValue(const std::array<double, NUM_ANALYSTS>& opinions) const;

    // Base size = min(0.5*max_position, 100); doubles for every 3 dollars of mispricing.
    // Capped so the position limit is not breached.
    Qty computeOrderQty(double mispricing_dollars, Qty capacity) const;

    void handleAck      (const NewAckMsg&    msg, SimTime now);
    void handleFill     (const FillMsg&      msg, SimTime now);
    void handleCancelled(const CancelledMsg& msg, SimTime now);
};
