// Order A: Guide included before Factory — uses custom guide.
#include "Guide.hpp"
#include "Factory.hpp"

int main() {
    auto c = make_container();
    // c is Container<std::string>
}
