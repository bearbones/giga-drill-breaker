// Example: macro invocations with boolean operators to be split/converted.
//
// The transforms demonstrated here convert ASSERT(a && b) style calls into
// either multiple ASSERT() calls or ASSERT_ALL(a, b, ...) depending on the
// number of operands.

#define ASSERT(cond) do { if (!(cond)) __builtin_abort(); } while (0)
#define ASSERT_ALL(...) /* placeholder: checks each argument individually */

void test_two_operands(int a, int b) {
  // Two operands: split into individual ASSERTs.
  ASSERT(a > 0 && b > 0);
}

void test_three_operands(int a, int b, int c) {
  // Three operands: convert to ASSERT_ALL.
  ASSERT(a > 0 && b > 0 && c > 0);
}

void test_four_operands(int a, int b, int c, int d) {
  // Four operands: convert to ASSERT_ALL.
  ASSERT(a > 0 && b > 0 && c > 0 && d > 0);
}

void test_no_change(int a, int b) {
  // OR operator: leave alone.
  ASSERT(a > 0 || b > 0);

  // Single condition: leave alone.
  ASSERT(a > 0);
}
