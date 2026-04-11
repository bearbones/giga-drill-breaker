#pragma once
#include <iostream>

namespace MathLib {
    struct Vector {};

    // The int overload ships in Core.hpp — always visible.
    inline void scale(Vector, int) {
        std::cout << "Scaled by int (Core)\n";
    }
}
