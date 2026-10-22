#include "../lib/matrix.hpp"
#include "../lib/Calculator_calculus.hpp"
#include <iostream>
#include <string>
#include <cmath>

Matrix::Matrix(int r, int c) : rows(r), cols(c), data(r, std::vector<double>(c, 0.0)) {}

Matrix::Matrix(const std::vector<std::vector<double>>& input) {
    rows = input.size();
    cols = input[0].size();
    data = input;
}

void Matrix::setElement(int r, int c, double val) { data[r][c] = val; }
double Matrix::getElement(int r, int c) const { return data[r][c]; }

void Matrix::print() const {
    for (int i = 0; i < rows; ++i) {
        std::cout << "[ ";
        for (int j = 0; j < cols; ++j) {
            std::cout << data[i][j] << " ";
        }
        std::cout << "]\n";
    }
}

Matrix Matrix::add(const Matrix& other) const {
    if (rows != other.rows || cols != other.cols) 
        throw std::invalid_argument("Matrix dimensions must match for addition.");
    Matrix res(rows, cols);
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            res.data[i][j] = data[i][j] + other.data[i][j];
    return res;
}

Matrix Matrix::subtract(const Matrix& other) const {
    if (rows != other.rows || cols != other.cols) 
        throw std::invalid_argument("Matrix dimensions must match for subtraction.");
    Matrix res(rows, cols);
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            res.data[i][j] = data[i][j] - other.data[i][j];
    return res;
}

Matrix Matrix::multiply(const Matrix& other) const {
    if (cols != other.rows) 
        throw std::invalid_argument("Matrix A cols must equal Matrix B rows.");
    Matrix res(rows, other.cols);
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < other.cols; ++j) {
            for (int k = 0; k < cols; ++k) {
                res.data[i][j] += data[i][k] * other.data[k][j];
            }
        }
    }
    return res;
}

Matrix Matrix::transpose() const {
    Matrix res(cols, rows);
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            res.data[j][i] = data[i][j];
    return res;
}

// Determinant สำหรับ n x n โดยใช้ Gaussian Elimination (Upper Triangular Matrix)
double Matrix::determinant() const {
    if (rows != cols) throw std::invalid_argument("Determinant requires a square matrix (n x n).");
    
    int n = rows;
    std::vector<std::vector<double>> mat = data;
    double det = 1.0;
    int swapCount = 0;

    for (int i = 0; i < n; ++i) {
        // หา Pivot Element ที่มีค่าสัมบูรณ์สูงสุดเพื่อความเสถียรเชิงตัวเลข
        int maxRow = i;
        for (int k = i + 1; k < n; ++k) {
            if (std::abs(mat[k][i]) > std::abs(mat[maxRow][i])) {
                maxRow = k;
            }
        }

        if (std::abs(mat[maxRow][i]) < 1e-9) return 0.0; // Det เป็น 0 ถ้า Pivot เป็น 0

        // สลับแถวถ้าจำเป็น
        if (i != maxRow) {
            std::swap(mat[i], mat[maxRow]);
            swapCount++;
        }

        // กำจัดค่าใต้ Pivot ให้เป็น 0
        for (int k = i + 1; k < n; ++k) {
            double factor = mat[k][i] / mat[i][i];
            for (int j = i; j < n; ++j) {
                mat[k][j] -= factor * mat[i][j];
            }
        }

        det *= mat[i][i];
    }

    // สลับแถว 1 ครั้งจะเปลี่ยนเครื่องหมายของ Det
    if (swapCount % 2 != 0) det = -det;

    return det;
}

