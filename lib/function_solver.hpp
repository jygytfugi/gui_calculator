#ifndef FUNCTION_SOLVER_HPP
#define FUNCTION_SOLVER_HPP

// =====================================================================================
//  FunctionSolver : โมดูล "ความสัมพันธ์และฟังก์ชัน" (Relations & Functions, ม.4)
//
//  อ้างอิงเนื้อหาจาก https://www.smartmathpro.com/article/relations-and-functions/
//    - ผลคูณคาร์ทีเซียน, ความสัมพันธ์, โดเมน/เรนจ์, ตัวผกผันของความสัมพันธ์
//    - ฟังก์ชัน (การทดสอบเส้นตรงแนวตั้ง), ฟังก์ชันจาก A ไป B, ไปทั่วถึง, 1-1
//    - ฟังก์ชันเพิ่ม/ลด, พีชคณิตของฟังก์ชัน (f+g, f-g, fg, f/g), ฟังก์ชันประกอบ g∘f
//    - ฟังก์ชันผกผัน f^-1
//
//  ความสามารถที่นำเข้ามาจากไฟล์เดิม:
//    - Calculator        : evaluate / setVariable / derivative / symbolicDerivative
//    - AlphaSolverEq     : normalizeVariable (รองรับตัวแปรอื่นนอกจาก x), solveGeneralEquation
//                          (ขัดเกลาคำตอบของ f(x)=y), getAns() (รองรับคำว่า "ans")
//    - AlgebraCalculator : ผ่าน Calculator (root, logBase, absolute ฯลฯ ใช้ภายใน evaluate)
//
//  หมายเหตุ: ส่วนที่เป็นฟังก์ชันจริง f(x) ทำงานแบบ "เชิงตัวเลข" (สแกนกริด + bisection)
//  จึงเป็นการประมาณที่แม่นยำ ไม่ใช่การพิสูจน์เชิงสัญลักษณ์ (ดูข้อจำกัดใน .cpp)
// =====================================================================================

#include <string>
#include <vector>
#include <utility>
#include <limits>
#include <map>
#include <complex>
#include "Calculator_calculus.hpp"

// ช่วงของจำนวนจริง เช่น [0, inf) หรือ (-inf, -1)  (ปลาย infinity เป็น "ปิด" ไม่ได้เสมอ)
struct Interval {
    double lo, hi;
    bool loClosed, hiClosed;

    Interval(double l = 0.0, double h = -1.0, bool lc = false, bool hc = false)
        : lo(l), hi(h), loClosed(lc), hiClosed(hc) {}

    bool isEmpty() const;
    bool contains(double x) const;
    std::string toString() const;      // เช่น "[0, inf)"
};

// ช่วงที่ฟังก์ชันเป็นฟังก์ชันเพิ่ม/ลด/คงที่
struct MonotonicPiece {
    Interval range;
    int kind;                          // +1 = เพิ่ม, -1 = ลด, 0 = ค่าคงตัว
};

// นิยามฟังก์ชันแบบมีชื่อ เช่น "f(x) = x^2 + 1", "g(t) = 2t - 3" (ชื่อและตัวแปรเป็นตัวอักษรอะไรก็ได้)
struct FunctionDef {
    char name;          // ชื่อฟังก์ชัน (f, g, h, ...)
    char var;           // ตัวแปรของฟังก์ชัน (x, t, a, ...)
    std::string body;   // เนื้อฟังก์ชัน
};

// ผลของการประเมินนิพจน์ที่มีการเรียกฟังก์ชัน เช่น f(x+h) - f(x)
struct CallResult {
    std::string substituted;        // หลังแทน f(...) ด้วยเนื้อฟังก์ชันแล้ว (ยังไม่กระจาย)
    std::string simplified;         // กระจาย + รวมพจน์คล้ายแล้ว (ถ้าเป็นพหุนาม) ไม่งั้นเท่ากับ substituted
    bool isPolynomial = false;
    std::vector<char> variables;    // ตัวแปรที่ยังเหลืออยู่ในผลลัพธ์
    bool hasValue = false;          // true ถ้าไม่มีตัวแปรเหลือ (ได้ค่าตัวเลข)
    double value = 0.0;
};

