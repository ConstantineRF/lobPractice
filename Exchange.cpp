#include "Exchange.h"
#include <sstream>
#include <iomanip>

Exchange::Exchange() {
    for (int i = 1; i <= NUM_TRADERS; ++i)
        accounts_[i] = AccountState{};
}

std::string Exchange::formatTime(SimTime t) const {
    int total_ms = static_cast<int>(t * 1000.0);
    int ms  = total_ms % 1000;
    int sec = (total_ms / 1000) % 60;
    int min = (total_ms / 60000) % 60;
    int hr  = (total_ms / 3600000) % 24;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", hr, min, sec, ms);
    return buf;
}

void Exchange::addGlobalLog(SimTime now, TraderID tid, const std::string& text) {
    global_log_.push_back({now, tid, text});
}

void Exchange::processNewOrder(const NewOrderMsg& msg, SimTime now) {
    OrderID oid = next_order_id_++;

    // Send NEWACK immediately
    NewAckMsg ack{msg.trader_id, msg.side, msg.qty, msg.price, oid, now};
    outbound_trader_.push_back({msg.trader_id, ack});

    std::string log_text = "NEWACK " + sideStr(msg.side) + " " +
        std::to_string(msg.trader_id) + " " + std::to_string(msg.qty) + " " +
        std::to_string(centsToDouble(msg.price)).substr(0,6) + " " +
        std::to_string(oid) + " " + formatTime(now);
    addGlobalLog(now, msg.trader_id, log_text);

    // Build and attempt to match the order
    Order o;
    o.id           = oid;
    o.trader_id    = msg.trader_id;
    o.side         = msg.side;
    o.qty          = msg.qty;
    o.original_qty = msg.qty;
    o.price        = msg.price;
    o.timestamp    = now;

    auto result = book_.addOrder(o);

    // Register ownership for each fill's passive order removal is already handled
    if (result.remainder.has_value()) {
        order_owners_[oid] = msg.trader_id;
        order_info_[oid]   = {msg.trader_id, msg.side};
    }

    applyFills(result, msg.trader_id, msg.side, now);
    broadcastLobChanges(now);
}

void Exchange::processCancel(const CancelMsg& msg, SimTime now) {
    auto it = order_owners_.find(msg.order_id);
    if (it == order_owners_.end() || it->second != msg.trader_id) {
        // Order doesn't exist or wrong owner
        CancelRejectMsg rej{msg.trader_id, msg.order_id, now};
        outbound_trader_.push_back({msg.trader_id, rej});
        addGlobalLog(now, msg.trader_id, "CANCELREJECT " +
            std::to_string(msg.trader_id) + " " + std::to_string(msg.order_id) +
            " " + formatTime(now));
        return;
    }

    bool ok = book_.cancelOrder(msg.order_id);
    if (ok) {
        order_owners_.erase(msg.order_id);
        order_info_.erase(msg.order_id);
        CancelledMsg cm{msg.trader_id, msg.order_id, now};
        outbound_trader_.push_back({msg.trader_id, cm});
        addGlobalLog(now, msg.trader_id, "CANCELLED " +
            std::to_string(msg.trader_id) + " " + std::to_string(msg.order_id) +
            " " + formatTime(now));
    } else {
        CancelRejectMsg rej{msg.trader_id, msg.order_id, now};
        outbound_trader_.push_back({msg.trader_id, rej});
        addGlobalLog(now, msg.trader_id, "CANCELREJECT " +
            std::to_string(msg.trader_id) + " " + std::to_string(msg.order_id) +
            " " + formatTime(now));
    }
    broadcastLobChanges(now);
}

void Exchange::processModify(const ModifyMsg& msg, SimTime now) {
    auto it = order_owners_.find(msg.order_id);
    if (it == order_owners_.end() || it->second != msg.trader_id) {
        // Order gone — send cancel reject (spec: CANCELREJECT for missing order)
        CancelRejectMsg rej{msg.trader_id, msg.order_id, now};
        outbound_trader_.push_back({msg.trader_id, rej});
        addGlobalLog(now, msg.trader_id, "CANCELREJECT(MOD) " +
            std::to_string(msg.trader_id) + " " + std::to_string(msg.order_id) +
            " " + formatTime(now));
        return;
    }

    // Cancel the old order
    book_.cancelOrder(msg.order_id);
    order_owners_.erase(msg.order_id);
    order_info_.erase(msg.order_id);

    // Re-insert as a new order (same ID re-used, new timestamp → loses FIFO priority)
    // We create a fresh NewOrderMsg internally
    Order o;
    o.id           = msg.order_id;  // preserve order ID
    o.trader_id    = msg.trader_id;
    o.side         = msg.side;
    o.qty          = msg.new_qty;
    o.original_qty = msg.new_qty;
    o.price        = msg.new_price;
    o.timestamp    = now;

    auto result = book_.addOrder(o);

    // NEWACK with same order_id
    NewAckMsg ack{msg.trader_id, msg.side, msg.new_qty, msg.new_price, msg.order_id, now};
    outbound_trader_.push_back({msg.trader_id, ack});
    addGlobalLog(now, msg.trader_id, "NEWACK(MOD) " + sideStr(msg.side) + " " +
        std::to_string(msg.trader_id) + " " + std::to_string(msg.new_qty) + " " +
        std::to_string(centsToDouble(msg.new_price)).substr(0,6) + " " +
        std::to_string(msg.order_id) + " " + formatTime(now));

    if (result.remainder.has_value()) {
        order_owners_[msg.order_id] = msg.trader_id;
        order_info_[msg.order_id]   = {msg.trader_id, msg.side};
    }

    applyFills(result, msg.trader_id, msg.side, now);
    broadcastLobChanges(now);
}

