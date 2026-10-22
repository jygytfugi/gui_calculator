#ifndef ALPHA_SOLVER_EQ_HPP
#define ALPHA_SOLVER_EQ_HPP

#include <vector>
#include <complex>
#include <string>
#include <stdexcept>
#include <map>

class Calculator; // Forward declaration
class Matrix;     // Forward declaration

class AlphaSolverEq {
public:
    AlphaSolverEq();

    // === [เพิ่มใหม่] รองรับคำว่า "ans" ในสมการ/นิพจน์ เพื่ออ้างอิงคำตอบล่าสุด เช่น "x + 2 = ans" ===
    // ตั้งค่า "ans" ไว้ล่วงหน้า (ปกติคือผลลัพธ์จากการคำนวณ/แก้สมการครั้งก่อนหน้า) แล้วทุกฟังก์ชัน solve*
    // ในคลาสนี้ (ที่รับ expression เป็น string) จะแทนคำว่า "ans" ที่ปรากฏในสมการด้วยค่านี้โดยอัตโนมัติ
    // ก่อนประมวลผลเสมอ (เป็น whole-word เท่านั้น เช่น "ans" แทนได้ แต่ "scans" จะไม่ถูกแตะต้อง)
    void setAns(double value);
    double getAns() const;

    // 1. แก้สมการพหุนาม
    std::vector<std::complex<double>> solvePolynomial(const std::vector<double>& coeffs);

    // 2. แยกตัวประกอบพหุนาม
    std::string factorizePolynomial(const std::vector<double>& coeffs, char var = 'x');

    // 3. แก้สมการทั่วไป f(x) = 0
    double solveGeneralEquation(Calculator& calc, const std::string& expr, double initialGuess = 1.0, int maxIter = 1000, double tol = 1e-7, char var = '\0');

    // === [เพิ่มใหม่] แก้สมการเชิงแคลคูลัส (Calculus Equations) ===

    // 3.1 แก้สมการจุดวิกฤต / สมการอนุพันธ์ f'(x) = targetValue (เช่น f'(x) = 0)
    double solveDerivativeEq(Calculator& calc, const std::string& expr, double targetValue = 0.0, double initialGuess = 1.0, int maxIter = 1000, double tol = 1e-7, char var = '\0');

    // 3.2 แก้สมการอินทิกรัลจำกัดเขตหาขอบเขต b ที่ทำให้ Integral[a to b] f(x) dx = targetValue
    double solveIntegralBound(Calculator& calc, const std::string& expr, double lowerBoundA, double targetValue, double initialGuess = 1.0, int maxIter = 1000, double tol = 1e-7, char var = '\0');

    // เพิ่มใน public section ของ AlphaSolverEq
    double solveIndefiniteIntegralEq(Calculator& calc, const std::string& expr, double targetValue, double x_ref = 0.0, double initialGuess = 1.0, int maxIter = 1000, double tol = 1e-7, char var = '\0');

    // 3.3 แก้สมการลิมิต lim(x->a) f(x) = targetValue (แก้หาค่า a)
    double solveLimitEq(Calculator& calc, const std::string& expr, double targetValue, double initialGuess = 1.0, int maxIter = 1000, double tol = 1e-7, char var = '\0');

// [เพิ่มใหม่] แก้สมการรูปแบบ "lhs = rhs" โดยย้ายข้างเป็น (lhs) - (rhs) = 0
    double solveTwoSideEquation(Calculator& calc, const std::string& fullEq, double initialGuess = 1.0, int maxIter = 1000, double tol = 1e-7, char var = '\0');

    // === [เพิ่มใหม่] แก้ระบบสมการที่มีตัวแปรตั้งแต่ 2 ตัวขึ้นไป (System of Equations) ===

