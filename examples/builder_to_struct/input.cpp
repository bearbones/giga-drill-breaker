// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

// Example: builder pattern class with nonmember lambda-based configuration.
//
// The transforms demonstrated here convert:
// 1. Builder-pattern class -> struct with field assignments
// 2. Nonmember function taking a lambda -> member function call

#include <string>

class Config {
public:
  Config& setName(const std::string& n) { name_ = n; return *this; }
  Config& setPort(int p) { port_ = p; return *this; }
  Config& setVerbose(bool v) { verbose_ = v; return *this; }

  const std::string& getName() const { return name_; }
  int getPort() const { return port_; }
  bool getVerbose() const { return verbose_; }

private:
  std::string name_;
  int port_ = 0;
  bool verbose_ = false;
};

// Nonmember function that takes a lambda to configure via builder pattern.
template <typename F>
void applyConfig(F&& f) {
  Config c;
  f(c);
  // ... use c ...
}

void example_single_chain() {
  applyConfig([](Config& c) {
    c.setName("server").setPort(8080).setVerbose(true);
  });
}

void example_separate_calls() {
  applyConfig([](Config& c) {
    c.setName("worker");
    c.setPort(9090);
    c.setVerbose(false);
  });
}
