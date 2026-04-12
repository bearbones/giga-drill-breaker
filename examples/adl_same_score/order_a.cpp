// Order A: this TU exists to teach Phase 1 of the mugann pipeline about the
// float overload in Extension.hpp. Calling scale with a float literal picks
// scale(Vector, float) unambiguously, so the TU compiles cleanly.
//
// Note: if we tried to include both Extension.hpp and Logic.hpp here, the
// `long amount` call in Logic.hpp would become an *actual* ambiguous call
// and the TU would fail to compile — which is the point of the whole
// fragility case. mugann flags that same tie from order_b.cpp's perspective
// using the cross-TU index.
#include "Extension.hpp"

int main() {
    MathLib::Vector v;
    MathLib::scale(v, 1.0f);
}
