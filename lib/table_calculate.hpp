#ifndef TABLE_CALCULATE_HPP
#define TABLE_CALCULATE_HPP

#include <vector>
#include <string>
#include <map>
#include <functional>
#include <stdexcept>
#include <iostream>

class Calculator; // Forward declaration for evaluation integration

// Data structure representing a generic 2D Dynamic Table
class DataTable {
private:
    int rows;
    int cols;
    std::vector<std::string> headers;
    std::vector<std::vector<double>> data;

public:
    DataTable(int r = 0, int c = 0);
    DataTable(const std::vector<std::vector<double>>& inputData, const std::vector<std::string>& colHeaders = {});

    void resize(int r, int c);
    int getRows() const { return rows; }
    int getCols() const { return cols; }

    void setHeader(int col, const std::string& name);
    void setHeaders(const std::vector<std::string>& colHeaders);
    std::string getHeader(int col) const;

    void setCell(int r, int c, double val);
    double getCell(int r, int c) const;

    std::vector<double> getRow(int r) const;
    std::vector<double> getColumn(int c) const;
    void setRow(int r, const std::vector<double>& values);
    void setColumn(int c, const std::vector<double>& values);

    void printTable() const;
};

// Main Table Calculator Class
class TableCalculate {
public:
    TableCalculate();

    // ==========================================
    // 1. General Table Operations (การจัดการตารางทั่วไป)
    // ==========================================
    static double sumColumn(const DataTable& table, int col);
    static double meanColumn(const DataTable& table, int col);
    static double minColumn(const DataTable& table, int col);
    static double maxColumn(const DataTable& table, int col);
    static double varianceColumn(const DataTable& table, int col, bool sample = true);
    static double stdevColumn(const DataTable& table, int col, bool sample = true);
    static double medianColumn(const DataTable& table, int col);

    // Apply custom formula or function across columns/rows
    static DataTable applyColumnOperation(const DataTable& table, int col1, int col2, 
                                           std::function<double(double, double)> op, 
                                           const std::string& resultHeader = "Result");
    
    // Evaluate expression string on each row (e.g. "x^2 + 2*y") using external Calculator
    static DataTable applyExpression(const DataTable& table, Calculator& calc, 
                                     const std::string& expr, const std::vector<char>& colVarMap, 
                                     const std::string& resultHeader = "ExprResult");

    // Matrix / Table Operations
    static DataTable transpose(const DataTable& table);
    static DataTable multiplyScalar(const DataTable& table, double scalar);
    static DataTable addTables(const DataTable& t1, const DataTable& t2);
    static DataTable multiplyTables(const DataTable& t1, const DataTable& t2);

    // ==========================================
    // 2. Probability & Statistics Distributions
    // ==========================================
    
    // Discrete Probability Distributions
    static double binomPDF(int k, int n, double p);
    static double binomCDF(int k, int n, double p);
    
    static double poissonPDF(int k, double lambda);
    static double poissonCDF(int k, double lambda);

    static double hypergeomPDF(int k, int N, int K, int n);
    
    static double geometricPDF(int k, double p);

    // Continuous Probability Distributions (Normal Distribution)
    static double normalPDF(double x, double mean = 0.0, double stdev = 1.0);
    static double normalCDF(double x, double mean = 0.0, double stdev = 1.0);
    
    // Combinatorics (nCr, nPr)
    static long long nCr(int n, int r);
    static long long nPr(int n, int r);

    // ==========================================
    // 3. Probability Table Generation
    // ==========================================
    
    // Generates Binomial Probability Distribution Table (k | P(X = k) | P(X <= k))
    static DataTable generateBinomialTable(int n, double p);

    // Generates Poisson Probability Distribution Table (k | P(X = k) | P(X <= k))
    static DataTable generatePoissonTable(double lambda, int maxK = 20);

    // Generates Standard Normal Distribution Z-Table (rows: Z from zMin to zMax with step)
    static DataTable generateNormalZTable(double zMin = -3.4, double zMax = 3.4, double step = 0.1);

    // Generates Joint / Contingency Probability Table & marginal probabilities
    static DataTable computeMarginalProbabilities(const DataTable& jointTable);
    static double computeExpectedValue(const std::vector<double>& values, const std::vector<double>& probs);

    static void plotTable(const DataTable& table, int xCol = 0, int yCol = 1, int width = 60, int height = 20);
};

#endif // TABLE_CALCULATE_HPP
