#include "Investor.h"
#include <numeric>
#include <sstream>

Investor::Investor(TraderID id, double avg_latency_ms,
                   int max_position,
                   std::array<double, NUM_ANALYSTS> analyst_weights)
    : Trader(id, avg_latency_ms)
    , max_position_(max_position)
    , weights_(analyst_weights)
{}

double Investor::computeFairValue(const std::array<double, NUM_ANALYSTS>& opinions) const {
    double fv = 0.0;
    for (int i = 0; i < NUM_ANALYSTS; ++i)
        fv += weights_[i] * opinions[i];
    return fv;
}

void Investor::handleAck(const NewAckMsg& msg, SimTime now) {
    active_order_id_  = msg.order_id;
    active_side_      = msg.side;
    active_price_     = msg.price;
    active_qty_       = msg.qty;
    waiting_for_ack_  = false;
    live_orders_[msg.order_id] = {msg.order_id, msg.side, msg.qty, msg.price, now};
    addLog(now, "NEWACK " + sideStr(msg.side) + " qty=" + std::to_string(msg.qty) +
        " @$" + std::to_string(centsToDouble(msg.price)).substr(0,6) +
        " id=" + std::to_string(msg.order_id));
}

void Investor::handleFill(const FillMsg& msg, SimTime now) {
    double dollars = msg.qty * centsToDouble(msg.price);
    if (msg.side == Side::BUY) {
        cash_   -= dollars;
        shares_ += msg.qty;
    } else {
        cash_   += dollars;
        shares_ -= msg.qty;
    }
    // Update active order quantity
    if (active_order_id_.has_value() && active_order_id_.value() == msg.order_id) {
        active_qty_ -= msg.qty;
        if (active_qty_ <= 0) {
            active_order_id_.reset();
            live_orders_.erase(msg.order_id);
        }
    }
    addLog(now, "FILL " + filledStr(msg.side) + " qty=" + std::to_string(msg.qty) +
        " @$" + std::to_string(centsToDouble(msg.price)).substr(0,6));
}

void Investor::handleCancelled(const CancelledMsg& msg, SimTime now) {
    if (active_order_id_.has_value() && active_order_id_.value() == msg.order_id) {
        active_order_id_.reset();
        live_orders_.erase(msg.order_id);
    }
    addLog(now, "CANCELLED id=" + std::to_string(msg.order_id));
}

void Investor::onMessage(const ExchToTraderMsg& msg, SimTime now) {
    std::visit([&](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, NewAckMsg>)         handleAck(m, now);
        else if constexpr (std::is_same_v<T, FillMsg>)      handleFill(m, now);
        else if constexpr (std::is_same_v<T, CancelledMsg>) handleCancelled(m, now);
        else {
            // CANCELREJECT: order already gone, clear our state
            if (active_order_id_.has_value() && active_order_id_.value() == m.order_id) {
                active_order_id_.reset();
                waiting_for_ack_ = false;
            }
            addLog(now, "CANCELREJECT id=" + std::to_string(m.order_id));
        }
    }, msg);
}

std::vector<TraderToExchMsg> Investor::onTick(SimTime now, const ExchangeView& view) {
    std::vector<TraderToExchMsg> out;

    if (!view.has_bid && !view.has_ask) return out;  // no market yet

    double fv = computeFairValue(view.analyst_opinions);

    bool want_buy  = fv > centsToDouble(view.best_ask) && shares_ < max_position_  && view.has_ask;
    bool want_sell = fv < centsToDouble(view.best_bid)  && shares_ > -max_position_ && view.has_bid;

    // ── If we have an active order, manage it ───────────────────────────────
    if (active_order_id_.has_value() && !waiting_for_ack_) {
        bool still_favorable = (active_side_ == Side::BUY && want_buy) ||
                               (active_side_ == Side::SELL && want_sell);

        if (!still_favorable) {
            // Cancel the order
            out.push_back(makeCancel(active_order_id_.value(), now));
            active_order_id_.reset();
            addLog(now, "CANCEL (no longer favorable)");
            return out;
        }

        // Check if 5s have passed without full fill → modify to better price
        if (now - order_sent_time_ > MODIFY_TIMEOUT) {
            Price new_price = active_price_;
            if (active_side_ == Side::BUY && view.has_ask) {
                new_price = view.best_ask;  // lift to current ask
            } else if (active_side_ == Side::SELL && view.has_bid) {
                new_price = view.best_bid;  // hit current bid
            }
            if (new_price != active_price_) {
                out.push_back(makeModify(active_order_id_.value(), active_side_,
                                         active_qty_, new_price, now));
                active_price_    = new_price;
                order_sent_time_ = now;
                waiting_for_ack_ = true;
                addLog(now, "MODIFY " + sideStr(active_side_) + " @$" +
                    std::to_string(centsToDouble(new_price)).substr(0,6));
            } else {
                order_sent_time_ = now;  // reset timer — price hasn't changed
            }
        }
        return out;
    }

    // ── No active order: decide whether to submit one ───────────────────────
    if (waiting_for_ack_) return out;  // already sent, waiting

    if (want_buy) {
        Price p = view.best_ask;
        Qty   q = std::min(ORDER_QTY, max_position_ - shares_);
        if (q > 0) {
            out.push_back(makeNewOrder(Side::BUY, q, p, now));
            order_sent_time_ = now;
            waiting_for_ack_ = true;
            addLog(now, "NEW BUY " + std::to_string(q) + " @$" +
                std::to_string(centsToDouble(p)).substr(0,6) +
                " (fv=$" + std::to_string(fv).substr(0,6) + ")");
        }
    } else if (want_sell) {
        Price p = view.best_bid;
        Qty   q = std::min(ORDER_QTY, max_position_ + shares_);
        if (q > 0) {
            out.push_back(makeNewOrder(Side::SELL, q, p, now));
            order_sent_time_ = now;
            waiting_for_ack_ = true;
            addLog(now, "NEW SELL " + std::to_string(q) + " @$" +
                std::to_string(centsToDouble(p)).substr(0,6) +
                " (fv=$" + std::to_string(fv).substr(0,6) + ")");
        }
    }

    return out;
}
