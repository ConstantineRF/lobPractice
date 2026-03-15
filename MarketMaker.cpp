#include "MarketMaker.h"
#include <sstream>
#include <algorithm>

MarketMaker::MarketMaker(TraderID id, double avg_latency_ms,
                         int accumulation_threshold,
                         int participation_threshold,
                         Price initial_bid_price,
                         Price initial_ask_price,
                         SimTime patience)
    : Trader(id, avg_latency_ms)
    , accum_thresh_(accumulation_threshold)
    , part_thresh_(participation_threshold)
    , init_bid_(initial_bid_price)
    , init_ask_(initial_ask_price)
    , patience_(patience)
{}

int MarketMaker::totalBuyQty() const {
    int total = 0;
    for (auto& [oid, qty] : order_qtys_)
        if (buy_order_ids_.count(oid)) total += qty;
    return total;
}

int MarketMaker::totalSellQty() const {
    int total = 0;
    for (auto& [oid, qty] : order_qtys_)
        if (sell_order_ids_.count(oid)) total += qty;
    return total;
}

void MarketMaker::handleAck(const NewAckMsg& msg, SimTime now) {
    OrderID oid = msg.order_id;
    if (msg.side == Side::BUY) {
        buy_order_ids_.insert(oid);
        waiting_ack_buy_ = false;
    } else {
        sell_order_ids_.insert(oid);
        waiting_ack_sell_ = false;
    }
    order_qtys_[oid] = msg.qty;
    live_orders_[oid] = {oid, msg.side, msg.qty, msg.price, now};

    std::string s = "NEWACK " + sideStr(msg.side) + " qty=" +
        std::to_string(msg.qty) + " @$" +
        std::to_string(centsToDouble(msg.price)).substr(0,6) +
        " id=" + std::to_string(oid);
    addLog(now, s);
}

void MarketMaker::handleFill(const FillMsg& msg, SimTime now) {
    // Update local account shadow
    double dollars = msg.qty * centsToDouble(msg.price);
    if (msg.side == Side::BUY) {
        cash_   -= dollars;
        shares_ += msg.qty;
    } else {
        cash_   += dollars;
        shares_ -= msg.qty;
    }

    // Reduce tracked qty for this order
    auto it = order_qtys_.find(msg.order_id);
    if (it != order_qtys_.end()) {
        it->second -= msg.qty;
        if (it->second <= 0) {
            order_qtys_.erase(it);
            buy_order_ids_.erase(msg.order_id);
            sell_order_ids_.erase(msg.order_id);
            live_orders_.erase(msg.order_id);
        }
    }

    std::string s = "FILL " + filledStr(msg.side) + " qty=" +
        std::to_string(msg.qty) + " @$" +
        std::to_string(centsToDouble(msg.price)).substr(0,6) +
        " id=" + std::to_string(msg.order_id);
    addLog(now, s);
}

void MarketMaker::handleCancelled(const CancelledMsg& msg, SimTime now) {
    buy_order_ids_.erase(msg.order_id);
    sell_order_ids_.erase(msg.order_id);
    order_qtys_.erase(msg.order_id);
    live_orders_.erase(msg.order_id);
    addLog(now, "CANCELLED id=" + std::to_string(msg.order_id));
}

void MarketMaker::onMessage(const ExchToTraderMsg& msg, SimTime now) {
    std::visit([&](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, NewAckMsg>)            handleAck(m, now);
        else if constexpr (std::is_same_v<T, FillMsg>)         handleFill(m, now);
        else if constexpr (std::is_same_v<T, CancelledMsg>)    handleCancelled(m, now);
        else if constexpr (std::is_same_v<T, CancelRejectMsg>) addLog(now, "CANCELREJECT id=" + std::to_string(m.order_id));
        else if constexpr (std::is_same_v<T, ModifyRejectMsg>) addLog(now, "MODIFYREJECT id=" + std::to_string(m.order_id));
    }, msg);
}

std::vector<TraderToExchMsg> MarketMaker::onTick(SimTime now, const ExchangeView& view) {
    return generateOrders(now, view);
}

