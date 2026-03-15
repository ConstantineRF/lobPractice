#pragma once
#include <cmath>
#include <string>

// All prices stored as integer cents to avoid floating-point comparison bugs
// e.g., 5064 = $50.64
using Price    = int;
using Qty      = int;
using OrderID  = unsigned long long;
using TraderID = int;   // 1-5 = market makers, 6-10 = investors
using SimTime  = double; // seconds since simulation start (wall clock)

enum class Side { BUY, SELL };

constexpr Price TICK            = 1;     // 1 cent minimum price increment
constexpr Price INITIAL_MID     = 5000;  // $50.00 in cents
constexpr int   NUM_TRADERS     = 10;
constexpr int   NUM_ANALYSTS    = 10;
constexpr int   NUM_MARKET_MAKERS = 5;
constexpr int   NUM_INVESTORS   = 5;

inline double centsToDouble(Price p) { return p / 100.0; }
inline Price  doubleToCents(double d) { return static_cast<Price>(std::round(d * 100.0)); }

inline std::string sideStr(Side s) { return s == Side::BUY ? "BUY" : "SELL"; }
inline std::string filledStr(Side s) { return s == Side::BUY ? "BOUGHT" : "SOLD"; }