// Inverse สำหรับ n x n โดยใช้ Gauss-Jordan Elimination กับ Augmented Matrix [A | I]
Matrix Matrix::inverse() const {
    if (rows != cols) throw std::invalid_argument("Inverse requires a square matrix (n x n).");

    int n = rows;
    std::vector<std::vector<double>> aug(n, std::vector<double>(2 * n, 0.0));

    // สร้าง Augmented Matrix [A | I]
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) aug[i][j] = data[i][j];
        aug[i][i + n] = 1.0;
    }

    for (int i = 0; i < n; ++i) {
        // หา Pivot
        int maxRow = i;
        for (int k = i + 1; k < n; ++k) {
            if (std::abs(aug[k][i]) > std::abs(aug[maxRow][i])) {
                maxRow = k;
            }
        }

        if (std::abs(aug[maxRow][i]) < 1e-9) {
            throw std::runtime_error("Matrix is singular (Det = 0), Inverse does not exist.");
        }

        if (i != maxRow) std::swap(aug[i], aug[maxRow]);

        // ทำ Pivot Element ให้เป็น 1
        double pivot = aug[i][i];
        for (int j = 0; j < 2 * n; ++j) aug[i][j] /= pivot;

        // กำจัดหลักนั้นทั้งบนและล่าง Pivot ให้เป็น 0
        for (int k = 0; k < n; ++k) {
            if (k != i) {
                double factor = aug[k][i];
                for (int j = 0; j < 2 * n; ++j) {
                    aug[k][j] -= factor * aug[i][j];
                }
            }
        }
    }

    // ดึงเฉพาะเมทริกซ์ฝั่งขวาที่เป็น Inverse ออกมา
    Matrix inv(n, n);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            inv.data[i][j] = aug[i][j + n];
        }
    }

    return inv;
}

// แก้ระบบสมการเชิงเส้น A * X = B ด้วย Gauss-Jordan Elimination พร้อม Partial Pivoting
// รองรับระบบสมการที่มีตัวแปรตั้งแต่ 2 ตัวขึ้นไป (n สมการ, n ตัวแปร)
Matrix Matrix::solveLinear(const Matrix& A, const Matrix& B) {
    if (A.rows != A.cols)
        throw std::invalid_argument("Coefficient matrix A must be square (n x n) to solve a linear system.");
    if (B.rows != A.rows)
        throw std::invalid_argument("Matrix B must have the same number of rows as A.");

    int n = A.rows;
    int m = B.cols;

    // สร้าง Augmented Matrix [A | B]
    std::vector<std::vector<double>> aug(n, std::vector<double>(n + m, 0.0));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) aug[i][j] = A.data[i][j];
        for (int j = 0; j < m; ++j) aug[i][n + j] = B.data[i][j];
    }

    for (int i = 0; i < n; ++i) {
        // หา Pivot ที่มีค่าสัมบูรณ์สูงสุดเพื่อความเสถียรเชิงตัวเลข
        int maxRow = i;
        for (int k = i + 1; k < n; ++k) {
            if (std::abs(aug[k][i]) > std::abs(aug[maxRow][i])) maxRow = k;
        }

        if (std::abs(aug[maxRow][i]) < 1e-9) {
            throw std::runtime_error("System has no unique solution (coefficient matrix is singular).");
        }

        if (i != maxRow) std::swap(aug[i], aug[maxRow]);

        // ทำ Pivot Element ให้เป็น 1
        double pivot = aug[i][i];
        for (int j = 0; j < n + m; ++j) aug[i][j] /= pivot;

        // กำจัดหลักนั้นทั้งบนและล่าง Pivot ให้เป็น 0 (Gauss-Jordan)
        for (int k = 0; k < n; ++k) {
            if (k != i) {
                double factor = aug[k][i];
                for (int j = 0; j < n + m; ++j) {
                    aug[k][j] -= factor * aug[i][j];
                }
            }
        }
    }

    Matrix X(n, m);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j)
            X.data[i][j] = aug[i][n + j];

    return X;
}

std::vector<double> Matrix::solveLinear(const Matrix& A, const std::vector<double>& b) {
    Matrix B(static_cast<int>(b.size()), 1);
    for (size_t i = 0; i < b.size(); ++i) B.setElement(static_cast<int>(i), 0, b[i]);

    Matrix X = solveLinear(A, B);

    std::vector<double> result(X.getRows());
    for (int i = 0; i < X.getRows(); ++i) result[i] = X.getElement(i, 0);
    return result;
}

