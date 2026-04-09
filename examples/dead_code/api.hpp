#pragma once
#include <vector>

// --- Public library API ---
//
// Demonstrates the difference between internally-used functions,
// public API functions (marked alive by external config), and
// truly dead functions.
//
//   sum                 — alive, called from main
//   mean                — dead internally, but could be marked public_api
//   normalize           — alive, called transitively by sum()
//   obscure_transform   — dead, never called, not public API

namespace mathutil {

// Called directly from main — alive.
double sum(const std::vector<double>& v);

// Never called from this project. An external config could mark it
// as public API (making it alive), but by default it's dead.
double mean(const std::vector<double>& v);

// Called by sum() internally — alive transitively.
double normalize(double val, double min, double max);

// Never called, not part of public API — dead.
double obscure_transform(double x, int n);

} // namespace mathutil
