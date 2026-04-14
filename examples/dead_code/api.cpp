// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

#include "api.hpp"
#include "internal.hpp"

namespace mathutil {

double sum(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double raw = internal::accumulate_helper(v.data(),
                                             static_cast<int>(v.size()));
    return normalize(raw, 0.0, 1e12);
}

double mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return sum(v) / static_cast<double>(v.size());
}

double normalize(double val, double min, double max) {
    if (max <= min) return val;
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

double obscure_transform(double x, int n) {
    double result = x;
    for (int i = 0; i < n; ++i) {
        result = result * 0.99 + 0.01;
    }
    return result;
}

} // namespace mathutil
