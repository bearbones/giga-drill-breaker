// Order B: Logic included before Extension — silent fallback.
#include "Logic.hpp"
#include "Extension.hpp"

int main() {
    do_scaling();
    // Output: Scaled by int (Fallback)
}
