#pragma once
#include "Container.hpp"

// The compiler must deduce 'auto' right here.
inline auto make_container() {
    return Container("Hello"); // "Hello" is a const char[6], decaying to const char*
}
