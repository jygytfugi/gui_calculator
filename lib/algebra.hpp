#ifndef ALGEBRA_HPP
#define ALGEBRA_HPP

#include <string>
#include <complex>
#include <stdexcept>

class AlgebraCalculator {
public:
    AlgebraCalculator();

    // 1. Exponent / Power: x^y
    double power(double base, double exponent);

    // 2. N-th Root: \sqrt[n]{x} (default n=2 for square root)
    double root(double x, double n = 2.0);

    // 3. Absolute Value: |x|
    double absolute(double x);

    // 4. Logarithm with base: log_base(x)
    double logBase(double x, double base = 10.0);

    // 5. Factorial: n!
    long long factorial(long long n);

    // 6. Percentage: x%
    double percentage(double value);

    // 7. Fraction Division: a / b
    double fraction(double numerator, double denominator);

    // 8. Inequalities & Equations
    bool isLessThan(double a, double b);
    bool isLessThanOrEqual(double a, double b);
    bool isGreaterThan(double a, double b);
    bool isGreaterThanOrEqual(double a, double b);
    bool isEqual(double a, double b, double epsilon = 1e-9);

    // 9. Complex Number operations (for 'i')
    std::complex<double> complexAdd(const std::complex<double>& a, const std::complex<double>& b);
    std::complex<double> complexMultiply(const std::complex<double>& a, const std::complex<double>& b);
};

#endif // ALGEBRA_HPP