#include "../lib/table_calculate.hpp"
#include "../lib/Calculator_calculus.hpp"
#include "../lib/math_graph.hpp"
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <sstream>

// ============================================================================
// DataTable Implementation
// ============================================================================

DataTable::DataTable(int r, int c) : rows(r), cols(c) {
    data = std::vector<std::vector<double>>(r, std::vector<double>(c, 0.0));
    headers.resize(c);
    for (int j = 0; j < c; ++j) {
        headers[j] = "Col_" + std::to_string(j + 1);
    }
}

DataTable::DataTable(const std::vector<std::vector<double>>& inputData, const std::vector<std::string>& colHeaders) {
    rows = inputData.size();
    cols = rows > 0 ? inputData[0].size() : 0;
    data = inputData;
    headers = colHeaders;
    if (headers.size() < static_cast<size_t>(cols)) {
        for (size_t j = headers.size(); j < static_cast<size_t>(cols); ++j) {
            headers.push_back("Col_" + std::to_string(j + 1));
        }
    }
}

void DataTable::resize(int r, int c) {
    rows = r;
    cols = c;
    data.resize(r, std::vector<double>(c, 0.0));
    for (int i = 0; i < r; ++i) {
        data[i].resize(c, 0.0);
    }
    if (headers.size() < static_cast<size_t>(c)) {
        for (size_t j = headers.size(); j < static_cast<size_t>(c); ++j) {
            headers.push_back("Col_" + std::to_string(j + 1));
        }
    } else {
        headers.resize(c);
    }
}

void DataTable::setHeader(int col, const std::string& name) {
    if (col >= 0 && col < cols) headers[col] = name;
}

void DataTable::setHeaders(const std::vector<std::string>& colHeaders) {
    headers = colHeaders;
    if (headers.size() < static_cast<size_t>(cols)) {
        for (size_t j = headers.size(); j < static_cast<size_t>(cols); ++j) {
            headers.push_back("Col_" + std::to_string(j + 1));
        }
    }
}

std::string DataTable::getHeader(int col) const {
    if (col >= 0 && col < cols) return headers[col];
    return "";
}

void DataTable::setCell(int r, int c, double val) {
    if (r >= 0 && r < rows && c >= 0 && c < cols) {
        data[r][c] = val;
    }
}

double DataTable::getCell(int r, int c) const {
    if (r >= 0 && r < rows && c >= 0 && c < cols) {
        return data[r][c];
    }
    return 0.0;
}

std::vector<double> DataTable::getRow(int r) const {
    if (r >= 0 && r < rows) return data[r];
    return {};
}

std::vector<double> DataTable::getColumn(int c) const {
    std::vector<double> colVec(rows, 0.0);
    if (c >= 0 && c < cols) {
        for (int i = 0; i < rows; ++i) colVec[i] = data[i][c];
    }
    return colVec;
}

void DataTable::setRow(int r, const std::vector<double>& values) {
    if (r >= 0 && r < rows) {
        for (int j = 0; j < std::min(cols, static_cast<int>(values.size())); ++j) {
            data[r][j] = values[j];
        }
    }
}

void DataTable::setColumn(int c, const std::vector<double>& values) {
    if (c >= 0 && c < cols) {
        for (int i = 0; i < std::min(rows, static_cast<int>(values.size())); ++i) {
            data[i][c] = values[i];
        }
    }
}