// แก้ระบบสมการเชิงเส้นแบบทั่วไป ไม่บังคับให้ A เป็นเมทริกซ์จัตุรัส
// รองรับทั้งกรณีจำนวนสมการเท่ากับ/มากกว่า/น้อยกว่าจำนวนตัวแปร
Matrix Matrix::solveLinearGeneral(const Matrix& A, const Matrix& B) {
    if (B.rows != A.rows)
        throw std::invalid_argument("Matrix B must have the same number of rows as A.");

    if (A.rows == A.cols) {
        // กรณีพอดี: จำนวนสมการ = จำนวนตัวแปร -> แก้ตรง ๆ
        return solveLinear(A, B);
    } else if (A.rows > A.cols) {
        // กรณีสมการมากกว่าตัวแปร (Overdetermined) -> Least Squares ผ่าน Normal Equation
        // (AᵀA) X = AᵀB
        Matrix At = A.transpose();
        Matrix AtA = At.multiply(A);
        Matrix AtB = At.multiply(B);
        try {
            return solveLinear(AtA, AtB);
        } catch (const std::runtime_error&) {
            throw std::runtime_error("Overdetermined system has no unique least-squares solution (rank deficient / AtA is singular).");
        }
    } else {
        // กรณีตัวแปรมากกว่าสมการ (Underdetermined) -> Minimum-Norm Solution
        // แก้ (AAᵀ) Y = B ก่อน แล้วค่อยได้ X = Aᵀ Y
        Matrix At = A.transpose();
        Matrix AAt = A.multiply(At);
        Matrix Y;
        try {
            Y = solveLinear(AAt, B);
        } catch (const std::runtime_error&) {
            throw std::runtime_error("Underdetermined system has no minimum-norm solution (rank deficient / AAt is singular).");
        }
        return At.multiply(Y);
    }
}

Matrix Matrix::multiplyScalar(double scalar) const {
    Matrix res(rows, cols);
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            res.data[i][j] = data[i][j] * scalar;
        }
    }
    return res;
}

// ฟังก์ชันสำหรับรับค่า Matrix โดยรองรับ Expression เช่น sin(30), log(10), 2^3, ฯลฯ
Matrix inputMatrixWithExpr(Calculator& calc, int rows, int cols, const std::string& name) {
    Matrix mat(rows, cols);
    std::cout << "Enter elements for " << name << " (" << rows << "x" << cols << "):\n";
    
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            std::string expr;
            std::cout << name << "[" << i + 1 << "][" << j + 1 << "]: ";
            std::cin >> expr;
            
            try {
                // คำนวณค่าจาก Expression ด้วย Calculator
                double val = calc.evaluate(expr);
                mat.setElement(i, j, val);
            } catch (const std::exception& e) {
                std::cout << "Invalid expression, default to 0. Error: " << e.what() << "\n";
                mat.setElement(i, j, 0.0);
            }
        }
    }
    return mat;
}

// ฟังก์ชันหา อนุพันธ์ (Derivative) ของสมาชิกทุกตัวใน Matrix (รับเป็นสตริงเมทริกซ์)
Matrix matrixDerivative(Calculator& calc, int rows, int cols, double x0) {
    Matrix result(rows, cols);
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            std::string expr;
            std::cout << "Expr[" << i + 1 << "][" << j + 1 << "]: ";
            std::cin >> expr;
            // คำนวณ d/dx ของฟังก์ชัน ณ จุด x0
            double dVal = calc.derivative(expr, x0);
            result.setElement(i, j, dVal);
        }
    }
    return result;
}

// ฟังก์ชันหา ปริพนธ์ (Integral) ของสมาชิกทุกตัวใน Matrix
Matrix matrixIntegral(Calculator& calc, int rows, int cols, double a, double b) {
    Matrix result(rows, cols);
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            std::string expr;
            std::cout << "Expr[" << i + 1 << "][" << j + 1 << "]: ";
            std::cin >> expr;
            // คำนวณ Integral จาก a ถึง b
            double iVal = calc.integral(expr, a, b);
            result.setElement(i, j, iVal);
        }
    }
    return result;
}