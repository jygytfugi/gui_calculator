#include "../lib/algebra.hpp"
#include <cmath>

AlgebraCalculator::AlgebraCalculator() {}

double AlgebraCalculator::power(double base, double exponent) {
    return std::pow(base, exponent);
}

double AlgebraCalculator::root(double x, double n) {
    if (n == 0) throw std::invalid_argument("Root degree cannot be zero.");
    if (x < 0 && static_cast<int>(n) % 2 == 0) {
        throw std::domain_error("Even root of negative number is not a real number.");
    }
    return std::pow(x, 1.0 / n);
}

double AlgebraCalculator::absolute(double x) {
    return std::fabs(x);
}

double AlgebraCalculator::logBase(double x, double base) {
    if (x <= 0) throw std::domain_error("Logarithm argument must be positive.");
    if (base <= 0 || base == 1.0) throw std::domain_error("Logarithm base must be positive and not equal to 1.");
    return std::log(x) / std::log(base);
}

long long AlgebraCalculator::factorial(long long n) {
    if (n < 0) throw std::domain_error("Factorial is not defined for negative numbers.");
    long long result = 1;
    for (long long i = 2; i <= n; ++i) result *= i;
    return result;
}

double AlgebraCalculator::percentage(double value) {
    return value / 100.0;
}

double AlgebraCalculator::fraction(double numerator, double denominator) {
    if (denominator == 0.0) throw std::runtime_error("Division by zero.");
    return numerator / denominator;
}

bool AlgebraCalculator::isLessThan(double a, double b) { return a < b; }
bool AlgebraCalculator::isLessThanOrEqual(double a, double b) { return a <= b; }
bool AlgebraCalculator::isGreaterThan(double a, double b) { return a > b; }
bool AlgebraCalculator::isGreaterThanOrEqual(double a, double b) { return a >= b; }
bool AlgebraCalculator::isEqual(double a, double b, double epsilon) { return std::fabs(a - b) < epsilon; }

std::complex<double> AlgebraCalculator::complexAdd(const std::complex<double>& a, const std::complex<double>& b) {
    return a + b;
}

std::complex<double> AlgebraCalculator::complexMultiply(const std::complex<double>& a, const std::complex<double>& b) {
    return a * b;
}