#pragma once
#include <iostream>

namespace MathLib {
    struct Vector {};

    // Fallback: takes an int
    void scale(Vector, int) {
        std::cout << "Scaled by int (Fallback)\n";
    }
}
