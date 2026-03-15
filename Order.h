#pragma once
#include "Types.h"
#include <map>
#include <deque>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <optional>

struct Order {
    OrderID  id           = 0;
    TraderID trader_id    = 0;
    Side     side         = Side::BUY;
    Qty      qty          = 0;
    Qty      original_qty = 0;
    Price    price        = 0;
    SimTime  timestamp    = 0.0;
};

class OrderBook {
public:
    struct Level {
        Price price;
        std::vector<std::pair<TraderID, Qty>> orders;  // ordered FIFO
    };

    struct MatchResult {
        // Each fill: first=passive order, second=aggressive order (snapshot at fill time)
        std::vector<std::pair<Order, Order>> fills;
        std::optional<Order> remainder;  // leftover of incoming after all crosses
    };

    MatchResult addOrder(Order o);
    bool        cancelOrder(OrderID id);
    // modifyOrder = cancel + re-insert (resets FIFO priority)
    bool        modifyOrder(OrderID id, Side side, Qty new_qty, Price new_price, SimTime ts);

    bool  hasBid() const;
    bool  hasAsk() const;
    Price bestBid() const;  // returns 0 if empty
    Price bestAsk() const;  // returns 0 if empty

    // Snapshots for renderer (best N levels, best-first)
    std::vector<Level> getBidLevels(int max_levels = 15) const;
    std::vector<Level> getAskLevels(int max_levels = 15) const;

    // Level change deltas since last drain: (side, price, qty_delta)
    std::vector<std::tuple<Side, Price, int>> drainLevelChanges();

    // For participation threshold management: orders for a specific trader on a side
    std::vector<std::pair<OrderID, Price>> getOrdersByTrader(TraderID id, Side side) const;

    int totalQtyForTrader(TraderID id, Side side) const;

private:
    // BUY: highest price first (best bid at top)
    std::map<Price, std::deque<Order>, std::greater<Price>> bids_;
    // SELL: lowest price first (best ask at top)
    std::map<Price, std::deque<Order>> asks_;

    std::unordered_map<OrderID, Price> price_idx_;
    std::unordered_map<OrderID, Side>  side_idx_;

    std::vector<std::tuple<Side, Price, int>> pending_changes_;

    void recordChange(Side side, Price price, int delta);
    void removeFromIndex(OrderID id);
};
