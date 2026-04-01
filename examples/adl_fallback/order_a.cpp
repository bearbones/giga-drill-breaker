// Order A: Extension included before Logic — correct resolution.
#include "Extension.hpp"
#include "Logic.hpp"

int main() {
    do_scaling();
    // Output: Scaled by double (Precise)
}