std::vector<TraderToExchMsg> MarketMaker::generateOrders(SimTime now, const ExchangeView& view) {
    std::vector<TraderToExchMsg> out;
    int abs_pos = std::abs(shares_);

    // ── 1. Place initial orders (within first 2 seconds) ─────────────────────
    if (!placed_initial_buy_ && !waiting_ack_buy_) {
        out.push_back(makeNewOrder(Side::BUY, 100, init_bid_, now));
        placed_initial_buy_ = true;
        waiting_ack_buy_    = true;
        addLog(now, "NEW BUY 100 @$" + std::to_string(centsToDouble(init_bid_)).substr(0,6));
    }
    if (!placed_initial_sell_ && !waiting_ack_sell_) {
        out.push_back(makeNewOrder(Side::SELL, 100, init_ask_, now));
        placed_initial_sell_ = true;
        waiting_ack_sell_    = true;
        addLog(now, "NEW SELL 100 @$" + std::to_string(centsToDouble(init_ask_)).substr(0,6));
    }

    // ── 2. Re-quote if a side is empty (must always keep ≥1 bid and ≥1 ask) ──
    if (!hasBuyOrder() && !waiting_ack_buy_ && view.has_bid) {
        Price bid_price = view.best_bid - TICK;  // just below best bid
        if (!view.has_bid) bid_price = view.best_ask - 10 * TICK;
        out.push_back(makeNewOrder(Side::BUY, 100, bid_price, now));
        waiting_ack_buy_ = true;
        addLog(now, "REQUOTE BUY 100 @$" + std::to_string(centsToDouble(bid_price)).substr(0,6));
    }
    if (!hasSellOrder() && !waiting_ack_sell_ && view.has_ask) {
        Price ask_price = view.best_ask + TICK;
        if (!view.has_ask) ask_price = view.best_bid + 10 * TICK;
        out.push_back(makeNewOrder(Side::SELL, 100, ask_price, now));
        waiting_ack_sell_ = true;
        addLog(now, "REQUOTE SELL 100 @$" + std::to_string(centsToDouble(ask_price)).substr(0,6));
    }

    // ── 3. Accumulation threshold logic ───────────────────────────────────────
    if (abs_pos >= accum_thresh_ && abs_pos < 2 * accum_thresh_) {
        // Send one aggressive order on the reducing side (+0.01 past best)
        if (shares_ > 0 && !waiting_ack_sell_ &&
            now - last_aggressive_sell_ > AGGRESSIVE_COOLDOWN && view.has_bid) {
            Price p = view.best_bid + TICK;  // better than best bid to attract buyers? No.
            // Aggressive SELL means pricing at best_bid (crosses immediately) or just below ask
            // Here: price 1 tick inside the spread (better than current best ask)
            p = view.has_ask ? view.best_ask - TICK : view.best_bid + TICK;
            if (p > view.best_bid) {  // don't cross with our own side
                out.push_back(makeNewOrder(Side::SELL, 100, p, now));
                last_aggressive_sell_ = now;
                waiting_ack_sell_ = true;
                addLog(now, "AGGR SELL 100 @$" + std::to_string(centsToDouble(p)).substr(0,6));
            }
        }
        if (shares_ < 0 && !waiting_ack_buy_ &&
            now - last_aggressive_buy_ > AGGRESSIVE_COOLDOWN && view.has_ask) {
            Price p = view.has_bid ? view.best_bid + TICK : view.best_ask - TICK;
            if (p < view.best_ask) {
                out.push_back(makeNewOrder(Side::BUY, 100, p, now));
                last_aggressive_buy_ = now;
                waiting_ack_buy_ = true;
                addLog(now, "AGGR BUY 100 @$" + std::to_string(centsToDouble(p)).substr(0,6));
            }
        }
    } else if (abs_pos >= 2 * accum_thresh_) {
        // Marketable limit order (at best opposing price — crosses immediately)
        if (shares_ > 0 && !waiting_ack_sell_ &&
            now - last_aggressive_sell_ > AGGRESSIVE_COOLDOWN && view.has_bid) {
            Price p = view.best_bid;  // sell at best bid → immediate cross
            out.push_back(makeNewOrder(Side::SELL, 100, p, now));
            last_aggressive_sell_ = now;
            waiting_ack_sell_ = true;
            addLog(now, "MKT SELL 100 @$" + std::to_string(centsToDouble(p)).substr(0,6));
        }
        if (shares_ < 0 && !waiting_ack_buy_ &&
            now - last_aggressive_buy_ > AGGRESSIVE_COOLDOWN && view.has_ask) {
            Price p = view.best_ask;  // buy at best ask → immediate cross
            out.push_back(makeNewOrder(Side::BUY, 100, p, now));
            last_aggressive_buy_ = now;
            waiting_ack_buy_ = true;
            addLog(now, "MKT BUY 100 @$" + std::to_string(centsToDouble(p)).substr(0,6));
        }
    }

    // ── 4. Participation threshold: cancel least-aggressive excess orders ─────
    // BUY side
    if (totalBuyQty() > part_thresh_) {
        // Collect buy orders sorted by price ascending (least aggressive = lowest price first)
        std::vector<std::pair<Price, OrderID>> buys;
        for (OrderID oid : buy_order_ids_) {
            auto it = live_orders_.find(oid);
            if (it != live_orders_.end())
                buys.push_back({it->second.price, oid});
        }
        std::sort(buys.begin(), buys.end());  // ascending = least aggressive first
        for (auto& [price, oid] : buys) {
            if (totalBuyQty() <= part_thresh_) break;
            out.push_back(makeCancel(oid, now));
            // Optimistically remove from tracking (will be confirmed by CancelledMsg)
            buy_order_ids_.erase(oid);
            order_qtys_.erase(oid);
            live_orders_.erase(oid);
            addLog(now, "CANCEL BUY id=" + std::to_string(oid) + " (part thresh)");
        }
    }
    // SELL side
    if (totalSellQty() > part_thresh_) {
        std::vector<std::pair<Price, OrderID>> sells;
        for (OrderID oid : sell_order_ids_) {
            auto it = live_orders_.find(oid);
            if (it != live_orders_.end())
                sells.push_back({it->second.price, oid});
        }
        std::sort(sells.rbegin(), sells.rend());  // descending = least aggressive first
        for (auto& [price, oid] : sells) {
            if (totalSellQty() <= part_thresh_) break;
            out.push_back(makeCancel(oid, now));
            sell_order_ids_.erase(oid);
            order_qtys_.erase(oid);
            live_orders_.erase(oid);
            addLog(now, "CANCEL SELL id=" + std::to_string(oid) + " (part thresh)");
        }
    }

    // ── 5. Patience: tighten spread when idle for too long ────────────────────
    if (now - last_action_time_ > patience_ && view.has_bid && view.has_ask) {
        Price spread = view.best_ask - view.best_bid;

        if (shares_ > 0 && spread > TICK && !waiting_ack_sell_) {
            Price p = view.best_ask - TICK;
            if (p > view.best_bid) {  // safety: don't cross
                Qty q = std::min(100, shares_);
                out.push_back(makeNewOrder(Side::SELL, q, p, now));
                waiting_ack_sell_ = true;
                addLog(now, "PATIENCE SELL " + std::to_string(q) + " @$" +
                    std::to_string(centsToDouble(p)).substr(0,6));
            }
        } else if (shares_ < 0 && spread > TICK && !waiting_ack_buy_) {
            Price p = view.best_bid + TICK;
            if (p < view.best_ask) {  // safety: don't cross
                Qty q = std::min(100, -shares_);
                out.push_back(makeNewOrder(Side::BUY, q, p, now));
                waiting_ack_buy_ = true;
                addLog(now, "PATIENCE BUY " + std::to_string(q) + " @$" +
                    std::to_string(centsToDouble(p)).substr(0,6));
            }
        } else if (shares_ == 0 && spread >= 3 * TICK) {
            Price bid_p = view.best_bid + TICK;
            Price ask_p = view.best_ask - TICK;
            if (bid_p < ask_p) {  // safety: prices don't cross
                if (!waiting_ack_buy_) {
                    out.push_back(makeNewOrder(Side::BUY, 100, bid_p, now));
                    waiting_ack_buy_ = true;
                    addLog(now, "PATIENCE BUY 100 @$" +
                        std::to_string(centsToDouble(bid_p)).substr(0,6));
                }
                if (!waiting_ack_sell_) {
                    out.push_back(makeNewOrder(Side::SELL, 100, ask_p, now));
                    waiting_ack_sell_ = true;
                    addLog(now, "PATIENCE SELL 100 @$" +
                        std::to_string(centsToDouble(ask_p)).substr(0,6));
                }
            }
        }
    }

    // Track last action time so patience timer resets after any sent message
    if (!out.empty()) last_action_time_ = now;

    return out;
}
