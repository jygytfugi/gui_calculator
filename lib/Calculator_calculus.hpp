#ifndef CALCULATOR_CALCULUS_HPP
#define CALCULATOR_CALCULUS_HPP

#include <string>
#include <map>
#include <stdexcept>
#include <limits>
#include "algebra.hpp"
#include "Alpha_solver_eq.hpp"

class Calculator {
public:
    Calculator();

    double evaluate(const std::string &expr);
    void setVariable(char name, double value);
    
    // === ฟังก์ชัน Calculus ===
    // รองรับ a = std::numeric_limits<double>::infinity() หรือ -infinity()
    // side: '\0' (ค่าเริ่มต้น) = ลิมิตสองด้าน (two-sided limit, x -> a)
    //       '+'  = ลิมิตด้านขวาเท่านั้น (right-hand limit, x -> a+ : เข้าใกล้ a จากค่าที่ "มากกว่า" a)
    //       '-'  = ลิมิตด้านซ้ายเท่านั้น (left-hand limit,  x -> a- : เข้าใกล้ a จากค่าที่ "น้อยกว่า" a)
    // (side ไม่มีผลเมื่อ a เป็น infinity/-infinity เพราะทิศทางการเข้าใกล้ถูกกำหนดอยู่แล้วโดยธรรมชาติ)
    double limit(const std::string &expr, double a, double h = 1e-6, char side = '\0');
    double derivative(const std::string &expr, double x0, double h = 1e-6);

    // === [เพิ่มใหม่] อนุพันธ์แบบสัญลักษณ์ (Symbolic Derivative) ===
    // ต่างจาก derivative() ตรงที่ไม่แทนค่า x เป็นตัวเลข แต่คืนค่า f'(x) ออกมาเป็น "สูตร" (string)
    // เช่น symbolicDerivative("x^2 + sin(x)") -> "((2*x) + cos(x))"
    // var: ตัวแปรที่จะหาอนุพันธ์เทียบด้วย (ค่าเริ่มต้น 'x'); ตัวแปรตัวอักษรอื่นในสมการจะถูกมองเป็นค่าคงที่
    // รองรับ: + - * / ^ (power rule เต็มรูปแบบรวม u^v ทั่วไป), sin/cos/tan/asin/acos/atan, sqrt/root,
    //          ln/log/exp, abs, เปอร์เซ็นต์ (%); สูตรที่ได้สามารถส่งต่อให้ evaluate()/setVariable() ประเมินค่าได้ปกติ
    // throw std::runtime_error ถ้าเจอ fact(x) (แฟคทอเรียลไม่มีอนุพันธ์รูปปิดเชิงสัญลักษณ์) หรือฟังก์ชันที่ไม่รองรับ
    // ซึ่งตัวแปรขึ้นกับตัวแปรที่กำลังจะหาอนุพันธ์ (เช่น root(x, y) ที่ y ไม่ใช่ค่าคงที่)
    std::string symbolicDerivative(const std::string &expr, char var = 'x');
    
    // === [เพิ่มใหม่] ปริพันธ์ไม่จำกัดเขตแบบสัญลักษณ์ (Symbolic Indefinite Integral) ===
    // คู่กับ symbolicDerivative(): คืน F(x) ที่ F'(x) = f(x) ออกมาเป็น "สูตร" (string) ไม่แทนค่า x (ไม่รวม + C)
    // เช่น symbolicIntegral("x^2 + sin(x)") -> "(((x^3)/3) - cos(x))"   (สูตรส่งต่อให้ evaluate()/setVariable() ได้)
    // var: ตัวแปรที่ integrate เทียบด้วย (ค่าเริ่มต้น 'x'); ตัวอักษรอื่นในสมการถูกมองเป็นค่าคงที่/พารามิเตอร์
    // รองรับ: พหุนาม, กฎกำลัง, ฟังก์ชันของ ax+b (sin/cos/tan/exp/ln/log/sqrt/asin/acos/atan/abs), a^(kx+b),
    //          กำลังของ sin/cos/tan, sin*cos, พหุนาม*(exp|sin|cos|a^x|ln) (by parts), ฟังก์ชันตรรกยะ (partial fractions),
    //          1/sqrt(ax^2+bx+c) และการแทนค่า u-substitution เช่น x*e^(x^2), ln(x)/x, x/(x^2+1)
    // throw std::runtime_error ถ้าไม่พบรูปปิดแบบเบื้องต้น (เช่น e^(x^2), sin(x)/x) หรือยังไม่รองรับ -- ไม่คืนค่าผิดแบบเงียบ ๆ
    std::string symbolicIntegral(const std::string &expr, char var = 'x');

    // รองรับ a หรือ b เป็น infinity() หรือ -infinity()
    double integral(const std::string &expr, double a, double b, int n = 1000);
    double indefiniteIntegral(const std::string &expr, double x0, double x_ref = 0.0, int n = 1000);

    AlgebraCalculator& algebra();
    AlphaSolverEq& solver();

private:
    std::string src;
    size_t pos;
    std::map<char, double> variables;
    AlgebraCalculator alg;
    AlphaSolverEq eqSolver;

    double parseExpression();
    double parseTerm();
    double parsePower();
    double parseUnary();
    double parsePrimary();
    double parseNumber();
    std::string parseIdentifier();

    void skipWhitespace();
    char peek() const;
    char get();
    bool eof() const;
    void expect(char c);

    double callFunction(const std::string &name);
};

#endif // CALCULATOR_CALCULUS_HPP