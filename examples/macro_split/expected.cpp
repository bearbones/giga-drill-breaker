// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

// Example: expected output after macro boolean-split transform.

#define ASSERT(cond) do { if (!(cond)) __builtin_abort(); } while (0)
#define ASSERT_ALL(...) /* placeholder: checks each argument individually */

void test_two_operands(int a, int b) {
  // Two operands: split into individual ASSERTs.
  ASSERT(a > 0);
  ASSERT(b > 0);
}

void test_three_operands(int a, int b, int c) {
  // Three operands: convert to ASSERT_ALL.
  ASSERT_ALL(a > 0, b > 0, c > 0);
}

void test_four_operands(int a, int b, int c, int d) {
  // Four operands: convert to ASSERT_ALL.
  ASSERT_ALL(a > 0, b > 0, c > 0, d > 0);
}

void test_no_change(int a, int b) {
  // OR operator: leave alone.
  ASSERT(a > 0 || b > 0);

  // Single condition: leave alone.
  ASSERT(a > 0);
}
