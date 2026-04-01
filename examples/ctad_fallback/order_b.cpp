// Order B: Factory included before Guide — uses implicit guide.
#include "Factory.hpp"
#include "Guide.hpp"

int main() {
    auto c = make_container();
    // c is Container<const char*>
}
