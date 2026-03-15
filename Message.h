#pragma once
#include "Types.h"
#include <variant>
#include <string>

// ─── Trader → Exchange ────────────────────────────────────────────────────────

struct NewOrderMsg {
    TraderID trader_id;
    Side     side;
    Qty      qty;
    Price    price;
    SimTime  sent_time;
    SimTime  delivery_time;
};

struct CancelMsg {
    TraderID trader_id;
    OrderID  order_id;
    SimTime  sent_time;
    SimTime  delivery_time;
};

struct ModifyMsg {
    TraderID trader_id;
    OrderID  order_id;
    Side     side;
    Qty      new_qty;
    Price    new_price;
    SimTime  sent_time;
    SimTime  delivery_time;
};

using TraderToExchMsg = std::variant<NewOrderMsg, CancelMsg, ModifyMsg>;

// Helper: extract delivery_time from any TraderToExchMsg
inline SimTime getDeliveryTime(const TraderToExchMsg& msg) {
    return std::visit([](const auto& m) { return m.delivery_time; }, msg);
}

// Min-heap comparator (earliest delivery first)
struct InboundComparator {
    bool operator()(const TraderToExchMsg& a, const TraderToExchMsg& b) const {
        return getDeliveryTime(a) > getDeliveryTime(b);
    }
};

// ─── Exchange → Trader ────────────────────────────────────────────────────────

struct NewAckMsg {
    TraderID trader_id;
    Side     side;
    Qty      qty;
    Price    price;
    OrderID  order_id;
    SimTime  timestamp;
};

struct FillMsg {
    TraderID trader_id;
    Side     side;     // BUY = BOUGHT, SELL = SOLD (direction of fill for this trader)
    Qty      qty;
    Price    price;
    OrderID  order_id;
    SimTime  timestamp;
};

struct CancelledMsg {
    TraderID trader_id;
    OrderID  order_id;
    SimTime  timestamp;
};

struct CancelRejectMsg {
    TraderID trader_id;
    OrderID  order_id;
    SimTime  timestamp;
};

using ExchToTraderMsg = std::variant<NewAckMsg, FillMsg, CancelledMsg, CancelRejectMsg>;

// ─── Exchange → All (LOB updates) ────────────────────────────────────────────

struct LobUpdateMsg {
    Side    side;
    int     qty_change;  // positive = increased, negative = decreased
    Price   price;
    SimTime timestamp;
};

// ─── Log entries (for display panels) ───────────────────────────────────────

struct LogEntry {
    SimTime   time;
    TraderID  trader_id;   // 0 = exchange-side log
    std::string text;
};