// ผลของการแก้สมการที่มีการเรียกฟังก์ชัน เช่น f(t+1) = 10
struct CallEquationResult {
    std::string equation;                          // สมการจัดรูปแล้ว (... = 0)
    char unknown = 'x';                            // ตัวแปรที่แก้หา
    bool isPolynomial = false;
    std::string factored;                          // รูปแยกตัวประกอบ (เฉพาะพหุนาม)
    std::vector<std::complex<double>> roots;       // ทุกคำตอบ (พหุนาม: รวมรากเชิงซ้อน)
    std::vector<double> realRoots;                 // เฉพาะรากจริง
    bool identity = false;                         // true ถ้าสมการเป็นจริงทุกค่า
};

// ผลวิเคราะห์ความสัมพันธ์ที่เขียนเป็นสมการ เช่น "x^2 + y^2 = 25" หรือ "y = 1/(x+1)"
struct RelationReport {
    bool isFunction = false;           // ผ่านการทดสอบเส้นตรงแนวตั้งหรือไม่
    double counterExampleX = 0.0;      // ถ้าไม่เป็นฟังก์ชัน: ค่า x ที่ให้ y มากกว่า 1 ค่า
    std::vector<double> counterExampleY;
    std::string standardForm;          // สมการจัดรูปแล้ว: ย้ายข้างเป็น "... = 0" (หรือคงรูป y = f(x) ถ้าเป็นรูปชัดอยู่แล้ว)
    std::string solvedForm;            // สมการแก้หา y แล้ว เช่น "y = ±sqrt(-(x^2 - 25))" (ว่างถ้าจัดรูปให้ไม่ได้)
    bool explicitForm = false;         // true ถ้าอยู่ในรูป y = f(x) พอดี
    std::string explicitExpr;          // f(x) (เมื่อ explicitForm = true)
    std::vector<Interval> domain;
    std::vector<Interval> range;
};

class FunctionSolver {
public:
    using Pair     = std::pair<double, double>;
    using Relation = std::vector<Pair>;      // เซตของคู่อันดับ (เรียงและไม่ซ้ำเสมอ)
    using NumSet   = std::vector<double>;

    explicit FunctionSolver(Calculator& calc);

    // ช่วงสแกนเชิงตัวเลข [-w, w] (ค่าเริ่มต้น 100) และความละเอียดกริดต่อ 1 หน่วย (ค่าเริ่มต้น 50)
    void setScanWindow(double w);
    void setStepsPerUnit(int steps);
    // ช่วงสแกนสำหรับความสัมพันธ์แบบสมการ (implicit) ซึ่งหนักกว่า (ค่าเริ่มต้น 20)
    void setRelationWindow(double w);

    // ============ ส่วนที่ 1: ความสัมพันธ์แบบเซตจำกัด ============

    // A x B
    static Relation cartesianProduct(const NumSet& A, const NumSet& B);

    // ผลคูณคาร์ทีเซียนของ n เซต (n >= 1): A1 x A2 x ... x An -> เซตของ n-tuple (เรียงตามลำดับพจนานุกรม)
    // ถ้ามีเซตใดเป็นเซตว่าง ผลลัพธ์เป็นเซตว่าง; throw ถ้าจำนวนสมาชิกผลลัพธ์เกิน maxResults (กันหน่วยความจำเต็ม)
    using Tuple = std::vector<double>;
    static std::vector<Tuple> cartesianProductN(const std::vector<NumSet>& sets, size_t maxResults = 1000000);
    static std::string toString(const std::vector<Tuple>& tuples, size_t maxShown = 0);   // maxShown = 0 -> แสดงทั้งหมด

    // r = {(x,y) in A x B | condition}  เช่น "x<y", "y=2x+1", "y^2=x", "x+y<=4", "x!=y"
    // รองรับ =, ==, !=, <, <=, >, >=  และการคูณโดยไม่ใส่ * (เช่น 2x)
    Relation relationFromCondition(const NumSet& A, const NumSet& B, const std::string& condition);

    static NumSet   domainOf(const Relation& r);       // D_r
    static NumSet   rangeOf(const Relation& r);        // R_r
    static Relation inverseRelation(const Relation& r);// r^-1 (สลับคู่อันดับ)
    // r^-1 ในรูปเงื่อนไข: สลับ x กับ y เช่น "y=2x+1" -> "x=2y+1"
    static std::string inverseCondition(const std::string& condition);

