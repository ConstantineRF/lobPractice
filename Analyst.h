#pragma once
#include "Types.h"
#include <array>

class AnalystSystem {
public:
    AnalystSystem();

    // Call each tick. Returns true if opinions changed this tick.
    // midquote: current (best_bid + best_ask) / 2 in dollars; has_midquote false if book is one-sided or empty.
    bool update(SimTime now, double midquote, bool has_midquote);

    const std::array<double, NUM_ANALYSTS>& getOpinions() const { return opinions_; }

private:
    std::array<double, NUM_ANALYSTS> opinions_;
    SimTime next_update_ = 5.0;   // first update at t=5s

    void applyRandomUpdate(double midquote, bool has_midquote);
    void applyNewsEvent();
};
