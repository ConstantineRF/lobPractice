#include "Trader.h"
#include <sstream>
#include <iomanip>

Trader::Trader(TraderID id, double avg_latency_ms)
    : id_(id), avg_latency_ms_(avg_latency_ms), rng_(std::random_device{}()) {}

void Trader::addLog(SimTime now, const std::string& text) {
    log_.push_back({now, id_, text});
    while ((int)log_.size() > LOG_CAP)
        log_.pop_front();
}

SimTime Trader::sampleDelivery(SimTime now) const {
    std::uniform_real_distribution<double> dist(0.8, 1.2);
    double latency_s = avg_latency_ms_ * dist(rng_) / 1000.0;
    SimTime delivery = std::max(now + latency_s, last_outbound_delivery_);
    last_outbound_delivery_ = delivery;
    return delivery;
}

NewOrderMsg Trader::makeNewOrder(Side side, Qty qty, Price price, SimTime now) const {
    NewOrderMsg msg;
    msg.trader_id    = id_;
    msg.side         = side;
    msg.qty          = qty;
    msg.price        = price;
    msg.sent_time    = now;
    msg.delivery_time = sampleDelivery(now);
    return msg;
}

CancelMsg Trader::makeCancel(OrderID order_id, SimTime now) const {
    CancelMsg msg;
    msg.trader_id    = id_;
    msg.order_id     = order_id;
    msg.sent_time    = now;
    msg.delivery_time = sampleDelivery(now);
    return msg;
}

ModifyMsg Trader::makeModify(OrderID order_id, Side side, Qty qty, Price price, SimTime now) {
    ModifyMsg msg;
    msg.trader_id    = id_;
    msg.order_id     = order_id;
    msg.side         = side;
    msg.new_qty      = qty;
    msg.new_price    = price;
    msg.sent_time    = now;
    msg.delivery_time = sampleDelivery(now);
    addLog(now, "-> MODIFY " + sideStr(side) + " @$" +
        std::to_string(centsToDouble(price)).substr(0, 6) +
        " id=" + std::to_string(order_id));
    return msg;
}