    static bool isFunction(const Relation& r);                                      // ตัวหน้าเหมือนกัน => ตัวหลังต้องเหมือนกัน
    static bool isFunctionFromAToB(const Relation& r, const NumSet& A, const NumSet& B); // D_r = A และ R_r ⊆ B
    static bool isOntoB(const Relation& r, const NumSet& B);                        // R_r = B
    static bool isOneToOne(const Relation& r);                                      // ตัวหลังไม่ซ้ำ

    static std::string toString(const Relation& r);    // {(1, 3), (2, 3)}
    static std::string toString(const NumSet& s);      // {1, 2, 3}
    static std::string toString(const std::vector<Interval>& v); // (-inf, -1) U (-1, inf)

    // ============ ส่วนที่ 2: ความสัมพันธ์แบบสมการ (จำนวนจริง) ============

    // "y = 1/(x+1)" (รูปชัด) หรือ "x^2 + y^2 = 25" (รูปไม่ชัด -> ทดสอบเส้นตรงแนวตั้งเชิงตัวเลข)
    RelationReport analyzeRelation(const std::string& equation);

    // ============ ส่วนที่ 3: ฟังก์ชัน f(x) ============
    // expr คือ f(x) เช่น "2x+1", "sqrt(x-2)", "1/(x+1)"; var = '\0' คือตรวจหาตัวแปรอัตโนมัติ

    double evaluateAt(const std::string& expr, double x, char var = '\0');

    std::vector<Interval> domain(const std::string& expr, char var = '\0');
    std::vector<Interval> range(const std::string& expr, char var = '\0');
    // จำกัดโดเมนไว้ที่ A (เช่น f : A -> B)
    std::vector<Interval> domainOn(const std::string& expr, const Interval& A, char var = '\0');
    std::vector<Interval> rangeOn(const std::string& expr, const Interval& A, char var = '\0');

    // ฟังก์ชันเพิ่ม/ลด (ใช้ Calculator::symbolicDerivative หาจุดเปลี่ยน)
    std::vector<MonotonicPiece> monotonicity(const std::string& expr, char var = '\0');

    // ประเภทของฟังก์ชัน
    bool isOneToOne(const std::string& expr, char var = '\0');
    bool isOneToOneOn(const std::string& expr, const Interval& A, char var = '\0');
    bool isFunctionFromTo(const std::string& expr, const Interval& A, const Interval& B, char var = '\0'); // A ⊆ D_f และ f(A) ⊆ B
    bool isOnto(const std::string& expr, const Interval& A, const Interval& B, char var = '\0');          // f(A) = B

    // หาทุก x (ในช่วงสแกน) ที่ f(x) = y0
    std::vector<double> solveFunctionEquals(const std::string& expr, double y0, char var = '\0');

    // ---- พีชคณิตของฟังก์ชัน: คืนนิพจน์ใหม่ในตัวแปร x (ส่งต่อให้ domain()/range()/evaluate ได้เลย) ----
    std::string add(const std::string& f, const std::string& g);
    std::string subtract(const std::string& f, const std::string& g);
    std::string multiply(const std::string& f, const std::string& g);
    std::string divide(const std::string& f, const std::string& g);   // โดเมนตัด g(x)=0 ให้อัตโนมัติเมื่อหา domain()
    std::string compose(const std::string& g, const std::string& f);  // g∘f (x) = g(f(x))

    // ---- ฟังก์ชันผกผัน ----
    // ฟังก์ชันเชิงเส้น f(x)=ax+b คืนสูตร f^-1 เป็นสตริง; ถ้าไม่ใช่เชิงเส้นคืนสตริงว่าง ""
    // (throw ถ้า f ไม่ใช่ 1-1 เช่น a = 0)
    std::string inverseExpression(const std::string& expr, char var = '\0');
    // f^-1(y0) เชิงตัวเลข (throw ถ้า f ไม่เป็น 1-1 หรือ y0 ไม่อยู่ในเรนจ์)
    double inverseAt(const std::string& expr, double y0, char var = '\0');

