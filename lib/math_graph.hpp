#ifndef MATH_GRAPH_HPP
#define MATH_GRAPH_HPP

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <complex>
#include <functional>
#include "table_calculate.hpp"

class Calculator;

class MathGraph {
public:
    // ตัวสร้าง Class พร้อมกำหนดความละเอียดเริ่มต้น (Width x Height)
    MathGraph(int width = 100, int height = 30);

    // เมธอดจัดการ ความละเอียด (Resolution)
    void setResolution(int newWidth, int newHeight);
    int getWidth() const { return width; }
    int getHeight() const { return height; }

    // ===================== โหมดวาดครั้งเดียว (Single Frame) =====================
    void plotDataTable(const DataTable& table, int xCol = 0, int yCol = 1, const std::string& title = "DataTable Scatter Plot");
    void plotDataTableRange(const DataTable& table, int xCol, int yCol, const std::string& title, double xMin, double xMax);
    void plotFunction(Calculator& calc, const std::string& expr, double xMin = -10.0, double xMax = 10.0);
    void plotPolynomialWithRoots(Calculator& calc, const std::vector<double>& coeffs, double xMin = -10.0, double xMax = 10.0);
    void plotDerivative(Calculator& calc, const std::string& expr, double x0, double xMin = -10.0, double xMax = 10.0);
    void plotIntegralArea(Calculator& calc, const std::string& expr, double a, double b, double xMin = -10.0, double xMax = 10.0);
    void plotLimit(Calculator& calc, const std::string& expr, double aTarget, double xMin = -10.0, double xMax = 10.0);
    void plotIndefiniteIntegral(Calculator& calc, const std::string& expr, double x_ref = 0.0, double xMin = -10.0, double xMax = 10.0);

    // เรนเดอร์พื้นที่ระหว่างกราฟ 2 เส้น f1(x) และ f2(x) บนช่วง [a, b]
    void plotAreaBetweenCurves(Calculator& calc, const std::string& expr1, const std::string& expr2,
                               double a, double b, double xMin = -10.0, double xMax = 10.0);

    // พล็อตฟังก์ชัน f(x) คู่กับ "ฟังก์ชันผกผัน f^-1(x)" และเส้น y = x
    // กราฟผกผันคือการสะท้อนกราฟ f ข้ามเส้น y = x (จุด (x, f(x)) -> (f(x), x))
    // แกน x และ y ใช้ช่วงเดียวกัน [xMin, xMax] เพื่อให้เห็นการสะท้อนสมมาตรจริง
    // สัญลักษณ์: [*] = f(x), [o] = f^-1(x), [.] = y = x
    void plotInverseFunction(Calculator& calc, const std::string& expr, double xMin = -10.0, double xMax = 10.0);

    // ============ โหมด Interactive ( Zoom In / Zoom Out / Resolution / Reset ) ============
    // ทุกฟังก์ชันด้านล่างใช้เมนูควบคุมชุดเดียวกัน (resolution_graph จาก menu.hpp)
    void plotFunctionInteractive(Calculator& calc, const std::string& expr,
                                 double initialXMin = -10.0, double initialXMax = 10.0);

    void plotInverseFunctionInteractive(Calculator& calc, const std::string& expr,
                                        double initialXMin = -10.0, double initialXMax = 10.0);

    void plotPolynomialWithRootsInteractive(Calculator& calc, const std::vector<double>& coeffs,
                                            double initialXMin = -10.0, double initialXMax = 10.0);

    void plotDerivativeInteractive(Calculator& calc, const std::string& expr, double x0,
                                   double initialXMin = -10.0, double initialXMax = 10.0);

    void plotIntegralAreaInteractive(Calculator& calc, const std::string& expr, double a, double b,
                                     double initialXMin = -10.0, double initialXMax = 10.0);

    void plotLimitInteractive(Calculator& calc, const std::string& expr, double aTarget,
                              double initialXMin = -10.0, double initialXMax = 10.0);

    void plotIndefiniteIntegralInteractive(Calculator& calc, const std::string& expr, double x_ref = 0.0,
                                           double initialXMin = -10.0, double initialXMax = 10.0);

    void plotDataTableInteractive(const DataTable& table, int xCol = 0, int yCol = 1,
                                  const std::string& title = "DataTable Scatter Plot");

    void plotAreaBetweenCurvesInteractive(Calculator& calc, const std::string& expr1, const std::string& expr2,
                                          double a, double b,
                                          double initialXMin = -10.0, double initialXMax = 10.0);

private:
    int width;
    int height;

    // ลูปควบคุมมุมมองกราฟที่ใช้ร่วมกันทุกฟังก์ชัน
    // drawFn(xMin, xMax) = ฟังก์ชันวาดกราฟ 1 เฟรมตามช่วง x ที่ได้รับ
    void interactiveView(double initialXMin, double initialXMax,
                         const std::function<void(double, double)>& drawFn);

    // ฟังก์ชันคำนวณตำแหน่งและเรนเดอร์กราฟ ASCII
    int yToRow(double y, double yMin, double yMax) const;
    int xToCol(double x, double xMin, double xMax) const;
    void drawAxes(std::vector<std::string>& grid, double xMin, double xMax, double yMin, double yMax) const;
    void render(const std::vector<std::string>& grid, double xMin, double xMax, double yMin, double yMax, const std::string& title) const;

    // เรนเดอร์ "พื้นที่" (fill) ในคอลัมน์เดียว ระหว่างแถว rowA กับ rowB ด้วยอักขระที่กำหนด
    // ใช้ร่วมกันทั้ง plotIntegralArea และ plotAreaBetweenCurves เพื่อไม่ให้ตรรกะฟิลพื้นที่ซ้ำซ้อน
    void fillAreaColumn(std::vector<std::string>& grid, int col, int rowA, int rowB, char fillChar) const;
};

#endif // MATH_GRAPH_HPP