void DataTable::printTable() const {
    if (cols == 0) return;

    // 1. คำนวณความกว้างของแต่ละคอลัมน์ให้อย่างน้อยเท่ากับหัวข้อ หรือความยาวของตัวเลข
    std::vector<size_t> colWidths(cols, 0);
    for (int j = 0; j < cols; ++j) {
        colWidths[j] = std::max(colWidths[j], headers[j].length());
    }

    // แปลงข้อมูลตัวเลขเป็น String เพื่อหาความกว้างที่เหมาะสม
    std::vector<std::vector<std::string>> formattedData(rows, std::vector<std::string>(cols));
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(5) << data[i][j];
            formattedData[i][j] = ss.str();
            colWidths[j] = std::max(colWidths[j], formattedData[i][j].length());
        }
    }

    // เพิ่ม padding เล็กน้อยให้ดูสบายตา (เช่น ข้างละ 1 ช่อง)
    for (int j = 0; j < cols; ++j) {
        colWidths[j] += 2;
    }

    // ฟังก์ชันผู้ช่วยสำหรับวาดเส้นแบ่ง (Separator line)
    auto printLine = [&](const std::string& left, const std::string& mid, const std::string& right) {
        std::cout << left;
        for (int j = 0; j < cols; ++j) {
            std::cout << std::string(colWidths[j], '-');
            if (j < cols - 1) std::cout << mid;
        }
        std::cout << right << "\n";
    };

    // 2. วาดขอบบนสุด (Top Border)
    printLine("+", "+", "+");

    // 3. วาดส่วนหัวตาราง (Header)
    std::cout << "|";
    for (int j = 0; j < cols; ++j) {
        size_t totalPad = colWidths[j] - headers[j].length();
        size_t leftPad = totalPad / 2;
        size_t rightPad = totalPad - leftPad;
        std::cout << std::string(leftPad, ' ') << headers[j] << std::string(rightPad, ' ') << "|";
    }
    std::cout << "\n";

    // 4. วาดเส้นกั้นระหว่าง Header กับ Data
    printLine("+", "+", "+");

    // 5. วาดส่วนข้อมูล (Data Rows)
    for (int i = 0; i < rows; ++i) {
        std::cout << "|";
        for (int j = 0; j < cols; ++j) {
            size_t totalPad = colWidths[j] - formattedData[i][j].length();
            size_t leftPad = totalPad / 2;
            size_t rightPad = totalPad - leftPad;
            std::cout << std::string(leftPad, ' ') << formattedData[i][j] << std::string(rightPad, ' ') << "|";
        }
        std::cout << "\n";
    }

    // 6. วาดขอบล่างสุด (Bottom Border)
    printLine("+", "+", "+");
}

// ============================================================================
// TableCalculate Implementation
// ============================================================================

TableCalculate::TableCalculate() {}

// ---------- 1. General Table Operations ----------

double TableCalculate::sumColumn(const DataTable& table, int col) {
    auto v = table.getColumn(col);
    return std::accumulate(v.begin(), v.end(), 0.0);
}

double TableCalculate::meanColumn(const DataTable& table, int col) {
    if (table.getRows() == 0) return 0.0;
    return sumColumn(table, col) / table.getRows();
}

double TableCalculate::minColumn(const DataTable& table, int col) {
    auto v = table.getColumn(col);
    if (v.empty()) return 0.0;
    return *std::min_element(v.begin(), v.end());
}

double TableCalculate::maxColumn(const DataTable& table, int col) {
    auto v = table.getColumn(col);
    if (v.empty()) return 0.0;
    return *std::max_element(v.begin(), v.end());
}

double TableCalculate::varianceColumn(const DataTable& table, int col, bool sample) {
    int n = table.getRows();
    if (n <= (sample ? 1 : 0)) return 0.0;

    double mean = meanColumn(table, col);
    auto v = table.getColumn(col);
    double sqDiff = 0.0;
    for (double x : v) {
        sqDiff += (x - mean) * (x - mean);
    }
    return sqDiff / (sample ? (n - 1) : n);
}

double TableCalculate::stdevColumn(const DataTable& table, int col, bool sample) {
    return std::sqrt(varianceColumn(table, col, sample));
}

double TableCalculate::medianColumn(const DataTable& table, int col) {
    auto v = table.getColumn(col);
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2 == 0) {
        return (v[n / 2 - 1] + v[n / 2]) / 2.0;
    }
    return v[n / 2];
}

DataTable TableCalculate::applyColumnOperation(const DataTable& table, int col1, int col2, 
                                                std::function<double(double, double)> op, 
                                                const std::string& resultHeader) {
    int r = table.getRows();
    DataTable res(r, 1);
    res.setHeader(0, resultHeader);

    for (int i = 0; i < r; ++i) {
        double v1 = table.getCell(i, col1);
        double v2 = table.getCell(i, col2);
        res.setCell(i, 0, op(v1, v2));
    }
    return res;
}