    // ============ ส่วนที่ 4: สัญกรณ์ฟังก์ชัน f(อะไรก็ได้) ============
    // นิยาม: "f(x)=x^2+1", "g(t)=2t-3" หรือใส่แค่เนื้อฟังก์ชัน "x^2+1" (ถือว่าชื่อ f, ตัวแปรตรวจอัตโนมัติ)
    static FunctionDef parseDefinition(const std::string& definition);

    // แทนทุกการเรียก f(อาร์กิวเมนต์) ในนิพจน์ด้วยเนื้อฟังก์ชัน (อาร์กิวเมนต์เป็นนิพจน์อะไรก็ได้ ซ้อนกันได้
    // เช่น f(g(x+1)), มีตัวอักษรอื่นเป็นพารามิเตอร์ได้ เช่น f(a+h))
    std::string expandCalls(const std::vector<FunctionDef>& defs, const std::string& query);

    // ประเมินนิพจน์ที่มีการเรียกฟังก์ชัน เช่น query = "f(x+h) - f(x)" หรือ "(f(x+h)-f(x))/h"
    //  - กระจายและรวมพจน์ให้ (พหุนามหลายตัวแปรไม่เกิน 4 ตัว) เช่น x^2 + 2xh + h^2 + 1
    //  - values : ค่าของพารามิเตอร์/ตัวแปร ถ้ามี (ใช้ AlphaSolverEq::substituteVariables) เช่น {{'a',2}}
    //  - ถ้าไม่เหลือตัวแปรเลย จะได้ค่าตัวเลขใน value
    CallResult evaluateCall(const std::vector<std::string>& definitions, const std::string& query,
                            const std::map<char, double>& values = {});

    // แก้สมการที่มีการเรียกฟังก์ชัน เช่น "f(t+1) = 10", "f(x+2) = g(x)"
    //  - unknown = '\0' คือตรวจหาตัวแปรอัตโนมัติ (ใช้ AlphaSolverEq::detectVariable)
    //  - เป็นพหุนาม: ใช้ solvePolynomial / factorizePolynomial ของ AlphaSolverEq (ได้รากเชิงซ้อน + แยกตัวประกอบ)
    //  - ไม่ใช่พหุนาม: สแกนหารากจริงแล้วขัดเกลาด้วย solveGeneralEquation
    CallEquationResult solveCallEquation(const std::vector<std::string>& definitions, const std::string& equation,
                                         char unknown = '\0', const std::map<char, double>& values = {});

    // สรุปทุกอย่างเป็นข้อความ
    std::string describeFunction(const std::string& expr, char var = '\0');

private:
    Calculator& calc_;
    double window_    = 100.0;
    int    stepsPerUnit_ = 50;
    double relWindow_ = 20.0;

    // --- ตัวช่วยภายใน ---
    std::string prepare(const std::string& expr, char var);                 // ans + คูณโดยนัย + แปลงตัวแปรเป็น x
    bool evalAt(const std::string& f, double x, double& out);               // true ถ้าคำนวณได้และเป็นจำนวนจริงจำกัด
    void sampleBounds(const Interval& P, double& L, double& H) const;

    std::vector<Interval> domainPrepared(const std::string& f);
    std::vector<Interval> rangeOnPieces(const std::string& f, const std::vector<Interval>& pieces);
    std::vector<MonotonicPiece> monotonicOnPieces(const std::string& f, const std::vector<Interval>& pieces);
    bool oneToOneOnPieces(const std::string& f, const std::vector<Interval>& pieces);
    bool pieceRange(const std::string& f, const Interval& P, Interval& out);

    bool fitMultivariate(const std::string& expr, const std::vector<char>& vars,
                         std::vector<std::pair<std::vector<int>, double>>& terms);   // กระจายพหุนามหลายตัวแปร
    std::vector<double> rootsInY(const std::string& lhs, const std::string& rhs, double x0,
                                  double W = -1.0, int N = 1000, bool tangent = true);   // W<0 => relWindow_
    std::vector<Interval> relationDomain(const std::string& lhs, const std::string& rhs);
    std::string solveForYForm(const std::string& lhs, const std::string& rhs);   // พยายามแก้ lhs = rhs หา y (เมื่อเป็นพหุนามกำลังสองใน y)
};

#endif // FUNCTION_SOLVER_HPP