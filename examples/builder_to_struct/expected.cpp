// Example: expected output after builder-to-struct + lambda-to-member transform.

#include <string>

struct Config {
  std::string name;
  int port = 0;
  bool verbose = false;

  void apply();
};

void Config::apply() {
  // ... use *this ...
}

void example_single_chain() {
  Config c;
  c.name = "server";
  c.port = 8080;
  c.verbose = true;
  c.apply();
}

void example_separate_calls() {
  Config c;
  c.name = "worker";
  c.port = 9090;
  c.verbose = false;
  c.apply();
}
