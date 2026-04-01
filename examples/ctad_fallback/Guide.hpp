#pragma once
#include "Container.hpp"
#include <string>

// Explicitly tell the compiler: if you see a const char*, deduce std::string
Container(const char*) -> Container<std::string>;
