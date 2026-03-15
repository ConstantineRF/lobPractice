#pragma once
#include "Trader.h"
#include <unordered_set>

class MarketMaker : public Trader {
public:
    MarketMaker(TraderID id, double avg_latency_ms,
                int accumulation_threshold,
                int participation_threshold,
                Price initial_bid_price,
                Price initial_ask_price,
                SimTime patience);

    std::vector<TraderToExchMsg> onTick(SimTime now, const ExchangeView& view) override;
    void onMessage(const ExchToTraderMsg& msg, SimTime now) override;

private:
    int   accum_thresh_;
    int   part_thresh_;
    Price init_bid_;
    Price init_ask_;

    // Track which of our live order IDs are on each side
    std::unordered_set<OrderID> buy_order_ids_;
    std::unordered_set<OrderID> sell_order_ids_;

    // Track qty per order (needed for participation threshold logic)
    std::unordered_map<OrderID, Qty> order_qtys_;

    bool placed_initial_buy_  = false;
    bool placed_initial_sell_ = false;
    bool waiting_ack_buy_     = false;  // sent but not yet acked
    bool waiting_ack_sell_    = false;

    // Patience: tighten spread when idle too long
    SimTime patience_         = 10.0;
    SimTime last_action_time_ = 0.0;   // updated whenever we send any message

    // Cooldown: don't send duplicate aggressive orders
    SimTime last_aggressive_buy_  = -999.0;
    SimTime last_aggressive_sell_ = -999.0;
    static constexpr SimTime AGGRESSIVE_COOLDOWN = 2.0;

    void handleAck      (const NewAckMsg&    msg, SimTime now);
    void handleFill     (const FillMsg&      msg, SimTime now);
    void handleCancelled(const CancelledMsg& msg, SimTime now);

    // Generate orders to maintain book presence and manage accumulation/participation
    std::vector<TraderToExchMsg> generateOrders(SimTime now, const ExchangeView& view);

    int  totalBuyQty()  const;
    int  totalSellQty() const;
    bool hasBuyOrder()  const { return !buy_order_ids_.empty(); }
    bool hasSellOrder() const { return !sell_order_ids_.empty(); }
};