void Exchange::applyFills(const OrderBook::MatchResult& result, TraderID aggressor_id,
                           Side aggressor_side, SimTime now) {
    for (auto& [passive, aggressor] : result.fills) {
        Price fill_price = passive.price;
        Qty   fill_qty   = passive.qty;  // snapshots have fill_qty set

        TraderID passive_id   = passive.trader_id;
        TraderID aggressor_id = aggressor.trader_id;

        // Passive side: they had the resting order
        Side passive_side = passive.side;
        // Aggressor matched against the passive

        // Update accounts on exchange side
        // Buyer: cash -= qty * price_in_dollars, shares += qty
        // Seller: cash += qty * price_in_dollars, shares -= qty
        double dollars = fill_qty * centsToDouble(fill_price);

        if (passive_side == Side::BUY) {
            // passive was a buyer resting, aggressor was a seller
            accounts_[passive_id].cash   -= dollars;
            accounts_[passive_id].shares += fill_qty;
            accounts_[aggressor_id].cash   += dollars;
            accounts_[aggressor_id].shares -= fill_qty;

            FillMsg fm_passive{passive_id, Side::BUY, fill_qty, fill_price, passive.id, now};
            FillMsg fm_aggressor{aggressor_id, Side::SELL, fill_qty, fill_price, aggressor.id, now};
            outbound_trader_.push_back({passive_id, fm_passive});
            outbound_trader_.push_back({aggressor_id, fm_aggressor});

            std::string ts = formatTime(now);
            addGlobalLog(now, passive_id,   "FILL BOUGHT " + std::to_string(passive_id) +
                " " + std::to_string(fill_qty) + " " +
                std::to_string(centsToDouble(fill_price)).substr(0,6) + " " +
                std::to_string(passive.id) + " " + ts);
            addGlobalLog(now, aggressor_id, "FILL SOLD " + std::to_string(aggressor_id) +
                " " + std::to_string(fill_qty) + " " +
                std::to_string(centsToDouble(fill_price)).substr(0,6) + " " +
                std::to_string(aggressor.id) + " " + ts);
        } else {
            // passive was a seller resting, aggressor was a buyer
            accounts_[aggressor_id].cash   -= dollars;
            accounts_[aggressor_id].shares += fill_qty;
            accounts_[passive_id].cash   += dollars;
            accounts_[passive_id].shares -= fill_qty;

            FillMsg fm_passive{passive_id, Side::SELL, fill_qty, fill_price, passive.id, now};
            FillMsg fm_aggressor{aggressor_id, Side::BUY, fill_qty, fill_price, aggressor.id, now};
            outbound_trader_.push_back({passive_id, fm_passive});
            outbound_trader_.push_back({aggressor_id, fm_aggressor});

            std::string ts = formatTime(now);
            addGlobalLog(now, passive_id,   "FILL SOLD " + std::to_string(passive_id) +
                " " + std::to_string(fill_qty) + " " +
                std::to_string(centsToDouble(fill_price)).substr(0,6) + " " +
                std::to_string(passive.id) + " " + ts);
            addGlobalLog(now, aggressor_id, "FILL BOUGHT " + std::to_string(aggressor_id) +
                " " + std::to_string(fill_qty) + " " +
                std::to_string(centsToDouble(fill_price)).substr(0,6) + " " +
                std::to_string(aggressor.id) + " " + ts);
        }

        // Remove fully-filled passive from ownership tracking
        // (The order book already removed it from the book)
        if (passive.qty == fill_qty) {  // fully filled (snapshot qty == original fill qty)
            // Check if completely gone from book (handled by addOrder already)
            order_owners_.erase(passive.id);
            order_info_.erase(passive.id);
        }
    }
}

void Exchange::broadcastLobChanges(SimTime now) {
    auto changes = book_.drainLevelChanges();
    for (auto& [side, price, delta] : changes) {
        if (delta == 0) continue;
        LobUpdateMsg upd{side, delta, price, now};
        outbound_lob_.push_back(upd);
    }
}

AccountState Exchange::getAccount(TraderID id) const {
    auto it = accounts_.find(id);
    if (it == accounts_.end()) return AccountState{};
    return it->second;
}

std::vector<std::pair<TraderID, ExchToTraderMsg>> Exchange::drainTraderMessages() {
    auto out = std::move(outbound_trader_);
    outbound_trader_.clear();
    return out;
}

std::vector<LobUpdateMsg> Exchange::drainLobUpdates() {
    auto out = std::move(outbound_lob_);
    outbound_lob_.clear();
    return out;
}

std::vector<LogEntry> Exchange::drainGlobalLog() {
    auto out = std::move(global_log_);
    global_log_.clear();
    return out;
}

bool Exchange::orderExists(OrderID id) const {
    return order_owners_.find(id) != order_owners_.end();
}

TraderID Exchange::getOrderOwner(OrderID id) const {
    auto it = order_owners_.find(id);
    return (it != order_owners_.end()) ? it->second : 0;
}
