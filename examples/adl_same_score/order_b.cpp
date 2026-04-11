// Order B: Only Core.hpp is visible here, so the call silently resolves to
// scale(Vector, int). The same-score tie against scale(Vector, float) in
// Extension.hpp is only visible to mugann's cross-TU index.
//
// The call is intentionally written here in the main file (rather than in
// an inline helper inside Logic.hpp) so that mugann's VisitCallExpr
// analyses it — the analyzer skips call sites that aren't in the main
// file.
#include "Core.hpp"

int main() {
    MathLib::Vector v;
    long amount = 3;
    scale(v, amount);
}
