#pragma once
#include "Types.h"
#include "Message.h"
#include "Order.h"
#include <unordered_map>
#include <vector>
#include <deque>

struct AccountState {
    double cash   = 1'000'000.0;
    int    shares = 0;
};

class Exchange {
public:
    Exchange();

    void processNewOrder(const NewOrderMsg& msg, SimTime now);
    void processCancel  (const CancelMsg&   msg, SimTime now);
    void processModify  (const ModifyMsg&   msg, SimTime now);

    const OrderBook& getBook() const { return book_; }
    AccountState     getAccount(TraderID id) const;

    // Drain outbound queues (called each tick by Simulation)
    std::vector<std::pair<TraderID, ExchToTraderMsg>> drainTraderMessages();
    std::vector<LobUpdateMsg>                          drainLobUpdates();
    std::vector<LogEntry>                              drainGlobalLog();

    // For modify: need to know trader_id of an order to validate ownership
    bool orderExists(OrderID id) const;
    TraderID getOrderOwner(OrderID id) const;

private:
    OrderBook book_;
    std::unordered_map<TraderID, AccountState> accounts_;

    // Maps order_id → trader_id for ownership checks
    std::unordered_map<OrderID, TraderID> order_owners_;
    // Maps order_id → side (needed to reconstruct order for modify)
    std::unordered_map<OrderID, Side>     order_sides_;
    // Maps order_id → cached order info (for modify re-insert)
    struct OrderInfo { TraderID trader_id; Side side; };
    std::unordered_map<OrderID, OrderInfo> order_info_;

    std::vector<std::pair<TraderID, ExchToTraderMsg>> outbound_trader_;
    std::vector<LobUpdateMsg>                          outbound_lob_;
    std::vector<LogEntry>                              global_log_;

    OrderID next_order_id_ = 100000;  // start at 100000 for readability

    void applyFills(const OrderBook::MatchResult& result, TraderID aggressor_id,
                    Side aggressor_side, SimTime now);
    void broadcastLobChanges(SimTime now);
    void addGlobalLog(SimTime now, TraderID tid, const std::string& text);

    std::string formatTime(SimTime t) const;
};
