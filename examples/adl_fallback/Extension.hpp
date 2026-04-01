#pragma once
#include "Core.hpp"

namespace MathLib {
    // Better match: takes a double
    void scale(Vector, double) {
        std::cout << "Scaled by double (Precise)\n";
    }
}
