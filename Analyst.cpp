#include "Analyst.h"
#include <random>
#include <algorithm>
#include <numeric>

static std::mt19937& rng() {
    static std::mt19937 gen(std::random_device{}());
    return gen;
}

AnalystSystem::AnalystSystem() {
    opinions_.fill(50.0);
}

bool AnalystSystem::update(SimTime now, double midquote, bool has_midquote) {
    if (now < next_update_) return false;
    next_update_ = now + 15.0;

    // 25% chance of a news event; otherwise normal per-analyst update
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    if (coin(rng()) < 0.25) {
        applyNewsEvent();
    } else {
        applyRandomUpdate(midquote, has_midquote);
    }
    return true;
}

void AnalystSystem::applyRandomUpdate(double midquote, bool has_midquote) {
    // Random subset of 3-7 analysts update their opinion
    std::uniform_int_distribution<int> subset_dist(3, 7);
    int count = subset_dist(rng());

    std::array<int, NUM_ANALYSTS> indices;
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng());

    // Possible magnitudes
    static const int magnitudes[] = {1, 2, 3};
    std::uniform_int_distribution<int> mag_dist(0, 2);

    for (int i = 0; i < count; ++i) {
        int idx = indices[i];
        double opinion = opinions_[idx];

        // Determine direction with 2:1 bias toward midquote when available
        int sign = 0;
        if (has_midquote) {
            // Weight: 2 toward midquote, 1 away from midquote
            // If opinion < midquote: P(+1) = 2/3, P(-1) = 1/3
            // If opinion > midquote: P(+1) = 1/3, P(-1) = 2/3
            // If opinion == midquote: equal (50/50)
            std::uniform_int_distribution<int> dir_dist(0, 2); // 0,1 = toward; 2 = away
            int roll = dir_dist(rng());
            bool toward = (roll < 2);
            if (opinion < midquote)
                sign = toward ? +1 : -1;
            else if (opinion > midquote)
                sign = toward ? -1 : +1;
            else
                sign = (roll < 1) ? +1 : -1; // 50/50 when equal
        } else {
            // No midquote: equal probability
            std::uniform_int_distribution<int> dir_dist(0, 1);
            sign = dir_dist(rng()) ? +1 : -1;
        }

        int delta = sign * magnitudes[mag_dist(rng())];
        double proposed = opinion + delta;

        // Constraint 1: min opinion >= 1.0
        if (proposed < 1.0) continue;

        // Constraint 2: max - min <= 30
        double cur_min = *std::min_element(opinions_.begin(), opinions_.end());
        double cur_max = *std::max_element(opinions_.begin(), opinions_.end());
        double new_min = std::min(cur_min, proposed);
        double new_max = std::max(cur_max, proposed);
        if (new_max - new_min > 30.0) continue;

        opinions_[idx] = proposed;
    }
}

void AnalystSystem::applyNewsEvent() {
    // Randomly bullish or bearish news
    std::uniform_int_distribution<int> coin(0, 1);
    int direction = coin(rng()) ? +1 : -1;

    // Each analyst moves 10-20 dollars in the same direction
    std::uniform_int_distribution<int> mag_dist(10, 20);

    for (int i = 0; i < NUM_ANALYSTS; ++i) {
        double prev = opinions_[i];
        double raw_delta = direction * mag_dist(rng());

        // Cap: change cannot exceed 50% of previous opinion
        double max_change = 0.5 * prev;
        double capped_delta = (raw_delta > 0)
            ? std::min(raw_delta, max_change)
            : std::max(raw_delta, -max_change);

        double proposed = prev + capped_delta;

        // Hard floor: opinion >= 1.0
        if (proposed < 1.0) proposed = 1.0;

        opinions_[i] = proposed;
    }
}
