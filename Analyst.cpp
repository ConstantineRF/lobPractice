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

bool AnalystSystem::update(SimTime now) {
    if (now < next_update_) return false;
    next_update_ = now + 15.0;
    applyRandomUpdate();
    return true;
}

void AnalystSystem::applyRandomUpdate() {
    // Random subset of 3-7 analysts update their opinion
    std::uniform_int_distribution<int> subset_dist(3, 7);
    int count = subset_dist(rng());

    // Pick indices to update
    std::array<int, NUM_ANALYSTS> indices;
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng());

    // Possible change magnitudes
    static const int magnitudes[] = {-3, -2, -1, 1, 2, 3};
    std::uniform_int_distribution<int> mag_dist(0, 5);

    for (int i = 0; i < count; ++i) {
        int idx = indices[i];
        int delta = magnitudes[mag_dist(rng())];
        double proposed = opinions_[idx] + delta;

        // Check constraints before applying
        // 1. min opinion >= 1.0
        if (proposed < 1.0) continue;

        // 2. max - min <= 30
        double cur_min = *std::min_element(opinions_.begin(), opinions_.end());
        double cur_max = *std::max_element(opinions_.begin(), opinions_.end());

        // Simulate what would happen if we apply this change
        double new_min = std::min(cur_min, proposed);
        double new_max = std::max(cur_max, proposed);
        // Also account for existing value being replaced
        // (simplified: just check with old values replaced by proposed)
        if (new_max - new_min > 30.0) continue;

        opinions_[idx] = proposed;
    }
}