DataTable TableCalculate::applyExpression(const DataTable& table, Calculator& calc, 
                                          const std::string& expr, const std::vector<char>& colVarMap, 
                                          const std::string& resultHeader) {
    int r = table.getRows();
    DataTable res(r, 1);
    res.setHeader(0, resultHeader);

    for (int i = 0; i < r; ++i) {
        for (size_t j = 0; j < colVarMap.size(); ++j) {
            if (j < static_cast<size_t>(table.getCols())) {
                calc.setVariable(colVarMap[j], table.getCell(i, static_cast<int>(j)));
            }
        }
        double val = calc.evaluate(expr);
        res.setCell(i, 0, val);
    }
    return res;
}

DataTable TableCalculate::transpose(const DataTable& table) {
    int r = table.getRows();
    int c = table.getCols();
    DataTable res(c, r);

    for (int i = 0; i < r; ++i) {
        for (int j = 0; j < c; ++j) {
            res.setCell(j, i, table.getCell(i, j));
        }
    }
    return res;
}

DataTable TableCalculate::multiplyScalar(const DataTable& table, double scalar) {
    int r = table.getRows();
    int c = table.getCols();
    DataTable res = table;

    for (int i = 0; i < r; ++i) {
        for (int j = 0; j < c; ++j) {
            res.setCell(i, j, table.getCell(i, j) * scalar);
        }
    }
    return res;
}

DataTable TableCalculate::addTables(const DataTable& t1, const DataTable& t2) {
    if (t1.getRows() != t2.getRows() || t1.getCols() != t2.getCols()) {
        throw std::invalid_argument("Table dimensions must match for addition.");
    }
    int r = t1.getRows();
    int c = t1.getCols();
    DataTable res(r, c);

    for (int i = 0; i < r; ++i) {
        for (int j = 0; j < c; ++j) {
            res.setCell(i, j, t1.getCell(i, j) + t2.getCell(i, j));
        }
    }
    return res;
}

DataTable TableCalculate::multiplyTables(const DataTable& t1, const DataTable& t2) {
    if (t1.getCols() != t2.getRows()) {
        throw std::invalid_argument("Columns of Table 1 must match Rows of Table 2.");
    }
    int r = t1.getRows();
    int c = t2.getCols();
    int kDim = t1.getCols();

    DataTable res(r, c);
    for (int i = 0; i < r; ++i) {
        for (int j = 0; j < c; ++j) {
            double sum = 0.0;
            for (int k = 0; k < kDim; ++k) {
                sum += t1.getCell(i, k) * t2.getCell(k, j);
            }
            res.setCell(i, j, sum);
        }
    }
    return res;
}

// ---------- 2. Probability & Combinatorics ----------

long long TableCalculate::nCr(int n, int r) {
    if (r < 0 || r > n) return 0;
    if (r == 0 || r == n) return 1;
    if (r > n / 2) r = n - r;

    long long res = 1;
    for (int i = 1; i <= r; ++i) {
        res = res * (n - r + i) / i;
    }
    return res;
}

long long TableCalculate::nPr(int n, int r) {
    if (r < 0 || r > n) return 0;
    long long res = 1;
    for (int i = 0; i < r; ++i) {
        res *= (n - i);
    }
    return res;
}

double TableCalculate::binomPDF(int k, int n, double p) {
    if (k < 0 || k > n || p < 0.0 || p > 1.0) return 0.0;
    return static_cast<double>(nCr(n, k)) * std::pow(p, k) * std::pow(1.0 - p, n - k);
}

double TableCalculate::binomCDF(int k, int n, double p) {
    if (k < 0) return 0.0;
    if (k >= n) return 1.0;
    double cdf = 0.0;
    for (int i = 0; i <= k; ++i) {
        cdf += binomPDF(i, n, p);
    }
    return cdf;
}

double TableCalculate::poissonPDF(int k, double lambda) {
    if (k < 0 || lambda <= 0.0) return 0.0;
    // P(X = k) = (lambda^k * e^-lambda) / k!
    double logProb = k * std::log(lambda) - lambda - std::lgamma(k + 1);
    return std::exp(logProb);
}

double TableCalculate::poissonCDF(int k, double lambda) {
    if (k < 0) return 0.0;
    double cdf = 0.0;
    for (int i = 0; i <= k; ++i) {
        cdf += poissonPDF(i, lambda);
    }
    return cdf;
}

double TableCalculate::hypergeomPDF(int k, int N, int K, int n) {
    if (k < 0 || k > K || (n - k) > (N - K) || (n - k) < 0) return 0.0;
    return static_cast<double>(nCr(K, k) * nCr(N - K, n - k)) / static_cast<double>(nCr(N, n));
}

