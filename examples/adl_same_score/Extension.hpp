#pragma once
#include "Core.hpp"

namespace MathLib {
    // A float overload added by Extension.hpp. Neither int nor float is a
    // strictly better match for a `long` argument — each reaches the
    // parameter type via a standard conversion of roughly equivalent rank.
    // Which one wins depends silently on which header was included first.
    inline void scale(Vector, float) {
        std::cout << "Scaled by float (Extension)\n";
    }
}
