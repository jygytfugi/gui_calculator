#include "../lib/math_graph.hpp"
#include "../lib/Calculator_calculus.hpp"
#include "../lib/Alpha_solver_eq.hpp"
#include "../lib/menu.hpp"
#include "../lib/function_solver.hpp"
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <cstdlib>

// ---------------------------------------------------------------------------
// ตัวช่วยอ่านค่าจากคีย์บอร์ดแบบกัน input พัง (ไม่ให้ลูป Interactive ค้าง)
// ---------------------------------------------------------------------------
namespace {
    bool readDouble(const std::string& prompt, double& out) {
        std::cout << prompt;
        while (!(std::cin >> out)) {
            if (std::cin.eof()) return false;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "\033[1;31m[Error] กรุณาใส่ตัวเลข > \033[0m";
        }
        return true;
    }

    bool readInt(const std::string& prompt, int& out) {
        double tmp = 0.0;
        if (!readDouble(prompt, tmp)) return false;
        out = static_cast<int>(tmp);
        return true;
    }
}

MathGraph::MathGraph(int width, int height) : width(width), height(height) {}

void MathGraph::setResolution(int newWidth, int newHeight) {
    if (newWidth > 10 && newHeight > 5) {
        width = newWidth;
        height = newHeight;
    } else {
        std::cout << "\033[1;31m[Warning] Resolution ต้องมากกว่า 10 x 5 — ไม่มีการเปลี่ยนแปลง\033[0m\n";
    }
}

int MathGraph::yToRow(double y, double yMin, double yMax) const {
    return height - 1 - static_cast<int>((y - yMin) / (yMax - yMin) * (height - 1));
}

int MathGraph::xToCol(double x, double xMin, double xMax) const {
    return static_cast<int>((x - xMin) / (xMax - xMin) * (width - 1));
}

void MathGraph::drawAxes(std::vector<std::string>& grid, double xMin, double xMax, double yMin, double yMax) const {
    int zeroRow = yToRow(0.0, yMin, yMax);
    int zeroCol = xToCol(0.0, xMin, xMax);

    // แกน X
    if (zeroRow >= 0 && zeroRow < height) {
        for (int c = 0; c < width; ++c) grid[zeroRow][c] = '-';
    }
    // แกน Y
    if (zeroCol >= 0 && zeroCol < width) {
        for (int r = 0; r < height; ++r) grid[r][zeroCol] = '|';
    }
    // จุดตัด (0,0)
    if (zeroRow >= 0 && zeroRow < height && zeroCol >= 0 && zeroCol < width) {
        grid[zeroRow][zeroCol] = '+';
    }
}

void MathGraph::render(const std::vector<std::string>& grid, double xMin, double xMax, double yMin, double yMax, const std::string& title) const {
    std::cout << "\n--- ASCII High-Res Graph (" << width << "x" << height << "): " << title << " ---\n";
    for (int r = 0; r < height; ++r) {
        std::cout << grid[r] << "\n";
    }
    std::cout << "x Range: [" << xMin << " to " << xMax << "], y Range: [" << yMin << " to " << yMax << "]\n\n";
}