double TableCalculate::geometricPDF(int k, double p) {
    if (k < 1 || p <= 0.0 || p > 1.0) return 0.0;
    return std::pow(1.0 - p, k - 1) * p;
}

double TableCalculate::normalPDF(double x, double mean, double stdev) {
    if (stdev <= 0.0) throw std::invalid_argument("Standard deviation must be positive.");
    static const double inv_sqrt_2pi = 0.3989422804014327;
    double z = (x - mean) / stdev;
    return (inv_sqrt_2pi / stdev) * std::exp(-0.5 * z * z);
}

double TableCalculate::normalCDF(double x, double mean, double stdev) {
    if (stdev <= 0.0) throw std::invalid_argument("Standard deviation must be positive.");
    double z = (x - mean) / stdev;
    return 0.5 * std::erfc(-z * M_SQRT1_2);
}

// ---------- 3. Probability Table Generation ----------

DataTable TableCalculate::generateBinomialTable(int n, double p) {
    DataTable table(n + 1, 3);
    table.setHeaders({"k", "P(X = k)", "P(X <= k)"});

    double cum = 0.0;
    for (int k = 0; k <= n; ++k) {
        double pdf = binomPDF(k, n, p);
        cum += pdf;
        table.setCell(k, 0, k);
        table.setCell(k, 1, pdf);
        table.setCell(k, 2, cum);
    }
    return table;
}

DataTable TableCalculate::generatePoissonTable(double lambda, int maxK) {
    DataTable table(maxK + 1, 3);
    table.setHeaders({"k", "P(X = k)", "P(X <= k)"});

    double cum = 0.0;
    for (int k = 0; k <= maxK; ++k) {
        double pdf = poissonPDF(k, lambda);
        cum += pdf;
        table.setCell(k, 0, k);
        table.setCell(k, 1, pdf);
        table.setCell(k, 2, cum);
    }
    return table;
}

DataTable TableCalculate::generateNormalZTable(double zMin, double zMax, double step) {
    int numRows = static_cast<int>(std::round((zMax - zMin) / step)) + 1;
    DataTable table(numRows, 3);
    table.setHeaders({"Z", "PDF f(z)", "CDF Phi(z)"});

    for (int i = 0; i < numRows; ++i) {
        double z = zMin + i * step;
        table.setCell(i, 0, z);
        table.setCell(i, 1, normalPDF(z));
        table.setCell(i, 2, normalCDF(z));
    }
    return table;
}

DataTable TableCalculate::computeMarginalProbabilities(const DataTable& jointTable) {
    int r = jointTable.getRows();
    int c = jointTable.getCols();

    DataTable res(r + 1, c + 1);
    
    std::vector<std::string> headers;
    for (int j = 0; j < c; ++j) {
        headers.push_back(jointTable.getHeader(j));
    }
    headers.push_back("P(X)");
    res.setHeaders(headers);

    double grandTotal = 0.0;

    for (int i = 0; i < r; ++i) {
        double rowSum = 0.0;
        for (int j = 0; j < c; ++j) {
            double val = jointTable.getCell(i, j);
            res.setCell(i, j, val);
            rowSum += val;
        }
        res.setCell(i, c, rowSum);
        grandTotal += rowSum;
    }

    for (int j = 0; j < c; ++j) {
        double colSum = 0.0;
        for (int i = 0; i < r; ++i) {
            colSum += jointTable.getCell(i, j);
        }
        res.setCell(r, j, colSum);
    }
    res.setCell(r, c, grandTotal);

    return res;
}

double TableCalculate::computeExpectedValue(const std::vector<double>& values, const std::vector<double>& probs) {
    if (values.size() != probs.size()) {
        throw std::invalid_argument("Values and probabilities vectors must have equal size.");
    }
    double ev = 0.0;
    for (size_t i = 0; i < values.size(); ++i) {
        ev += values[i] * probs[i];
    }
    return ev;
}
void TableCalculate::plotTable(const DataTable& table, int xCol, int yCol, int width, int height) {
    MathGraph graph(width, height);
    std::string title = table.getHeader(xCol) + " vs " + table.getHeader(yCol);
    graph.plotDataTable(table, xCol, yCol, title);
}