#pragma once
#include "Types.h"
#include <array>

class AnalystSystem {
public:
    AnalystSystem();

    // Call each tick. Returns true if opinions changed this tick.
    bool update(SimTime now);

    const std::array<double, NUM_ANALYSTS>& getOpinions() const { return opinions_; }

private:
    std::array<double, NUM_ANALYSTS> opinions_;
    SimTime next_update_ = 5.0;   // first update at t=5s

    void applyRandomUpdate();
};