// ===========================================================================
//  ลูปควบคุมมุมมองกราฟ (ใช้ร่วมกันทุกฟังก์ชัน Interactive)
// ===========================================================================
void MathGraph::interactiveView(double initialXMin, double initialXMax,
                                const std::function<void(double, double)>& drawFn) {
    double currentXMin = initialXMin;
    double currentXMax = initialXMax;

    if (currentXMin >= currentXMax) std::swap(currentXMin, currentXMax);
    if (std::abs(currentXMax - currentXMin) < 1e-9) {
        currentXMin -= 1.0;
        currentXMax += 1.0;
    }

    // บันทึกขนาดตั้งต้นไว้สำหรับใช้ Reset
    const int initialWidth = this->width;
    const int initialHeight = this->height;

    int choice = 0;

    while (true) {
        clearScreen();

        // วาดกราฟ 1 เฟรมตามช่วงปัจจุบัน (กันไม่ให้ error ของสมการทำให้หลุดลูป)
        try {
            drawFn(currentXMin, currentXMax);
        } catch (const std::exception& e) {
            std::cout << "\033[1;31m[Error] " << e.what() << "\033[0m\n";
        } catch (...) {
            std::cout << "\033[1;31m[Error] ไม่สามารถวาดกราฟในช่วงนี้ได้\033[0m\n";
        }

        // เรียกใช้ฟังก์ชันเมนูควบคุมจาก menu.hpp
        resolution_graph();

        if (!(std::cin >> choice)) {
            if (std::cin.eof()) break;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        if (choice == 0) break;

        if (choice == 1) {
            double xCenter = (currentXMin + currentXMax) / 2.0;
            double zoomFactor = 2.0;
            if (!readDouble("Enter the desired x-position for zooming in (center): ", xCenter)) break;
            if (!readDouble("Enter the zoom ratio (e.g., 2 = 2x): ", zoomFactor)) break;
            if (zoomFactor <= 0) zoomFactor = 2.0;

            double span = (currentXMax - currentXMin) / zoomFactor;
            currentXMin = xCenter - (span / 2.0);
            currentXMax = xCenter + (span / 2.0);
        } else if (choice == 2) {
            double xCenter = (currentXMin + currentXMax) / 2.0;
            double zoomFactor = 2.0;
            if (!readDouble("Enter the zoom out ratio (e.g., 2 = 2x): ", zoomFactor)) break;
            if (zoomFactor <= 0) zoomFactor = 2.0;

            double span = (currentXMax - currentXMin) * zoomFactor;
            currentXMin = xCenter - (span / 2.0);
            currentXMax = xCenter + (span / 2.0);
        } else if (choice == 3) {
            double newMin = currentXMin, newMax = currentXMax;
            if (!readDouble("Enter the new xMin value: ", newMin)) break;
            if (!readDouble("Enter the new xMax value: ", newMax)) break;
            if (newMin >= newMax) {
                std::cout << "[Warning] xMin must be less than xMax, swapping values...\n";
                std::swap(newMin, newMax);
            }
            currentXMin = newMin;
            currentXMax = newMax;
        } else if (choice == 4) {
            int newW = width, newH = height;
            if (!readInt("Enter the new width: ", newW)) break;
            if (!readInt("Enter the new height: ", newH)) break;
            setResolution(newW, newH);
        } else if (choice == 5) {
            // คืนค่าขอบเขตและขนาด Resolution กลับสู่จุดเริ่มต้น
            currentXMin = initialXMin;
            currentXMax = initialXMax;
            setResolution(initialWidth, initialHeight);
            std::cout << "\n\033[1;32m[Success] Reset ค่าทั้งหมดกลับเป็นเริ่มต้นเรียบร้อยแล้ว!\033[0m\n";
        } else {
            std::cout << "\033[1;31m[Warning] คำสั่งไม่ถูกต้อง (0-5)\033[0m\n";
        }

        // กันช่วงกราฟแคบเกินจนคำนวณไม่ได้
        if (std::abs(currentXMax - currentXMin) < 1e-9) {
            currentXMin -= 1e-6;
            currentXMax += 1e-6;
        }
    }

    // เคลียร์ตัวอักษรขึ้นบรรทัดใหม่ที่ค้างอยู่ เพื่อให้ getline ของเมนูหลักทำงานปกติ
    if (std::cin.good()) {
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

// ---------------------------------------------------------------------------
// เรนเดอร์พื้นที่ (fill) ในคอลัมน์เดียวระหว่างแถว rowA กับ rowB
// ---------------------------------------------------------------------------
void MathGraph::fillAreaColumn(std::vector<std::string>& grid, int col, int rowA, int rowB, char fillChar) const {
    if (col < 0 || col >= width) return;
    int rStart = std::min(rowA, rowB);
    int rEnd = std::max(rowA, rowB);
    for (int r = rStart; r <= rEnd; ++r) {
        if (r >= 0 && r < height) grid[r][col] = fillChar;
    }
}

// ===========================================================================
//  Single-frame plots
// ===========================================================================
void MathGraph::plotFunction(Calculator& calc, const std::string& expr, double xMin, double xMax) {
    std::vector<std::string> grid(height, std::string(width, ' '));

    std::vector<double> xVals(width), yVals(width);
    std::vector<bool> valid(width, false);
    std::vector<double> validY;

    double dx = (xMax - xMin) / (width - 1);

    for (int i = 0; i < width; ++i) {
        double x = xMin + i * dx;
        xVals[i] = x;
        try {
            calc.setVariable('x', x);
            double y = calc.evaluate(expr);

            if (!std::isnan(y) && !std::isinf(y) && std::abs(y) < 1e5) {
                yVals[i] = y;
                valid[i] = true;
                validY.push_back(y);
            }
        } catch (...) {
            valid[i] = false;
        }
    }

    if (validY.empty()) {
        std::cout << "\n[Error] Unable to evaluate equation properly. Please check syntax.\n";
        return;
    }

    std::sort(validY.begin(), validY.end());

    size_t lowerIdx = validY.size() * 0.05;
    size_t upperIdx = validY.size() * 0.95;
    if (upperIdx >= validY.size()) upperIdx = validY.size() - 1;

    double yMin = validY[lowerIdx];
    double yMax = validY[upperIdx];

    if (std::abs(yMax - yMin) < 1e-6) {
        yMin -= 1.0;
        yMax += 1.0;
    }

    drawAxes(grid, xMin, xMax, yMin, yMax);

    for (int i = 0; i < width; ++i) {
        if (!valid[i]) continue;
        if (yVals[i] < yMin || yVals[i] > yMax) continue;

        int row = yToRow(yVals[i], yMin, yMax);
        if (row >= 0 && row < height) {
            grid[row][i] = '*';
        }
    }

    render(grid, xMin, xMax, yMin, yMax, "f(x) = " + expr);
}

void MathGraph::plotInverseFunction(Calculator& calc, const std::string& expr, double xMin, double xMax) {
    if (xMin > xMax) std::swap(xMin, xMax);
    if (std::abs(xMax - xMin) < 1e-9) { xMin -= 1.0; xMax += 1.0; }

    // แกน x และ y ใช้ช่วงเดียวกัน เพื่อให้การสะท้อนข้ามเส้น y = x ดูสมมาตร
    const double yMin = xMin, yMax = xMax;
    std::vector<std::string> grid(height, std::string(width, ' '));
    drawAxes(grid, xMin, xMax, yMin, yMax);

    // เส้น y = x
    for (int c = 0; c < width; ++c) {
        double x = xMin + (xMax - xMin) * c / (width - 1);
        int row = yToRow(x, yMin, yMax);
        if (row >= 0 && row < height && grid[row][c] == ' ') grid[row][c] = '.';
    }

    // สุ่มตัวอย่างหนาแน่นกว่าความกว้างจอ เพื่อให้เส้นโค้งชันไม่ขาดตอน
    const int samples = std::max(width, height) * 8;
    std::vector<std::pair<double, double>> pts; // (t, f(t))
    for (int i = 0; i <= samples; ++i) {
        double t = xMin + (xMax - xMin) * i / samples;
        try {
            calc.setVariable('x', t);
            double y = calc.evaluate(expr);
            if (std::isfinite(y) && std::abs(y) < 1e5) pts.emplace_back(t, y);
        } catch (...) {}
    }
    if (pts.empty()) {
        std::cout << "\n[Error] Unable to evaluate equation properly. Please check syntax.\n";
        return;
    }

    auto mark = [&](double px, double py, char ch) {
        if (px < xMin || px > xMax || py < yMin || py > yMax) return;
        int col = xToCol(px, xMin, xMax), row = yToRow(py, yMin, yMax);
        if (row >= 0 && row < height && col >= 0 && col < width) grid[row][col] = ch;
    };
    for (const auto& p : pts) mark(p.first, p.second, '*');   // f(x)
    for (const auto& p : pts) mark(p.second, p.first, 'o');   // f^-1(x) : จุดที่สลับ x <-> y

    render(grid, xMin, xMax, yMin, yMax, "f(x)=[*] " + expr + "   f^-1(x)=[o]   y=x [.]");

    // วิเคราะห์ว่า f มีฟังก์ชันผกผันจริงหรือไม่ (ต้องเป็น 1-1)
    try {
        FunctionSolver fs(calc);
        if (fs.isOneToOne(expr)) {
            std::string inv = fs.inverseExpression(expr);
            if (!inv.empty()) std::cout << " f^-1(x) = " << inv << "\n\n";
            else std::cout << " f is one-to-one, so f^-1 is a function (no closed form shown).\n\n";
        } else {
            std::cout << " \033[1;33m[Note] f is not one-to-one, so [o] is only the reflected relation, not a function.\n"
                      << "        Restrict the domain (e.g. x >= 0 for x^2) to get a true inverse function.\033[0m\n\n";
        }
    } catch (...) {
        // วิเคราะห์ไม่ได้ ก็แสดงเฉพาะกราฟ
    }
}

void MathGraph::plotPolynomialWithRoots(Calculator& calc, const std::vector<double>& coeffs, double xMin, double xMax) {
    std::string expr = "";
    int deg = static_cast<int>(coeffs.size()) - 1;
    for (int i = 0; i <= deg; ++i) {
        double c = coeffs[i];
        if (c == 0) continue;
        int p = deg - i;
        if (!expr.empty() && c > 0) expr += " + ";
        if (c < 0) expr += " - ";
        double absC = std::abs(c);
        if (absC != 1 || p == 0) expr += std::to_string(absC);
        if (p > 0) expr += "x";
        if (p > 1) expr += "^" + std::to_string(p);
    }
    if (expr.empty()) expr = "0";

    plotFunction(calc, expr, xMin, xMax);

    AlphaSolverEq solver;
    auto roots = solver.solvePolynomial(coeffs);
    std::cout << "\n[ Roots plotted on Graph ]\n";
    for (size_t i = 0; i < roots.size(); ++i) {
        if (std::abs(roots[i].imag()) < 1e-7) {
            std::cout << " Root " << i + 1 << ": x = " << roots[i].real() << "\n";
        }
    }
}

void MathGraph::plotDerivative(Calculator& calc, const std::string& expr, double x0, double xMin, double xMax) {
    double slope = calc.derivative(expr, x0);
    calc.setVariable('x', x0);
    double y0 = calc.evaluate(expr);

    std::vector<std::string> grid(height, std::string(width, ' '));
    std::vector<double> validY;
    double dx = (xMax - xMin) / (width - 1);

    std::vector<double> yFunc(width), yTan(width);
    std::vector<bool> valid(width, false);

    for (int i = 0; i < width; ++i) {
        double x = xMin + i * dx;
        try {
            calc.setVariable('x', x);
            double y = calc.evaluate(expr);
            if (!std::isnan(y) && !std::isinf(y) && std::abs(y) < 1e5) {
                yFunc[i] = y;
                yTan[i] = slope * (x - x0) + y0;
                valid[i] = true;
                validY.push_back(y);
                validY.push_back(yTan[i]);
            }
        } catch (...) {
            valid[i] = false;
        }
    }

    if (validY.empty()) {
        std::cout << "\n[Error] Unable to evaluate equation in this range.\n";
        return;
    }

    std::sort(validY.begin(), validY.end());
    double yMin = validY[validY.size() * 0.05];
    double yMax = validY[validY.size() * 0.95];
    if (std::abs(yMax - yMin) < 1e-6) { yMin -= 1.0; yMax += 1.0; }

    drawAxes(grid, xMin, xMax, yMin, yMax);

    for (int i = 0; i < width; ++i) {
        if (!valid[i]) continue;

        int rTan = yToRow(yTan[i], yMin, yMax);
        if (rTan >= 0 && rTan < height) grid[rTan][i] = '/';

        int rFunc = yToRow(yFunc[i], yMin, yMax);
        if (rFunc >= 0 && rFunc < height) grid[rFunc][i] = '*';
    }

    int r0 = yToRow(y0, yMin, yMax);
    int c0 = xToCol(x0, xMin, xMax);
    if (r0 >= 0 && r0 < height && c0 >= 0 && c0 < width) grid[r0][c0] = 'O';

    render(grid, xMin, xMax, yMin, yMax, "f(x) = " + expr + " | Tangent at x=" + std::to_string(x0));
    std::cout << " Slope m (d/dx at x=" << x0 << ") = " << slope << "\n\n";
}

void MathGraph::plotIntegralArea(Calculator& calc, const std::string& expr, double a, double b, double xMin, double xMax) {
    double area = calc.integral(expr, a, b);

    double plotA = std::isinf(a) ? (a < 0 ? xMin : xMax) : a;
    double plotB = std::isinf(b) ? (b < 0 ? xMin : xMax) : b;
    if (plotA > plotB) std::swap(plotA, plotB);

    std::vector<std::string> grid(height, std::string(width, ' '));
    std::vector<double> validY;
    double dx = (xMax - xMin) / (width - 1);

    std::vector<double> yFunc(width);
    std::vector<bool> valid(width, false);

    for (int i = 0; i < width; ++i) {
        double x = xMin + i * dx;
        try {
            calc.setVariable('x', x);
            double y = calc.evaluate(expr);
            if (!std::isnan(y) && !std::isinf(y) && std::abs(y) < 1e5) {
                yFunc[i] = y;
                valid[i] = true;
                validY.push_back(y);
            }
        } catch (...) {
            valid[i] = false;
        }
    }

    if (validY.empty()) {
        std::cout << "\n[Error] Unable to evaluate equation in this range.\n";
        return;
    }

    std::sort(validY.begin(), validY.end());
    double yMin = std::min(0.0, validY[0]);
    double yMax = std::max(0.0, validY.back());
    if (std::abs(yMax - yMin) < 1e-6) { yMin -= 1.0; yMax += 1.0; }

    drawAxes(grid, xMin, xMax, yMin, yMax);
    int zeroRow = yToRow(0.0, yMin, yMax);

    for (int i = 0; i < width; ++i) {
        if (!valid[i]) continue;
        double x = xMin + i * dx;

        int rFunc = yToRow(yFunc[i], yMin, yMax);

        if (x >= plotA && x <= plotB && zeroRow >= 0 && zeroRow < height) {
            fillAreaColumn(grid, i, rFunc, zeroRow, '#');
        }

        if (rFunc >= 0 && rFunc < height) grid[rFunc][i] = '*';
    }

    std::string strA = std::isinf(a) ? (a > 0 ? "inf" : "-inf") : std::to_string(a);
    std::string strB = std::isinf(b) ? (b > 0 ? "inf" : "-inf") : std::to_string(b);

    render(grid, xMin, xMax, yMin, yMax, "Integral Area of f(x) over [" + strA + ", " + strB + "]");
    std::cout << " Definite Integral (Area) = " << area << "\n\n";
}

void MathGraph::plotAreaBetweenCurves(Calculator& calc, const std::string& expr1, const std::string& expr2,
                                      double a, double b, double xMin, double xMax) {
    if (a > b) std::swap(a, b);

    // คำนวณพื้นที่ล้อมรอบจริง (ใช้ |f1(x) - f2(x)| เพื่อให้ได้ค่าพื้นที่เป็นบวกเสมอ
    // ไม่ว่าเส้นไหนจะอยู่บนหรือล่าง)
    double area = 0.0;
    bool hasArea = true;
    try {
        area = calc.integral("abs((" + expr1 + ")-(" + expr2 + "))", a, b);
    } catch (...) {
        hasArea = false;
    }

    std::vector<std::string> grid(height, std::string(width, ' '));
    std::vector<double> validY;
    double dx = (xMax - xMin) / (width - 1);

    std::vector<double> y1(width), y2(width);
    std::vector<bool> valid(width, false);

    for (int i = 0; i < width; ++i) {
        double x = xMin + i * dx;
        try {
            calc.setVariable('x', x);
            double v1 = calc.evaluate(expr1);
            double v2 = calc.evaluate(expr2);
            if (!std::isnan(v1) && !std::isinf(v1) && std::abs(v1) < 1e5 &&
                !std::isnan(v2) && !std::isinf(v2) && std::abs(v2) < 1e5) {
                y1[i] = v1;
                y2[i] = v2;
                valid[i] = true;
                validY.push_back(v1);
                validY.push_back(v2);
            }
        } catch (...) {
            valid[i] = false;
        }
    }

    if (validY.empty()) {
        std::cout << "\n[Error] Unable to evaluate f1(x) or f2(x) in this range.\n";
        return;
    }

    std::sort(validY.begin(), validY.end());
    double yMin = validY[validY.size() * 0.05];
    double yMax = validY[validY.size() * 0.95];
    if (std::abs(yMax - yMin) < 1e-6) { yMin -= 1.0; yMax += 1.0; }

    drawAxes(grid, xMin, xMax, yMin, yMax);

    for (int i = 0; i < width; ++i) {
        if (!valid[i]) continue;
        double x = xMin + i * dx;

        int r1 = yToRow(y1[i], yMin, yMax);
        int r2 = yToRow(y2[i], yMin, yMax);

        // เรนเดอร์พื้นที่ล้อมรอบระหว่างสองเส้นเฉพาะในช่วง [a, b]
        if (x >= a && x <= b) {
            fillAreaColumn(grid, i, r1, r2, '#');
        }

        if (r1 >= 0 && r1 < height) grid[r1][i] = '*';
        if (r2 >= 0 && r2 < height) grid[r2][i] = '~';
    }

    render(grid, xMin, xMax, yMin, yMax,
          "Area between f1(x)=[*] " + expr1 + " and f2(x)=[~] " + expr2);

    if (hasArea) {
        std::cout << " Enclosed Area over [" << a << ", " << b << "] = " << area << "\n\n";
    } else {
        std::cout << " [Error] Unable to compute enclosed area over [" << a << ", " << b << "]\n\n";
    }
}

void MathGraph::plotLimit(Calculator& calc, const std::string& expr, double aTarget, double xMin, double xMax) {
    double limVal = 0.0;
    bool hasLimit = true;
    std::string limitStatus = "";

    std::string strTarget = std::isinf(aTarget) ? (aTarget > 0 ? "inf" : "-inf") : std::to_string(aTarget);

    try {
        limVal = calc.limit(expr, aTarget);
        if (std::isinf(limVal)) {
            limitStatus = "lim (x->" + strTarget + ") = " + (limVal > 0 ? "inf" : "-inf");
        } else {
            limitStatus = "lim (x->" + strTarget + ") = " + std::to_string(limVal);
        }
    } catch (const std::exception& e) {
        hasLimit = false;
        limitStatus = std::string("Limit undefined/does not exist (") + e.what() + ")";
    }

    std::vector<std::string> grid(height, std::string(width, ' '));
    std::vector<double> validY;
    double dx = (xMax - xMin) / (width - 1);

    std::vector<double> yFunc(width);
    std::vector<bool> valid(width, false);

    for (int i = 0; i < width; ++i) {
        double x = xMin + i * dx;
        try {
            calc.setVariable('x', x);
            double y = calc.evaluate(expr);
            if (!std::isnan(y) && !std::isinf(y) && std::abs(y) < 1e5) {
                yFunc[i] = y;
                valid[i] = true;
                validY.push_back(y);
            }
        } catch (...) {
            valid[i] = false;
        }
    }

    if (validY.empty()) {
        std::cout << "\n[Error] Unable to evaluate equation in this range.\n";
        return;
    }

    std::sort(validY.begin(), validY.end());
    double yMin = validY[validY.size() * 0.05];
    double yMax = validY[validY.size() * 0.95];
    if (std::abs(yMax - yMin) < 1e-6) { yMin -= 1.0; yMax += 1.0; }

    drawAxes(grid, xMin, xMax, yMin, yMax);

    if (!std::isinf(aTarget)) {
        int colTarget = xToCol(aTarget, xMin, xMax);
        if (colTarget >= 0 && colTarget < width) {
            for (int r = 0; r < height; ++r) {
                if (grid[r][colTarget] == ' ') grid[r][colTarget] = ':';
            }
        }

        if (hasLimit && !std::isinf(limVal)) {
            int rLim = yToRow(limVal, yMin, yMax);
            if (rLim >= 0 && rLim < height && colTarget >= 0 && colTarget < width) {
                grid[rLim][colTarget] = 'O';
            }
        }
    }

    for (int i = 0; i < width; ++i) {
        if (!valid[i]) continue;
        int rFunc = yToRow(yFunc[i], yMin, yMax);
        if (rFunc >= 0 && rFunc < height) grid[rFunc][i] = '*';
    }

    render(grid, xMin, xMax, yMin, yMax, "Limit Plot of f(x) at x -> " + strTarget);
    std::cout << " Result: " << limitStatus << "\n\n";
}

void MathGraph::plotIndefiniteIntegral(Calculator& calc, const std::string& expr, double x_ref, double xMin, double xMax) {
    std::vector<std::string> grid(height, std::string(width, ' '));
    std::vector<double> validY;
    double dx = (xMax - xMin) / (width - 1);

    std::vector<double> yFunc(width), yIntegral(width);
    std::vector<bool> valid(width, false);

    for (int i = 0; i < width; ++i) {
        double x = xMin + i * dx;
        try {
            calc.setVariable('x', x);
            double y = calc.evaluate(expr);
            double F_x = calc.integral(expr, x_ref, x);

            if (!std::isnan(y) && !std::isinf(y) && !std::isnan(F_x) && !std::isinf(F_x)) {
                yFunc[i] = y;
                yIntegral[i] = F_x;
                valid[i] = true;
                validY.push_back(y);
                validY.push_back(F_x);
            }
        } catch (...) {
            valid[i] = false;
        }
    }

    if (validY.empty()) {
        std::cout << "\n[Error] Unable to evaluate equation in this range.\n";
        return;
    }

    std::sort(validY.begin(), validY.end());
    double yMin = validY[validY.size() * 0.05];
    double yMax = validY[validY.size() * 0.95];
    if (std::abs(yMax - yMin) < 1e-6) { yMin -= 1.0; yMax += 1.0; }

    drawAxes(grid, xMin, xMax, yMin, yMax);

    for (int i = 0; i < width; ++i) {
        if (!valid[i]) continue;

        int rInt = yToRow(yIntegral[i], yMin, yMax);
        if (rInt >= 0 && rInt < height) grid[rInt][i] = 'I';

        int rFunc = yToRow(yFunc[i], yMin, yMax);
        if (rFunc >= 0 && rFunc < height) grid[rFunc][i] = '*';
    }

    render(grid, xMin, xMax, yMin, yMax, "f(x) [*] vs Indefinite Integral F(x) [I] (ref=" + std::to_string(x_ref) + ")");
}

void MathGraph::plotDataTableRange(const DataTable& table, int xCol, int yCol, const std::string& title,
                                   double xMin, double xMax) {
    int rows = table.getRows();
    if (rows == 0) return;

    auto xVec = table.getColumn(xCol);
    auto yVec = table.getColumn(yCol);

    // เลือกเฉพาะจุดที่อยู่ในช่วง x ที่กำลังมองอยู่ เพื่อคำนวณช่วง y ให้พอดี
    std::vector<double> yInView;
    for (int i = 0; i < rows; ++i) {
        if (xVec[i] >= xMin && xVec[i] <= xMax) yInView.push_back(yVec[i]);
    }

    if (yInView.empty()) {
        std::cout << "\n[Info] ไม่มีจุดข้อมูลในช่วง x [" << xMin << ", " << xMax << "]\n\n";
        return;
    }

    double yMin = *std::min_element(yInView.begin(), yInView.end());
    double yMax = *std::max_element(yInView.begin(), yInView.end());
    if (std::abs(yMax - yMin) < 1e-6) { yMin -= 1.0; yMax += 1.0; }

    std::vector<std::string> grid(height, std::string(width, ' '));
    drawAxes(grid, xMin, xMax, yMin, yMax);

    for (int i = 0; i < rows; ++i) {
        if (xVec[i] < xMin || xVec[i] > xMax) continue;

        int col = xToCol(xVec[i], xMin, xMax);
        int row = yToRow(yVec[i], yMin, yMax);

        if (row >= 0 && row < height && col >= 0 && col < width) {
            grid[row][col] = '*';
        }
    }

    render(grid, xMin, xMax, yMin, yMax, title);
}

void MathGraph::plotDataTable(const DataTable& table, int xCol, int yCol, const std::string& title) {
    int rows = table.getRows();
    if (rows == 0) return;

    auto xVec = table.getColumn(xCol);
    double xMin = *std::min_element(xVec.begin(), xVec.end());
    double xMax = *std::max_element(xVec.begin(), xVec.end());
    if (std::abs(xMax - xMin) < 1e-6) { xMin -= 1.0; xMax += 1.0; }

    plotDataTableRange(table, xCol, yCol, title, xMin, xMax);
}

// ===========================================================================
//  Interactive wrappers — ทุกฟังก์ชันได้ Zoom / Resolution / Reset เหมือนกันหมด
// ===========================================================================
void MathGraph::plotFunctionInteractive(Calculator& calc, const std::string& expr,
                                        double initialXMin, double initialXMax) {
    interactiveView(initialXMin, initialXMax, [&](double xMin, double xMax) {
        plotFunction(calc, expr, xMin, xMax);
    });
}

void MathGraph::plotInverseFunctionInteractive(Calculator& calc, const std::string& expr,
                                               double initialXMin, double initialXMax) {
    interactiveView(initialXMin, initialXMax, [&](double xMin, double xMax) {
        plotInverseFunction(calc, expr, xMin, xMax);
    });
}

void MathGraph::plotPolynomialWithRootsInteractive(Calculator& calc, const std::vector<double>& coeffs,
                                                   double initialXMin, double initialXMax) {
    interactiveView(initialXMin, initialXMax, [&](double xMin, double xMax) {
        plotPolynomialWithRoots(calc, coeffs, xMin, xMax);
    });
}

void MathGraph::plotDerivativeInteractive(Calculator& calc, const std::string& expr, double x0,
                                          double initialXMin, double initialXMax) {
    interactiveView(initialXMin, initialXMax, [&](double xMin, double xMax) {
        plotDerivative(calc, expr, x0, xMin, xMax);
    });
}

void MathGraph::plotIntegralAreaInteractive(Calculator& calc, const std::string& expr, double a, double b,
                                            double initialXMin, double initialXMax) {
    interactiveView(initialXMin, initialXMax, [&](double xMin, double xMax) {
        plotIntegralArea(calc, expr, a, b, xMin, xMax);
    });
}

void MathGraph::plotLimitInteractive(Calculator& calc, const std::string& expr, double aTarget,
                                     double initialXMin, double initialXMax) {
    interactiveView(initialXMin, initialXMax, [&](double xMin, double xMax) {
        plotLimit(calc, expr, aTarget, xMin, xMax);
    });
}

void MathGraph::plotIndefiniteIntegralInteractive(Calculator& calc, const std::string& expr, double x_ref,
                                                  double initialXMin, double initialXMax) {
    interactiveView(initialXMin, initialXMax, [&](double xMin, double xMax) {
        plotIndefiniteIntegral(calc, expr, x_ref, xMin, xMax);
    });
}

void MathGraph::plotAreaBetweenCurvesInteractive(Calculator& calc, const std::string& expr1, const std::string& expr2,
                                                 double a, double b,
                                                 double initialXMin, double initialXMax) {
    interactiveView(initialXMin, initialXMax, [&](double xMin, double xMax) {
        plotAreaBetweenCurves(calc, expr1, expr2, a, b, xMin, xMax);
    });
}

void MathGraph::plotDataTableInteractive(const DataTable& table, int xCol, int yCol, const std::string& title) {
    int rows = table.getRows();
    if (rows == 0) {
        std::cout << "\n[Error] DataTable is empty.\n";
        return;
    }

    auto xVec = table.getColumn(xCol);
    double xMin = *std::min_element(xVec.begin(), xVec.end());
    double xMax = *std::max_element(xVec.begin(), xVec.end());
    if (std::abs(xMax - xMin) < 1e-6) { xMin -= 1.0; xMax += 1.0; }

    interactiveView(xMin, xMax, [&](double curXMin, double curXMax) {
        plotDataTableRange(table, xCol, yCol, title, curXMin, curXMax);
    });
}