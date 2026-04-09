#include "internal.hpp"
#include <iostream>

namespace internal {

double accumulate_helper(const double* data, int n) {
    double total = 0.0;
    for (int i = 0; i < n; ++i) {
        total += data[i];
    }
    return total;
}

double old_normalize(double val, double range) {
    if (range == 0.0) return 0.0;
    return val / range;
}

void log_value(const std::string& label, double val) {
    std::cout << "[DEBUG] " << label << " = " << val << "\n";
}

} // namespace internal
