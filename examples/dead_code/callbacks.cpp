#include "callbacks.hpp"

// --- Transform functions ---

double double_it(double x) {
    return x * 2.0;
}

double triple_it(double x) {
    return x * 3.0;
}

double negate_it(double x) {
    return -x;
}

double square_it(double x) {
    return x * x;
}

// --- Higher-order functions ---

double apply_once(TransformFn fn, double x) {
    return fn(x);
}

double apply_chain(TransformFn first, TransformFn second, double x) {
    return second(first(x));
}

TransformFn select_transform(int choice) {
    switch (choice) {
    case 0:  return double_it;
    case 1:  return triple_it;
    default: return double_it;
    }
}
