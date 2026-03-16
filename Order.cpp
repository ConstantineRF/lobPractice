#include "Order.h"
#include <algorithm>

void OrderBook::recordChange(Side side, Price price, int delta) {
    // Merge with existing pending change at same (side, price) if any
    for (auto& [s, p, d] : pending_changes_) {
        if (s == side && p == price) {
            d += delta;
            return;
        }
    }
    pending_changes_.push_back({side, price, delta});
}

void OrderBook::removeFromIndex(OrderID id) {
    price_idx_.erase(id);
    side_idx_.erase(id);
}

OrderBook::MatchResult OrderBook::addOrder(Order o) {
    MatchResult result;

    if (o.side == Side::BUY) {
        // Cross against asks with price <= o.price
        while (o.qty > 0 && !asks_.empty()) {
            auto it = asks_.begin();
            if (it->first > o.price) break;  // best ask too expensive

            auto& queue = it->second;
            Order& passive = queue.front();

            int fill_qty = std::min(o.qty, passive.qty);

            // Snapshot both at fill time
            Order passive_snap = passive;
            Order aggressor_snap = o;
            passive_snap.qty = fill_qty;
            aggressor_snap.qty = fill_qty;
            result.fills.push_back({passive_snap, aggressor_snap});

            // Reduce quantities
            passive.qty -= fill_qty;
            o.qty -= fill_qty;
            recordChange(Side::SELL, it->first, -fill_qty);

            if (passive.qty == 0) {
                removeFromIndex(passive.id);
                queue.pop_front();
                if (queue.empty()) asks_.erase(it);
            }
        }
        if (o.qty > 0) {
            recordChange(Side::BUY, o.price, +o.qty);
            price_idx_[o.id] = o.price;
            side_idx_[o.id]  = Side::BUY;
            bids_[o.price].push_back(o);
            result.remainder = o;
        }
    } else {
        // SELL: cross against bids with price >= o.price
        while (o.qty > 0 && !bids_.empty()) {
            auto it = bids_.begin();
            if (it->first < o.price) break;  // best bid too low

            auto& queue = it->second;
            Order& passive = queue.front();

            int fill_qty = std::min(o.qty, passive.qty);

            Order passive_snap = passive;
            Order aggressor_snap = o;
            passive_snap.qty = fill_qty;
            aggressor_snap.qty = fill_qty;
            result.fills.push_back({passive_snap, aggressor_snap});

            passive.qty -= fill_qty;
            o.qty -= fill_qty;
            recordChange(Side::BUY, it->first, -fill_qty);

            if (passive.qty == 0) {
                removeFromIndex(passive.id);
                queue.pop_front();
                if (queue.empty()) bids_.erase(it);
            }
        }
        if (o.qty > 0) {
            recordChange(Side::SELL, o.price, +o.qty);
            price_idx_[o.id] = o.price;
            side_idx_[o.id]  = Side::SELL;
            asks_[o.price].push_back(o);
            result.remainder = o;
        }
    }

    return result;
}

bool OrderBook::cancelOrder(OrderID id) {
    auto sit = side_idx_.find(id);
    auto pit = price_idx_.find(id);
    if (sit == side_idx_.end() || pit == price_idx_.end()) return false;

    Side  side  = sit->second;
    Price price = pit->second;

    if (side == Side::BUY) {
        auto mit = bids_.find(price);
        if (mit != bids_.end()) {
            auto& q = mit->second;
            for (auto it = q.begin(); it != q.end(); ++it) {
                if (it->id == id) {
                    recordChange(Side::BUY, price, -it->qty);
                    q.erase(it);
                    break;
                }
            }
            if (q.empty()) bids_.erase(mit);
        }
    } else {
        auto mit = asks_.find(price);
        if (mit != asks_.end()) {
            auto& q = mit->second;
            for (auto it = q.begin(); it != q.end(); ++it) {
                if (it->id == id) {
                    recordChange(Side::SELL, price, -it->qty);
                    q.erase(it);
                    break;
                }
            }
            if (q.empty()) asks_.erase(mit);
        }
    }

    removeFromIndex(id);
    return true;
}

bool OrderBook::modifyOrder(OrderID id, Side side, Qty new_qty, Price new_price, SimTime ts) {
    if (!cancelOrder(id)) return false;
    Order o;
    o.id           = id;  // reuse same ID
    o.trader_id    = 0;   // caller must set this — see Exchange.cpp
    o.side         = side;
    o.qty          = new_qty;
    o.original_qty = new_qty;
    o.price        = new_price;
    o.timestamp    = ts;
    // Note: addOrder will be called by Exchange after setting trader_id
    return true;
}

bool OrderBook::isResting(OrderID id) const {
    return side_idx_.find(id) != side_idx_.end();
}

bool OrderBook::hasBid() const { return !bids_.empty(); }
bool OrderBook::hasAsk() const { return !asks_.empty(); }

Price OrderBook::bestBid() const {
    return bids_.empty() ? 0 : bids_.begin()->first;
}
Price OrderBook::bestAsk() const {
    return asks_.empty() ? 0 : asks_.begin()->first;
}

std::vector<OrderBook::Level> OrderBook::getBidLevels(int max_levels) const {
    std::vector<Level> result;
    for (auto& [price, q] : bids_) {
        if ((int)result.size() >= max_levels) break;
        Level lvl;
        lvl.price = price;
        for (auto& o : q)
            lvl.orders.push_back({o.trader_id, o.qty});
        result.push_back(std::move(lvl));
    }
    return result;
}

std::vector<OrderBook::Level> OrderBook::getAskLevels(int max_levels) const {
    std::vector<Level> result;
    for (auto& [price, q] : asks_) {
        if ((int)result.size() >= max_levels) break;
        Level lvl;
        lvl.price = price;
        for (auto& o : q)
            lvl.orders.push_back({o.trader_id, o.qty});
        result.push_back(std::move(lvl));
    }
    return result;
}

std::vector<std::tuple<Side, Price, int>> OrderBook::drainLevelChanges() {
    auto out = std::move(pending_changes_);
    pending_changes_.clear();
    return out;
}

std::vector<std::pair<OrderID, Price>> OrderBook::getOrdersByTrader(TraderID id, Side side) const {
    std::vector<std::pair<OrderID, Price>> result;
    if (side == Side::BUY) {
        for (auto& [price, q] : bids_)
            for (auto& o : q)
                if (o.trader_id == id)
                    result.push_back({o.id, price});
    } else {
        for (auto& [price, q] : asks_)
            for (auto& o : q)
                if (o.trader_id == id)
                    result.push_back({o.id, price});
    }
    return result;
}

int OrderBook::totalQtyForTrader(TraderID id, Side side) const {
    int total = 0;
    if (side == Side::BUY) {
        for (auto& [price, q] : bids_)
            for (auto& o : q)
                if (o.trader_id == id) total += o.qty;
    } else {
        for (auto& [price, q] : asks_)
            for (auto& o : q)
                if (o.trader_id == id) total += o.qty;
    }
    return total;
}
