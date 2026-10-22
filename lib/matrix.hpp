#ifndef MATRIX_HPP
#define MATRIX_HPP

#include <vector>
#include <iostream>
#include <stdexcept>

class Matrix {
private:
    int rows;
    int cols;
    std::vector<std::vector<double>> data;

public:
    Matrix(int r = 1, int c = 1);
    Matrix(const std::vector<std::vector<double>>& input);

    void setElement(int r, int c, double val);
    double getElement(int r, int c) const;
    int getRows() const { return rows; }
    int getCols() const { return cols; }
    void print() const;

    Matrix add(const Matrix& other) const;
    Matrix subtract(const Matrix& other) const;
    Matrix multiply(const Matrix& other) const;
    Matrix multiplyScalar(double scalar) const;
    Matrix transpose() const;
    
    double determinant() const;
    Matrix inverse() const;

    // แก้ระบบสมการเชิงเส้น A * X = B ด้วย Gauss-Jordan Elimination (รองรับตัวแปรตั้งแต่ 2 ตัวขึ้นไป)
    // A ต้องเป็นเมทริกซ์จัตุรัส n x n, B เป็น n x m (m = จำนวนชุดคำตอบที่ต้องการแก้พร้อมกัน)
    // คืนค่า X ขนาด n x m; throw std::runtime_error ถ้า A เอกฐาน (ไม่มีคำตอบเดียว)
    static Matrix solveLinear(const Matrix& A, const Matrix& B);

    // Overload สะดวกใช้: รับ/คืนเป็น std::vector<double> ตรง ๆ สำหรับกรณี B มีคอลัมน์เดียว
    static std::vector<double> solveLinear(const Matrix& A, const std::vector<double>& b);

    // แก้ระบบสมการเชิงเส้น A * X = B แบบทั่วไป โดย "ไม่บังคับ" ว่าจำนวนสมการต้องเท่ากับจำนวนตัวแปร:
    //  - A.rows == A.cols (พอดี/Exact)         -> แก้ตรง ๆ ด้วย solveLinear (คำตอบเดียว ถ้า A ไม่เอกฐาน)
    //  - A.rows >  A.cols (สมการเกิน/Overdetermined) -> คำตอบที่ดีที่สุดแบบ Least Squares: (AᵀA) X = AᵀB
    //  - A.rows <  A.cols (ตัวแปรเกิน/Underdetermined) -> คำตอบแบบ Minimum-Norm: X = Aᵀ * ((AAᵀ)⁻¹ B)
    // throw std::runtime_error ถ้าเมทริกซ์ที่เกี่ยวข้อง (A, AᵀA หรือ AAᵀ) เอกฐาน / Rank ไม่พอ
    static Matrix solveLinearGeneral(const Matrix& A, const Matrix& B);
};

#endif