    // 4.1 แก้ระบบสมการเชิงเส้น (Linear System) เช่น {"2x + 3y = 8", "x - y = 1"}
    // จำนวนสมการ (equations.size()) และจำนวนตัวแปรที่พบในสมการทั้งหมด "ไม่จำเป็นต้องเท่ากัน":
    //  - พอดี (n สมการ = n ตัวแปร)        -> คำตอบตรงหนึ่งเดียว (ถ้าระบบไม่เอกฐาน)
    //  - สมการมากกว่าตัวแปร (Overdetermined) -> คำตอบที่ดีที่สุดแบบ Least Squares
    //  - ตัวแปรมากกว่าสมการ (Underdetermined) -> คำตอบแบบ Minimum-Norm (หนึ่งในคำตอบที่เป็นไปได้)
    // แต่ละสมการต้องเป็นเชิงเส้นจริง ๆ (ไม่มี x^2, sin(x), x*y ฯลฯ)
    // ฟังก์ชันนี้ตรวจสอบความเป็นเชิงเส้นของทุกสมการก่อนแก้เสมอ ถ้าพบพจน์ไม่เชิงเส้น (เช่น x^2, y^3, x*y)
    // จะ throw std::invalid_argument ทันที พร้อมบอกให้ไปใช้ solveNonlinearSystem แทน (จะไม่คืนคำตอบที่ผิดแบบเงียบ ๆ)
    // ใช้ Matrix::solveLinearGeneral ภายใน คืนค่าเป็น map ของ ชื่อตัวแปร -> คำตอบ
    std::map<char, double> solveLinearSystem(Calculator& calc, const std::vector<std::string>& equations);

    // 4.2 แก้ระบบสมการทั่วไป/ไม่เชิงเส้น (Nonlinear System) ด้วย Newton-Raphson / Gauss-Newton
    // เช่น {"x^2 + y^2 = 25", "x - y = 1"}
    // จำนวนสมการและจำนวนตัวแปรไม่จำเป็นต้องเท่ากันเช่นกัน (พอดี/เกิน/ขาด ใช้หลักการเดียวกับข้อ 4.1 ในแต่ละรอบวนซ้ำ)
    // initialGuess: ค่าเริ่มต้นของแต่ละตัวแปร (ตัวแปรที่ไม่ได้ระบุจะเริ่มที่ 1.0 โดยอัตโนมัติ)
    // ใช้ Jacobian เชิงตัวเลข (numerical differentiation) แล้วแก้ระบบเชิงเส้นย่อยด้วย Matrix::solveLinearGeneral ในแต่ละรอบ
    std::map<char, double> solveNonlinearSystem(Calculator& calc, const std::vector<std::string>& equations,
                                                 const std::map<char, double>& initialGuess = {},
                                                 int maxIter = 100, double tol = 1e-7);

    // ตรวจหาชื่อตัวแปรจาก expression อัตโนมัติ (รองรับ a-z, A-Z)
    // - ถ้ามี 'x' อยู่ใน expression จะเลือก 'x' ก่อน
    // - ถ้าไม่มี 'x' และมีตัวแปรเดียว จะเลือกตัวนั้น
    // - ถ้าไม่พบตัวแปรเลย จะคืน 'x'; ถ้าพบหลายตัวและไม่มี 'x' จะ throw (ให้ระบุ var เอง)
    static char detectVariable(const std::string& expr);

    // คืนรายชื่อตัวแปรทั้งหมดที่พบใน expression (เรียงตามตัวอักษร, ไม่รวม 'e' ที่เป็นค่าคงที่)
    static std::vector<char> findVariables(const std::string& expr);

    // แทนค่าตัวเลขลงในตัวแปรที่ระบุ (พารามิเตอร์) เช่น a=3 : "a^2+2*x" -> "(3)^2+2*x"
    static std::string substituteVariables(const std::string& expr, const std::map<char, double>& values);

    // แปลงตัวแปร var (a-z, A-Z) ใน expression ให้เป็น 'x' เพื่อส่งให้ Calculator ที่ผูกกับ 'x'
    // var = '\0' หมายถึงตรวจหาอัตโนมัติ; throw ถ้าตัวแปรไม่ใช่ตัวอักษร หรือชนกับ 'x' ที่เป็นตัวแปรอีกตัว
    static std::string normalizeVariable(const std::string& expr, char var = '\0');

private:
    double lastAnswer = 0.0; // ค่า "ans" ปัจจุบัน ตั้งผ่าน setAns()

    std::vector<std::complex<double>> solveQuadratic(double a, double b, double c);
    std::vector<std::complex<double>> solveCubic(double a, double b, double c, double d);
    std::vector<std::complex<double>> solveQuartic(double a, double b, double c, double d, double e);
    std::vector<std::complex<double>> solveHighDegreePoly(const std::vector<double>& coeffs);
};

#endif // ALPHA_SOLVER_EQ_HPP