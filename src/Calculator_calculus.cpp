#include "../lib/Calculator_calculus.hpp"
#include <cmath>
#include <cctype>
#include <vector>
#include <limits>
#include <algorithm>
#include <utility>
#include <memory>
#include <sstream>
#include <iomanip>

Calculator::Calculator() : pos(0) {
    variables['e'] = M_E;
    variables['x'] = 0.0;
    variables['y'] = 0.0;
}

AlgebraCalculator& Calculator::algebra() {
    return alg;
}

AlphaSolverEq& Calculator::solver() {
    return eqSolver;
}

void Calculator::setVariable(char name, double value) {
    variables[name] = value;
}

// ---------- Public Entry Points ----------

double Calculator::evaluate(const std::string &expr) {
    src = expr;
    pos = 0;
    double result = parseExpression();
    skipWhitespace();
    if (!eof()) {
        throw std::runtime_error("Unexpected character at position " +
                                  std::to_string(pos) + ": '" + peek() + "'");
    }
    return result;
}

double Calculator::limit(const std::string &expr, double a, double h, char side) {
    if (side != '\0' && side != '+' && side != '-') {
        throw std::invalid_argument("side must be '+' (right-hand), '-' (left-hand), or '\\0' (two-sided).");
    }

    double savedX = variables['x'];

// 1. กรณีลิมิตเมื่อ x เข้าใกล้ +infinity หรือ -infinity
// (side ไม่มีผลในกรณีนี้ เพราะทิศทางการเข้าใกล้ +-infinity ถูกกำหนดโดยธรรมชาติอยู่แล้ว)
    if (std::isinf(a)) {
        bool isPosInf = (a > 0);
        auto evalAtX = [&](double x_val) {
            variables['x'] = x_val;
            try {
                return evaluate(expr);
            } catch (...) {
                variables['x'] = x_val * 1.1;
                return evaluate(expr);
            }
        };

        // สุ่มจุด x ที่มีขนาดใหญ่มาก 3 จุด
        double x1 = isPosInf ? 1e6 : -1e6;
        double x2 = isPosInf ? 1e9 : -1e9;
        double x3 = isPosInf ? 1e12 : -1e12;

        double v1 = evalAtX(x1);
        double v2 = evalAtX(x2);
        double v3 = evalAtX(x3);

        variables['x'] = savedX;

        if (std::isnan(v1) || std::isnan(v2) || std::isnan(v3)) {
            throw std::runtime_error("Limit does not exist (Undefined/NaN)");
        }

        // --- เช็คการพุ่งออกสู่ Infinity (Divergence Check) ---
        // 1.1 เจอค่า inf ตรงๆ จากระบบ
        if (std::isinf(v1) || std::isinf(v2) || std::isinf(v3)) {
            return (v3 > 0) ? std::numeric_limits<double>::infinity() 
                            : -std::numeric_limits<double>::infinity();
        }

        // 1.2 ค่า f(x) มีขนาดใหญ่มาก (เกิน 1e10) และเพิ่มขึ้นเรื่อยๆ ตาม x
        if (std::abs(v3) > 1e10 && std::abs(v3) > std::abs(v2) && std::abs(v2) > std::abs(v1)) {
            return (v3 > 0) ? std::numeric_limits<double>::infinity() 
                            : -std::numeric_limits<double>::infinity();
        }

        // --- เช็คการลู่เข้าหาค่าคงที่ (Convergence Check) ---
        double diff = std::abs(v2 - v3);
        double avg = (std::abs(v2) + std::abs(v3)) / 2.0;

        if (diff < 1e-3 || (avg > 1e-5 && (diff / avg) < 1e-3)) {
            return v3;
        } else {
            throw std::runtime_error("Limit does not converge (Oscillating or Undefined)");
        }
    }

    // กรณีที่ 2: ลิมิตเมื่อ x เข้าใกล้จุดคงที่ a
    // [แก้ไข] รองรับกรณีโดเมนของฟังก์ชันสิ้นสุดพอดีที่จุด a (เช่น sqrt(16-x^2) นิยามได้แค่ -4<=x<=4
    // ดังนั้นลิมิตเมื่อ x->4 ฝั่งขวา (x>4) จะติดลบในรูท/ไม่มีค่าจริง) เดิมโค้ดจะโยน exception จาก
    // evaluate() หลุดออกไปตรงๆ เพราะ retry ครั้งที่สองไม่ได้ครอบด้วย try-catch และยังคงอยู่ฝั่งเดิมที่
    // นอกโดเมนอยู่ดี ตอนนี้เปลี่ยนมาลองหดระยะเข้าใกล้ a ทีละขั้นในแต่ละฝั่งแยกกัน ถ้าฝั่งไหนนิยามไม่ได้
    // เลยจริงๆ (ไม่ใช่แค่ noise ชั่วคราว) จะไม่ throw แต่ถือว่าฝั่งนั้น "ไม่มีค่า" แล้วใช้ลิมิตด้านเดียว
    // จากฝั่งที่นิยามได้แทน (ตามหลักเดียวกับลิมิตที่ขอบเขตโดเมน เช่น lim x->0+ sqrt(x) = 0)

    auto tryEvalAt = [&](double x_val) -> std::pair<bool, double> {
        variables['x'] = x_val;
        try {
            double v = evaluate(expr);
            if (std::isnan(v)) return {false, 0.0};
            return {true, v};
        } catch (...) {
            return {false, 0.0};
        }
    };

    // เข้าใกล้ a จากด้านที่กำหนด (fromLeft) โดยหดระยะ step ลงเรื่อยๆ ถ้าจุดนั้นอยู่นอกโดเมน
    // คืนค่า {true, value} ถ้าหาค่าที่นิยามได้สำเร็จ (value อาจเป็น +-infinity ถ้าฟังก์ชันพุ่งออกแบบต่อเนื่อง),
    // หรือ {false, 0} ถ้านิยามไม่ได้เลยทุกระยะที่ลองในฝั่งนี้ (เช่น ln(x) เมื่อ x ติดลบตลอดทั้งฝั่ง)
    auto approachFrom = [&](bool fromLeft) -> std::pair<bool, double> {
        double step = h;
        bool found = false;

        // หาจุดแรกที่นิยามได้ (ลองหดระยะเข้าหา a ถ้าจุดเริ่มต้นดันอยู่นอกโดเมนพอดี)
        for (int attempt = 0; attempt < 10 && !found; ++attempt) {
            auto res = tryEvalAt(fromLeft ? (a - step) : (a + step));
            if (res.first) { found = true; }
            else step /= 10.0;
        }
        if (!found) return {false, 0.0};

        // เก็บตัวอย่างค่าเพิ่มอีกสูงสุด 4 ระดับ (หดระยะทีละ 10 เท่า) เพื่อดูแนวโน้ม:
        // ลู่เข้าค่าคงที่ (ratio ~1) หรือพุ่งออกสู่ infinity (ขนาดโตขึ้นต่อเนื่องทุกระดับ)
        std::vector<double> samples;
        {
            auto res0 = tryEvalAt(fromLeft ? (a - step) : (a + step));
            if (res0.first && !std::isnan(res0.second)) samples.push_back(res0.second);
        }
        for (int i = 0; i < 4 && !samples.empty(); ++i) {
            step /= 10.0;
            auto res = tryEvalAt(fromLeft ? (a - step) : (a + step));
            if (!res.first || std::isnan(res.second)) break;
            samples.push_back(res.second);
        }
        if (samples.empty()) return {false, 0.0};

        double last = samples.back();
        if (std::isinf(last)) return {true, last};

        // ถ้าขนาดค่าโตขึ้นต่อเนื่องอย่างน้อย 1.5 เท่าทุกครั้งที่หดระยะ (2 ขั้นหลังสุด) และมีเครื่องหมายเดิม
        // ให้ถือว่าฟังก์ชันกำลังพุ่งออกสู่ infinity (ไม่ใช่แค่ noise เชิงตัวเลข) แทนที่จะคืนเลขจำนวนมากที่ทำให้เข้าใจผิด
        if (samples.size() >= 3) {
            double v1 = samples[samples.size() - 3];
            double v2 = samples[samples.size() - 2];
            double v3 = samples[samples.size() - 1];
            bool sameSign = (v1 > 0) == (v2 > 0) && (v2 > 0) == (v3 > 0);
            bool sustainedGrowth = std::abs(v2) > std::abs(v1) * 1.5 && std::abs(v3) > std::abs(v2) * 1.5;
            if (sameSign && sustainedGrowth && std::abs(v3) > 10.0) {
                return {true, (v3 > 0) ? std::numeric_limits<double>::infinity()
                                        : -std::numeric_limits<double>::infinity()};
            }
        }
        return {true, last};
    };

    // --- ลิมิตด้านเดียว (One-sided limit): x -> a+ หรือ x -> a- ---
    // คำนวณเฉพาะฝั่งที่ถูกขอเท่านั้น (ไม่แตะอีกฝั่งเลย แม้อีกฝั่งจะอยู่นอกโดเมนก็ไม่กระทบ)
    if (side == '+') {
        auto [ok, val] = approachFrom(false); // false = เข้าใกล้ a จากทางขวา (x > a)
        variables['x'] = savedX;
        if (!ok) {
            throw std::runtime_error("Right-hand limit does not exist (function is undefined for x > " + std::to_string(a) + " near a = " + std::to_string(a) + ")");
        }
        return val;
    }
    if (side == '-') {
        auto [ok, val] = approachFrom(true); // true = เข้าใกล้ a จากทางซ้าย (x < a)
        variables['x'] = savedX;
        if (!ok) {
            throw std::runtime_error("Left-hand limit does not exist (function is undefined for x < " + std::to_string(a) + " near a = " + std::to_string(a) + ")");
        }
        return val;
    }

    // --- ลิมิตสองด้าน (Two-sided limit, ค่าเริ่มต้น) ---
    auto [leftOk, leftVal] = approachFrom(true);
    auto [rightOk, rightVal] = approachFrom(false);

    variables['x'] = savedX;

    if (!leftOk && !rightOk) {
        throw std::runtime_error("Limit does not exist (function is undefined on both sides of x = " + std::to_string(a) + ")");
    }

    // ฟังก์ชันนิยามได้แค่ด้านเดียวของ a (ขอบเขตโดเมน เช่น sqrt(16-x^2) ที่ x=4) -> ใช้ลิมิตด้านเดียวนั้น
    if (leftOk && !rightOk) return leftVal;
    if (!leftOk && rightOk) return rightVal;

    if (std::isinf(leftVal) || std::isinf(rightVal)) {
        if (std::isinf(leftVal) && std::isinf(rightVal) && (leftVal > 0) == (rightVal > 0)) {
            return leftVal; // ทั้งสองฝั่งพุ่งไปทิศเดียวกัน -> ลิมิตเป็น infinity นั้น
        }
        throw std::runtime_error("Limit does not exist (function diverges to infinity)");
    }

    double diff = std::abs(leftVal - rightVal);
    double avg = (std::abs(leftVal) + std::abs(rightVal)) / 2.0;

    if (diff < 1e-3 || (avg > 1e-5 && (diff / avg) < 1e-3)) {
        return (leftVal + rightVal) / 2.0;
    } else {
        throw std::runtime_error("Limit does not exist (Left limit != Right limit)");
    }
}

// ================= Symbolic Derivative (อนุพันธ์แบบสัญลักษณ์ ไม่แทนค่า x) =================
// สร้าง AST ของ expression แยกต่างหากจาก parser เชิงตัวเลขด้านบน (ที่ evaluate ค่าทันที) เพราะที่นี่
// ต้องการ "สูตร" ไม่ใช่ตัวเลข จึงต้องคงโครงสร้างนิพจน์ไว้เพื่อ apply กฎการหาอนุพันธ์ (chain/product/quotient/power rule)
namespace {

enum class NKind { Num, Var, Add, Sub, Mul, Div, Pow, Neg, Func, Percent };

struct Node {
    NKind kind;
    double num = 0.0;              // ใช้กับ Num
    char var = 0;                  // ใช้กับ Var
    std::shared_ptr<Node> a, b;    // ตัวถูกดำเนินการ (b ไม่ใช้กับ Neg/Percent/Func)
    std::string fname;             // ใช้กับ Func
    std::vector<std::shared_ptr<Node>> args; // ใช้กับ Func
};
using NodePtr = std::shared_ptr<Node>;

NodePtr mkNum(double v) { auto n = std::make_shared<Node>(); n->kind = NKind::Num; n->num = v; return n; }
NodePtr mkVar(char c)   { auto n = std::make_shared<Node>(); n->kind = NKind::Var; n->var = c; return n; }
NodePtr mkNeg(NodePtr a) { auto n = std::make_shared<Node>(); n->kind = NKind::Neg; n->a = a; return n; }
NodePtr mkPercent(NodePtr a) { auto n = std::make_shared<Node>(); n->kind = NKind::Percent; n->a = a; return n; }
NodePtr mkBin(NKind k, NodePtr a, NodePtr b) { auto n = std::make_shared<Node>(); n->kind = k; n->a = a; n->b = b; return n; }
NodePtr mkFunc(const std::string& name, std::vector<NodePtr> args) {
    auto n = std::make_shared<Node>(); n->kind = NKind::Func; n->fname = name; n->args = std::move(args); return n;
}

bool isZero(const NodePtr& n) { return n->kind == NKind::Num && n->num == 0.0; }
bool isOne(const NodePtr& n)  { return n->kind == NKind::Num && n->num == 1.0; }

// --- constructor ที่ลดรูปทันที (constant folding + 0/1 identities) เพื่อให้สูตรที่ได้อ่านง่ายขึ้น ---
NodePtr sAdd(NodePtr a, NodePtr b) {
    if (isZero(a)) return b;
    if (isZero(b)) return a;
    if (a->kind == NKind::Num && b->kind == NKind::Num) return mkNum(a->num + b->num);
    return mkBin(NKind::Add, a, b);
}
NodePtr sSub(NodePtr a, NodePtr b) {
    if (isZero(b)) return a;
    if (a->kind == NKind::Num && b->kind == NKind::Num) return mkNum(a->num - b->num);
    return mkBin(NKind::Sub, a, b);
}
NodePtr sMul(NodePtr a, NodePtr b) {
    if (isZero(a) || isZero(b)) return mkNum(0.0);
    if (isOne(a)) return b;
    if (isOne(b)) return a;
    if (a->kind == NKind::Num && b->kind == NKind::Num) return mkNum(a->num * b->num);
    return mkBin(NKind::Mul, a, b);
}
NodePtr sDiv(NodePtr a, NodePtr b) {
    if (isZero(a)) return mkNum(0.0);
    if (isOne(b)) return a;
    if (a->kind == NKind::Num && b->kind == NKind::Num && b->num != 0.0) return mkNum(a->num / b->num);
    return mkBin(NKind::Div, a, b);
}
NodePtr sNeg(NodePtr a) {
    if (a->kind == NKind::Num) return mkNum(-a->num);
    if (a->kind == NKind::Neg) return a->a; // -(-x) = x
    return mkNeg(a);
}
NodePtr sPow(NodePtr a, NodePtr b) {
    if (b->kind == NKind::Num) {
        if (b->num == 0.0) return mkNum(1.0);
        if (b->num == 1.0) return a;
    }
    if (a->kind == NKind::Num && b->kind == NKind::Num) return mkNum(std::pow(a->num, b->num));
    return mkBin(NKind::Pow, a, b);
}

// --- Parser: โครงสร้างเดียวกับ parser เชิงตัวเลขของ Calculator แต่สร้าง Node tree แทนการคำนวณค่า ---
class SymbolicParser {
public:
    explicit SymbolicParser(const std::string& s) : src(s), pos(0) {}

    NodePtr parse() {
        NodePtr r = parseExpression();
        skipWs();
        if (!eof()) throw std::runtime_error("Unexpected character while parsing for differentiation at position " + std::to_string(pos));
        return r;
    }

private:
    const std::string& src;
    size_t pos;

    void skipWs() { while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos; }
    bool eof() const { return pos >= src.size(); }
    char peek() const { return pos < src.size() ? src[pos] : '\0'; }
    char get() { return src[pos++]; }
    void expect(char c) {
        skipWs();
        if (eof() || peek() != c) throw std::runtime_error(std::string("Expected '") + c + "' while parsing for differentiation");
        ++pos;
    }

    NodePtr parseExpression() {
        NodePtr v = parseTerm();
        for (;;) {
            skipWs();
            if (peek() == '+') { get(); v = sAdd(v, parseTerm()); }
            else if (peek() == '-') { get(); v = sSub(v, parseTerm()); }
            else break;
        }
        return v;
    }
    NodePtr parseTerm() {
        NodePtr v = parsePower();
        for (;;) {
            skipWs();
            if (peek() == '*') { get(); v = sMul(v, parsePower()); }
            else if (peek() == '/') { get(); v = sDiv(v, parsePower()); }
            else if (peek() == '%') { get(); v = mkPercent(v); }
            else break;
        }
        return v;
    }
    NodePtr parsePower() {
        NodePtr base = parseUnary();
        skipWs();
        if (peek() == '^') { get(); NodePtr exp = parsePower(); return sPow(base, exp); }
        return base;
    }
    NodePtr parseUnary() {
        skipWs();
        if (peek() == '-') { get(); return sNeg(parseUnary()); }
        if (peek() == '+') { get(); return parseUnary(); }
        return parsePrimary();
    }
    NodePtr parsePrimary() {
        skipWs();
        if (eof()) throw std::runtime_error("Unexpected end of expression while parsing for differentiation");
        char c = peek();
        if (c == '(') { get(); NodePtr v = parseExpression(); expect(')'); return v; }
        if (c == '|') { get(); NodePtr v = parseExpression(); expect('|'); return mkFunc("abs", {v}); }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return parseNumber();
        if (std::isalpha(static_cast<unsigned char>(c))) {
            std::string name = parseIdentifier();
            skipWs();
            if (!eof() && peek() == '(') return parseFunctionCall(name);
            if (name == "pi") return mkNum(M_PI);
            if (name == "e")  return mkNum(M_E); // ให้ตรงกับ evaluate() ที่ตั้ง variables['e'] = M_E ไว้เป็นค่าคงที่
            if (name == "inf" || name == "infinity") throw std::runtime_error("infinity cannot appear inside a symbolic-derivative expression");
            if (name.size() == 1) return mkVar(name[0]);
            throw std::runtime_error("Unknown identifier: " + name);
        }
        throw std::runtime_error(std::string("Unexpected character: '") + c + "'");
    }
    NodePtr parseNumber() {
        size_t start = pos;
        while (!eof() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.')) ++pos;
        return mkNum(std::stod(src.substr(start, pos - start)));
    }
    std::string parseIdentifier() {
        std::string name;
        while (!eof() && std::isalpha(static_cast<unsigned char>(peek()))) name += get();
        return name;
    }
    NodePtr parseFunctionCall(const std::string& name) {
        expect('(');
        std::vector<NodePtr> args;
        args.push_back(parseExpression());
        skipWs();
        while (peek() == ',') { get(); args.push_back(parseExpression()); skipWs(); }
        expect(')');
        return mkFunc(name, args);
    }
};

// --- กฎการหาอนุพันธ์ (differentiation rules) เทียบตัวแปร var ---
NodePtr diffNode(const NodePtr& n, char var) {
    switch (n->kind) {
        case NKind::Num: return mkNum(0.0);
        case NKind::Var:  return mkNum(n->var == var ? 1.0 : 0.0); // ตัวแปรอื่นที่ไม่ใช่ var ถูกมองเป็นค่าคงที่
        case NKind::Add:  return sAdd(diffNode(n->a, var), diffNode(n->b, var));
        case NKind::Sub:  return sSub(diffNode(n->a, var), diffNode(n->b, var));
        case NKind::Neg:  return sNeg(diffNode(n->a, var));
        case NKind::Percent: return sDiv(diffNode(n->a, var), mkNum(100.0)); // u% = u/100
        case NKind::Mul:  return sAdd(sMul(diffNode(n->a, var), n->b), sMul(n->a, diffNode(n->b, var)));
        case NKind::Div: {
            NodePtr up = sSub(sMul(diffNode(n->a, var), n->b), sMul(n->a, diffNode(n->b, var)));
            NodePtr down = sPow(n->b, mkNum(2.0));
            return sDiv(up, down);
        }
        case NKind::Pow: {
            NodePtr u = n->a, v = n->b;
            NodePtr du = diffNode(u, var), dv = diffNode(v, var);
            bool vConst = isZero(dv), uConst = isZero(du);
            if (vConst && uConst) return mkNum(0.0);
            if (vConst) { // power rule: v * u^(v-1) * u'
                return sMul(sMul(v, sPow(u, sSub(v, mkNum(1.0)))), du);
            }
            if (uConst) { // exponential rule (ฐานคงที่): u^v * ln(u) * v'
                return sMul(sMul(n, mkFunc("ln", {u})), dv);
            }
            // กรณีทั่วไป (logarithmic differentiation): u^v * (v'*ln(u) + v*u'/u)
            return sMul(n, sAdd(sMul(dv, mkFunc("ln", {u})), sMul(v, sDiv(du, u))));
        }
        case NKind::Func: {
            const std::string& f = n->fname;

            // ถ้าทุก argument ไม่ขึ้นกับ var เลย -> ค่าทั้งก้อนเป็นค่าคงที่ อนุพันธ์ = 0 (ครอบคลุมรวมถึง fact(const))
            bool allArgsConst = true;
            for (const auto& ag : n->args) {
                if (!isZero(diffNode(ag, var))) { allArgsConst = false; break; }
            }
            if (allArgsConst) return mkNum(0.0);

            NodePtr u = n->args[0];
            NodePtr du = diffNode(u, var);

            if (f == "sin")  return sMul(mkFunc("cos", {u}), du);
            if (f == "cos")  return sNeg(sMul(mkFunc("sin", {u}), du));
            if (f == "tan")  return sDiv(du, sPow(mkFunc("cos", {u}), mkNum(2.0)));
            if (f == "asin") return sDiv(du, mkFunc("sqrt", {sSub(mkNum(1.0), sPow(u, mkNum(2.0)))}));
            if (f == "acos") return sNeg(sDiv(du, mkFunc("sqrt", {sSub(mkNum(1.0), sPow(u, mkNum(2.0)))})));
            if (f == "atan") return sDiv(du, sAdd(mkNum(1.0), sPow(u, mkNum(2.0))));
            if (f == "ln")   return sDiv(du, u);
            if (f == "exp")  return sMul(mkFunc("exp", {u}), du);
            if (f == "abs")  return sMul(sDiv(u, mkFunc("abs", {u})), du);
            if (f == "fact") throw std::runtime_error("Symbolic derivative of fact(x) is not supported (factorial has no elementary closed-form derivative).");

            if (f == "sqrt") {
                if (n->args.size() >= 2) {
                    NodePtr deg = n->args[1];
                    if (!isZero(diffNode(deg, var)))
                        throw std::runtime_error("Symbolic derivative of sqrt(x, n) with a non-constant degree n is not supported.");
                    NodePtr invDeg = sDiv(mkNum(1.0), deg);
                    return sMul(sMul(invDeg, sPow(u, sSub(invDeg, mkNum(1.0)))), du);
                }
                return sDiv(du, sMul(mkNum(2.0), mkFunc("sqrt", {u})));
            }
            if (f == "root") {
                if (n->args.size() != 2) throw std::runtime_error("root(x, n) needs two arguments");
                NodePtr deg = n->args[1];
                if (!isZero(diffNode(deg, var)))
                    throw std::runtime_error("Symbolic derivative of root(x, n) with a non-constant degree n is not supported.");
                NodePtr invDeg = sDiv(mkNum(1.0), deg);
                return sMul(sMul(invDeg, sPow(u, sSub(invDeg, mkNum(1.0)))), du);
            }
            if (f == "log") {
                if (n->args.size() == 1) return sDiv(du, sMul(u, mkFunc("ln", {mkNum(10.0)})));
                NodePtr base = n->args[1];
                if (!isZero(diffNode(base, var)))
                    throw std::runtime_error("Symbolic derivative of log(x, base) with a non-constant base is not supported.");
                return sDiv(du, sMul(u, mkFunc("ln", {base})));
            }

            throw std::runtime_error("Symbolic derivative of function '" + f + "' is not supported.");
        }
    }
    throw std::runtime_error("Internal error: unknown node kind during symbolic differentiation.");
}

// --- แปลง Node tree กลับเป็นสตริงสูตรที่ evaluate()/setVariable() เอาไปประเมินค่าต่อได้ ---
bool isAtomicNode(const NodePtr& n) { return n->kind == NKind::Num || n->kind == NKind::Var || n->kind == NKind::Func; }

bool g_fractionPrint = false;   // true ระหว่างพิมพ์ผลของ symbolicIntegral: แสดงจำนวนตรรกยะเป็นเศษส่วน

std::string numToString(double v) {
    if (std::abs(v - std::round(v)) < 1e-9 && std::abs(v) < 1e15) {
        return std::to_string(static_cast<long long>(std::llround(v)));
    }
    if (g_fractionPrint) {
        for (long long q = 2; q <= 100; ++q) {
            double t = v * static_cast<double>(q);
            if (std::abs(t - std::round(t)) < 1e-9 * (1.0 + std::abs(t))) {
                std::string frac = std::to_string(static_cast<long long>(std::llround(t))) + "/" + std::to_string(q);
                return (v < 0) ? frac : "(" + frac + ")";
            }
        }
    }
    std::ostringstream oss;
    oss << std::setprecision(12) << v;
    return oss.str();
}

std::string nodeToString(const NodePtr& n) {
    switch (n->kind) {
        case NKind::Num: {
            std::string s = numToString(n->num);
            return (n->num < 0) ? ("(" + s + ")") : s;
        }
        case NKind::Var: return std::string(1, n->var);
        case NKind::Neg: {
            std::string s = nodeToString(n->a);
            return isAtomicNode(n->a) ? ("-" + s) : ("-(" + s + ")");
        }
        case NKind::Percent: return "((" + nodeToString(n->a) + ")/100)";
        case NKind::Add: return "(" + nodeToString(n->a) + " + " + nodeToString(n->b) + ")";
        case NKind::Sub: return "(" + nodeToString(n->a) + " - " + nodeToString(n->b) + ")";
        case NKind::Mul: return "(" + nodeToString(n->a) + "*" + nodeToString(n->b) + ")";
        case NKind::Div: return "(" + nodeToString(n->a) + "/" + nodeToString(n->b) + ")";
        case NKind::Pow: return "(" + nodeToString(n->a) + "^" + nodeToString(n->b) + ")";
        case NKind::Func: {
            std::string s = n->fname + "(";
            for (size_t i = 0; i < n->args.size(); ++i) {
                if (i) s += ",";
                s += nodeToString(n->args[i]);
            }
            return s + ")";
        }
    }
    return "?";
}


// ================= Symbolic Indefinite Integral (ปริพันธ์ไม่จำกัดเขตแบบสัญลักษณ์) =================
// หา F(x) ที่ F'(x) = f(x) ในรูป "สูตร" โดยไม่แทนค่า x (คู่กับ symbolicDerivative) ใช้ Node tree ชุดเดียวกัน
// ครอบคลุม: พหุนาม (มีพารามิเตอร์ตัวอักษรอื่นได้), กฎกำลัง, ฟังก์ชันของอาร์กิวเมนต์เชิงเส้น f(ax+b),
//   sin/cos/tan/exp/ln/asin/acos/atan/abs, a^(kx+b), กำลังของ sin/cos/tan, ผลคูณ sin/cos (product-to-sum),
//   พหุนาม x (exp|sin|cos|a^x) และพหุนาม x ln(ax+b) (integration by parts แบบตาราง),
//   ฟังก์ชันตรรกยะ: P/(ax+b)^k, P/((a1x+b1)(a2x+b2)...) (partial fractions), P/(ax^2+bx+c) (ตัวเลข),
//   1/sqrt(ax^2+bx+c), และการแทนค่า (u-substitution) เช่น x*e^(x^2), cos(x)*e^(sin(x)), ln(x)/x, x/(x^2+1)
// ถ้าไม่พบรูปปิดแบบเบื้องต้น (หรือยังไม่รองรับ) จะ throw std::runtime_error แทนที่จะคืนค่าผิด
using Poly = std::vector<NodePtr>; // สัมประสิทธิ์เรียงจากกำลังต่ำไปสูง (index = เลขชี้กำลัง)

NodePtr mkLn(const NodePtr& u)  { return mkFunc("ln", {u}); }
NodePtr mkAbsN(const NodePtr& u) { return mkFunc("abs", {u}); }

bool dependsOn(const NodePtr& n, char var) {
    switch (n->kind) {
        case NKind::Num: return false;
        case NKind::Var: return n->var == var;
        case NKind::Neg: case NKind::Percent: return dependsOn(n->a, var);
        case NKind::Func:
            for (const auto& ag : n->args) if (dependsOn(ag, var)) return true;
            return false;
        default: return dependsOn(n->a, var) || dependsOn(n->b, var);
    }
}

bool containsNode(const NodePtr& n, const std::string& target) {
    if (nodeToString(n) == target) return true;
    switch (n->kind) {
        case NKind::Num: case NKind::Var: return false;
        case NKind::Neg: case NKind::Percent: return containsNode(n->a, target);
        case NKind::Func:
            for (const auto& ag : n->args) if (containsNode(ag, target)) return true;
            return false;
        default: return containsNode(n->a, target) || containsNode(n->b, target);
    }
}

NodePtr substNode(const NodePtr& n, const std::string& target, const NodePtr& repl) {
    if (nodeToString(n) == target) return repl;
    switch (n->kind) {
        case NKind::Num: case NKind::Var: return n;
        case NKind::Neg: return mkNeg(substNode(n->a, target, repl));
        case NKind::Percent: return mkPercent(substNode(n->a, target, repl));
        case NKind::Func: {
            std::vector<NodePtr> args;
            for (const auto& ag : n->args) args.push_back(substNode(ag, target, repl));
            return mkFunc(n->fname, args);
        }
        default: return mkBin(n->kind, substNode(n->a, target, repl), substNode(n->b, target, repl));
    }
}

// หมายเหตุ: ตัวเลขเก็บเป็น double และตอนพิมพ์ผลลัพธ์จะแสดงเศษส่วนสวย ๆ (เช่น 0.333.. -> (1/3)) จึงหารตรง ๆ ได้
NodePtr exactDiv(const NodePtr& a, const NodePtr& b) { return sDiv(a, b); }

// ---- พหุนามเชิงสัญลักษณ์ใน var (สัมประสิทธิ์เป็น Node ที่ไม่ขึ้นกับ var) ----
void polyTrim(Poly& p) {
    while (p.size() > 1 && isZero(p.back())) p.pop_back();
    if (p.empty()) p.push_back(mkNum(0.0));
}
Poly polyAddP(const Poly& a, const Poly& b) {
    Poly r(std::max(a.size(), b.size()), mkNum(0.0));
    for (size_t i = 0; i < a.size(); ++i) r[i] = sAdd(r[i], a[i]);
    for (size_t i = 0; i < b.size(); ++i) r[i] = sAdd(r[i], b[i]);
    polyTrim(r); return r;
}
Poly polyNegP(const Poly& a) { Poly r; for (const auto& c : a) r.push_back(sNeg(c)); return r; }
Poly polyMulP(const Poly& a, const Poly& b) {
    Poly r(a.size() + b.size() - 1, mkNum(0.0));
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = 0; j < b.size(); ++j) r[i + j] = sAdd(r[i + j], sMul(a[i], b[j]));
    polyTrim(r); return r;
}
Poly polyDerivP(const Poly& p) {
    Poly r;
    for (size_t i = 1; i < p.size(); ++i) r.push_back(sMul(mkNum(static_cast<double>(i)), p[i]));
    if (r.empty()) r.push_back(mkNum(0.0));
    return r;
}
NodePtr polyEvalP(const Poly& p, const NodePtr& x) {   // Horner
    NodePtr r = p.back();
    for (int i = static_cast<int>(p.size()) - 2; i >= 0; --i) r = sAdd(sMul(r, x), p[i]);
    return r;
}
NodePtr polyToNode(const Poly& p, char var) {
    NodePtr r = mkNum(0.0);
    for (size_t k = 0; k < p.size(); ++k) r = sAdd(r, sMul(p[k], sPow(mkVar(var), mkNum(static_cast<double>(k)))));
    return r;
}
// ∫ Σ c_k x^k dx = Σ (c_k/(k+1)) x^(k+1)
NodePtr polyIntegralNode(const Poly& p, char var) {
    NodePtr r = mkNum(0.0);
    for (size_t k = 0; k < p.size(); ++k) {
        if (isZero(p[k])) continue;
        NodePtr xp = sPow(mkVar(var), mkNum(static_cast<double>(k + 1)));
        r = sAdd(r, sMul(sDiv(p[k], mkNum(static_cast<double>(k + 1))), xp));
    }
    return r;
}

bool polyCoeffs(const NodePtr& n, char var, Poly& out) {
    if (!dependsOn(n, var)) { out = {n}; return true; }
    switch (n->kind) {
        case NKind::Var: out = {mkNum(0.0), mkNum(1.0)}; return true;
        case NKind::Neg: { Poly a; if (!polyCoeffs(n->a, var, a)) return false; out = polyNegP(a); return true; }
        case NKind::Add: case NKind::Sub: {
            Poly a, b;
            if (!polyCoeffs(n->a, var, a) || !polyCoeffs(n->b, var, b)) return false;
            out = polyAddP(a, n->kind == NKind::Add ? b : polyNegP(b)); return true;
        }
        case NKind::Mul: {
            Poly a, b;
            if (!polyCoeffs(n->a, var, a) || !polyCoeffs(n->b, var, b)) return false;
            out = polyMulP(a, b); return true;
        }
        case NKind::Div: {
            if (dependsOn(n->b, var)) return false;
            Poly a; if (!polyCoeffs(n->a, var, a)) return false;
            for (auto& c : a) c = sDiv(c, n->b);
            out = a; return true;
        }
        case NKind::Pow: {
            if (n->b->kind != NKind::Num) return false;
            double e = n->b->num;
            if (e < 0 || e > 30 || std::fabs(e - std::round(e)) > 1e-12) return false;
            Poly base; if (!polyCoeffs(n->a, var, base)) return false;
            Poly r = {mkNum(1.0)};
            for (int i = 0; i < static_cast<int>(std::round(e)); ++i) r = polyMulP(r, base);
            out = r; return true;
        }
        default: return false;
    }
}
bool linearForm(const NodePtr& u, char var, NodePtr& a, NodePtr& b) {
    Poly p; if (!polyCoeffs(u, var, p)) return false;
    polyTrim(p);
    if (p.size() != 2) return false;
    a = p[1]; b = p[0]; return true;
}

// ---- ทำให้อยู่ในรูปมาตรฐาน: log/sqrt/root/% -> ln/กำลัง ----
NodePtr normNode(const NodePtr& n) {
    switch (n->kind) {
        case NKind::Num: case NKind::Var: return n;
        case NKind::Neg: return sNeg(normNode(n->a));
        case NKind::Percent: return sDiv(normNode(n->a), mkNum(100.0));
        case NKind::Add: return sAdd(normNode(n->a), normNode(n->b));
        case NKind::Sub: return sSub(normNode(n->a), normNode(n->b));
        case NKind::Mul: return sMul(normNode(n->a), normNode(n->b));
        case NKind::Div: return sDiv(normNode(n->a), normNode(n->b));
        case NKind::Pow: return sPow(normNode(n->a), normNode(n->b));
        case NKind::Func: {
            std::vector<NodePtr> args;
            for (const auto& ag : n->args) args.push_back(normNode(ag));
            const std::string& f = n->fname;
            if (f == "log") {
                if (args.size() == 1) return sDiv(mkLn(args[0]), mkLn(mkNum(10.0)));
                return sDiv(mkLn(args[0]), mkLn(args[1]));
            }
            if (f == "sqrt") {
                if (args.size() == 1) return sPow(args[0], mkNum(0.5));
                return sPow(args[0], sDiv(mkNum(1.0), args[1]));
            }
            if (f == "root" && args.size() == 2) return sPow(args[0], sDiv(mkNum(1.0), args[1]));
            return mkFunc(f, args);
        }
    }
    return n;
}

// ---- ผลลัพธ์สวยขึ้น: x^0.5 -> sqrt(x) ----
NodePtr prettyNode(const NodePtr& n) {
    switch (n->kind) {
        case NKind::Num: case NKind::Var: return n;
        case NKind::Neg: return mkNeg(prettyNode(n->a));
        case NKind::Percent: return mkPercent(prettyNode(n->a));
        case NKind::Func: {
            std::vector<NodePtr> args;
            for (const auto& ag : n->args) args.push_back(prettyNode(ag));
            return mkFunc(n->fname, args);
        }
        case NKind::Pow:
            if (n->b->kind == NKind::Num && n->b->num == 0.5) return mkFunc("sqrt", {prettyNode(n->a)});
            return mkBin(NKind::Pow, prettyNode(n->a), prettyNode(n->b));
        default: {
            NodePtr a = prettyNode(n->a), b = prettyNode(n->b);
            if (n->kind == NKind::Mul) {
                if (b->kind == NKind::Neg) return sNeg(mkBin(NKind::Mul, a, b->a));
                if (a->kind == NKind::Neg) return sNeg(mkBin(NKind::Mul, a->a, b));
            } else if (n->kind == NKind::Div) {
                if (b->kind == NKind::Neg) return sNeg(mkBin(NKind::Div, a, b->a));
                if (a->kind == NKind::Neg) return sNeg(mkBin(NKind::Div, a->a, b));
            } else if (n->kind == NKind::Sub && b->kind == NKind::Neg) {
                return mkBin(NKind::Add, a, b->a);
            } else if (n->kind == NKind::Add && b->kind == NKind::Neg) {
                return mkBin(NKind::Sub, a, b->a);
            }
            return mkBin(n->kind, a, b);
        }
    }
}

// ---- แตกตัวประกอบของผลคูณ/ผลหาร ----
void collectFactors(const NodePtr& n, std::vector<NodePtr>& nums, std::vector<NodePtr>& dens, bool invert = false) {
    std::vector<NodePtr>& N = invert ? dens : nums;
    std::vector<NodePtr>& D = invert ? nums : dens;
    if (n->kind == NKind::Mul) { collectFactors(n->a, nums, dens, invert); collectFactors(n->b, nums, dens, invert); return; }
    if (n->kind == NKind::Div) { collectFactors(n->a, nums, dens, invert); collectFactors(n->b, nums, dens, !invert); return; }
    if (n->kind == NKind::Neg) { N.push_back(mkNum(-1.0)); collectFactors(n->a, nums, dens, invert); return; }
    if (n->kind == NKind::Pow && n->b->kind == NKind::Num && n->b->num < 0) {
        double m = -n->b->num;
        D.push_back(m == 1.0 ? n->a : sPow(n->a, mkNum(m)));
        return;
    }
    N.push_back(n);
}
NodePtr productOf(const std::vector<NodePtr>& f) {
    NodePtr r = mkNum(1.0);
    for (const auto& x : f) r = sMul(r, x);
    return r;
}
NodePtr rebuildFrac(const std::vector<NodePtr>& nums, const std::vector<NodePtr>& dens) {
    NodePtr r = productOf(nums);
    for (const auto& d : dens) r = sDiv(r, d);
    return r;
}

NodePtr integrateNode(const NodePtr& n, char var, int depth);

// ∫ sin^n, cos^n, tan^n ของ u = a*x+b (a = ตัวคูณของ x)
NodePtr trigPowerIntegral(const std::string& f, const NodePtr& u, const NodePtr& a, int n, char var) {
    if (n == 0) return mkVar(var);
    if (f == "sin") {
        if (n == 1) return sDiv(sNeg(mkFunc("cos", {u})), a);
        NodePtr first = sDiv(sNeg(sMul(sPow(mkFunc("sin", {u}), mkNum(n - 1.0)), mkFunc("cos", {u}))), sMul(mkNum(static_cast<double>(n)), a));
        return sAdd(first, sMul(exactDiv(mkNum(n - 1.0), mkNum(static_cast<double>(n))), trigPowerIntegral(f, u, a, n - 2, var)));
    }
    if (f == "cos") {
        if (n == 1) return sDiv(mkFunc("sin", {u}), a);
        NodePtr first = sDiv(sMul(mkFunc("sin", {u}), sPow(mkFunc("cos", {u}), mkNum(n - 1.0))), sMul(mkNum(static_cast<double>(n)), a));
        return sAdd(first, sMul(exactDiv(mkNum(n - 1.0), mkNum(static_cast<double>(n))), trigPowerIntegral(f, u, a, n - 2, var)));
    }
    // tan
    if (n == 1) return sDiv(sNeg(mkLn(mkAbsN(mkFunc("cos", {u})))), a);
    NodePtr first = sDiv(sPow(mkFunc("tan", {u}), mkNum(n - 1.0)), sMul(mkNum(n - 1.0), a));
    return sSub(first, trigPowerIntegral(f, u, a, n - 2, var));
}

// ∫ ของ "ตัวประกอบเดี่ยว" ที่ขึ้นกับ var (ฟังก์ชัน/กำลัง ของอาร์กิวเมนต์เชิงเส้น ฯลฯ)
NodePtr integrateBasic(const NodePtr& f, char var) {
    NodePtr a, b;
    if (f->kind == NKind::Var && f->var == var) return sDiv(sPow(f, mkNum(2.0)), mkNum(2.0));

    if (f->kind == NKind::Func && f->args.size() == 1) {
        const NodePtr& u = f->args[0];
        if (!linearForm(u, var, a, b) || isZero(a)) return nullptr;
        const std::string& fn = f->fname;
        NodePtr F;
        if (fn == "sin") F = sNeg(mkFunc("cos", {u}));
        else if (fn == "cos") F = mkFunc("sin", {u});
        else if (fn == "tan") F = sNeg(mkLn(mkAbsN(mkFunc("cos", {u}))));
        else if (fn == "exp") F = mkFunc("exp", {u});
        else if (fn == "ln") F = sSub(sMul(u, mkLn(u)), u);
        else if (fn == "asin") F = sAdd(sMul(u, mkFunc("asin", {u})), mkFunc("sqrt", {sSub(mkNum(1.0), sPow(u, mkNum(2.0)))}));
        else if (fn == "acos") F = sSub(sMul(u, mkFunc("acos", {u})), mkFunc("sqrt", {sSub(mkNum(1.0), sPow(u, mkNum(2.0)))}));
        else if (fn == "atan") F = sSub(sMul(u, mkFunc("atan", {u})), sDiv(mkLn(sAdd(mkNum(1.0), sPow(u, mkNum(2.0)))), mkNum(2.0)));
        else if (fn == "abs") F = sDiv(sMul(u, mkAbsN(u)), mkNum(2.0));
        else return nullptr;
        return sDiv(F, a);
    }

    if (f->kind == NKind::Pow) {
        const NodePtr& base = f->a;
        const NodePtr& ex = f->b;
        // (ax+b)^p
        if (!dependsOn(ex, var) && linearForm(base, var, a, b) && !isZero(a)) {
            if (ex->kind == NKind::Num && ex->num == -1.0) return sDiv(mkLn(mkAbsN(base)), a);
            NodePtr p1 = sAdd(ex, mkNum(1.0));
            return sMul(sDiv(mkNum(1.0), sMul(a, p1)), sPow(base, p1));
        }
        // c^(ax+b)
        if (!dependsOn(base, var) && linearForm(ex, var, a, b) && !isZero(a)) {
            if (base->kind == NKind::Num && std::fabs(base->num - M_E) < 1e-12) return sDiv(mkFunc("exp", {ex}), a);
            return sDiv(f, sMul(a, mkLn(base)));
        }
        // sin^n, cos^n, tan^n (n จำนวนเต็ม >= 2)
        if (base->kind == NKind::Func && base->args.size() == 1 && ex->kind == NKind::Num &&
            (base->fname == "sin" || base->fname == "cos" || base->fname == "tan") &&
            std::fabs(ex->num - std::round(ex->num)) < 1e-12 && ex->num >= 2 && ex->num <= 12 &&
            linearForm(base->args[0], var, a, b) && !isZero(a)) {
            return trigPowerIntegral(base->fname, base->args[0], a, static_cast<int>(std::round(ex->num)), var);
        }
        // (ax^2+bx+c)^(-1/2)  (สัมประสิทธิ์เป็นตัวเลข)
        if (ex->kind == NKind::Num && ex->num == -0.5) {
            Poly q; 
            if (polyCoeffs(base, var, q) && (polyTrim(q), q.size() == 3) &&
                q[0]->kind == NKind::Num && q[1]->kind == NKind::Num && q[2]->kind == NKind::Num) {
                double c = q[0]->num, bb = q[1]->num, aa = q[2]->num;
                if (aa > 0) {
                    NodePtr sa = mkNum(std::sqrt(aa));
                    NodePtr inner = sAdd(sMul(sMul(mkNum(2.0), sa), mkFunc("sqrt", {base})), sAdd(sMul(mkNum(2.0 * aa), mkVar(var)), mkNum(bb)));
                    return sDiv(mkLn(mkAbsN(inner)), sa);
                } else if (aa < 0) {
                    double D = bb * bb - 4 * aa * c;
                    if (D > 0) {
                        double sD = std::sqrt(D);
                        NodePtr arg = polyToNode(Poly{mkNum(-bb / sD), mkNum(-2.0 * aa / sD)}, var);
                        return sDiv(mkFunc("asin", {arg}), mkNum(std::sqrt(-aa)));
                    }
                }
            }
        }
    }
    return nullptr;
}

// ∫ P(x)/(x - r) ... : ใช้ใน P ln(u) -> หา ∫ Q(x)/(x-r) dx เมื่อ u = a(x - r)  (คืน poly part + Q(r) ln|u|)
NodePtr integrateOverLinear(const Poly& Q, const NodePtr& r, const NodePtr& u, char var) {
    // หาร Q ด้วย (x - r) แบบ synthetic division (Q เรียงต่ำ->สูง)
    int n = static_cast<int>(Q.size()) - 1;
    Poly S(std::max(n, 0) + (n >= 1 ? 0 : 1), mkNum(0.0));
    NodePtr carry = mkNum(0.0);
    std::vector<NodePtr> q(n + 1, mkNum(0.0));
    for (int i = n; i >= 0; --i) {
        carry = sAdd(Q[i], sMul(carry, r));
        q[i] = carry;
    }
    NodePtr remainder = q[0];
    Poly quot;
    for (int i = 1; i <= n; ++i) quot.push_back(q[i]);
    NodePtr res = quot.empty() ? mkNum(0.0) : polyIntegralNode(quot, var);
    return sAdd(res, sMul(remainder, mkLn(mkAbsN(u))));
}

// หารพหุนาม P ด้วย D (สัมประสิทธิ์นำของ D หารได้): คืน (S, R)
void polyDivMod(const Poly& P, const Poly& D, Poly& S, Poly& R) {
    R = P;
    int dn = static_cast<int>(D.size()) - 1;
    int rn = static_cast<int>(R.size()) - 1;
    if (rn < dn) { S = {mkNum(0.0)}; polyTrim(R); return; }
    S.assign(rn - dn + 1, mkNum(0.0));
    for (int i = rn - dn; i >= 0; --i) {
        NodePtr coef = exactDiv(R[i + dn], D[dn]);
        S[i] = coef;
        for (int j = 0; j <= dn; ++j) R[i + j] = sSub(R[i + j], sMul(coef, D[j]));
    }
    R.resize(dn > 0 ? dn : 1);
    polyTrim(R);
}

// ∫ P(x)/(ax+b)^k dx (k >= 1) ด้วยการกระจายรอบ r = -b/a
NodePtr shiftMethodIntegral(const Poly& P, const NodePtr& a, const NodePtr& b, int k, char var) {
    NodePtr r = exactDiv(sNeg(b), a);
    NodePtr u = sAdd(sMul(a, mkVar(var)), b);
    if (k == 1) {                                         // P/(ax+b) = S(x) + R/(ax+b)
        Poly S, R; polyDivMod(P, Poly{b, a}, S, R);
        NodePtr res = (S.size() == 1 && isZero(S[0])) ? mkNum(0.0) : polyIntegralNode(S, var);
        if (!isZero(R[0])) res = sAdd(res, sMul(sDiv(R[0], a), mkLn(mkAbsN(u))));
        return res;
    }
    // Taylor shift: P(x) = Σ c_j (x-r)^j  ด้วย synthetic division ซ้ำ
    Poly cur = P;
    std::vector<NodePtr> cj;
    while (!cur.empty()) {
        int n = static_cast<int>(cur.size()) - 1;
        std::vector<NodePtr> q(n + 1, mkNum(0.0));
        NodePtr carry = mkNum(0.0);
        for (int i = n; i >= 0; --i) { carry = sAdd(cur[i], sMul(carry, r)); q[i] = carry; }
        cj.push_back(q[0]);
        Poly next;
        for (int i = 1; i <= n; ++i) next.push_back(q[i]);
        cur = next;
    }
    NodePtr x_r = (a->kind == NKind::Num && a->num == 1.0) ? sAdd(mkVar(var), b)      // x - r = x + b เมื่อ a = 1
                                                           : sSub(mkVar(var), r);
    NodePtr sum = mkNum(0.0);
    for (size_t j = 0; j < cj.size(); ++j) {
        if (isZero(cj[j])) continue;
        int e = static_cast<int>(j) - k;
        if (e == -1) sum = sAdd(sum, sMul(cj[j], mkLn(mkAbsN(u))));
        else sum = sAdd(sum, sDiv(sMul(cj[j], sPow(x_r, mkNum(e + 1.0))), mkNum(e + 1.0)));
    }
    return sDiv(sum, sPow(a, mkNum(static_cast<double>(k))));
}

bool allNum(const Poly& p) { for (const auto& c : p) if (c->kind != NKind::Num) return false; return true; }

// partial fractions: P / Π (a_i x + b_i)  (ตัวประกอบเชิงเส้นกำลัง 1 ที่รากต่างกัน)
NodePtr partialFractionsIntegral(const Poly& P, const std::vector<std::pair<NodePtr, NodePtr>>& lin, char var) {
    Poly D = {mkNum(1.0)};
    std::vector<NodePtr> roots;
    for (const auto& ab : lin) {
        if (isZero(ab.first)) return nullptr;
        roots.push_back(exactDiv(sNeg(ab.second), ab.first));
        D = polyMulP(D, Poly{ab.second, ab.first});
    }
    Poly S, R; polyDivMod(P, D, S, R);
    Poly Dp = polyDerivP(D);
    NodePtr res = (S.size() == 1 && isZero(S[0])) ? mkNum(0.0) : polyIntegralNode(S, var);
    for (size_t i = 0; i < roots.size(); ++i) {
        NodePtr den = polyEvalP(Dp, roots[i]);
        if (den->kind == NKind::Num && std::fabs(den->num) < 1e-12) return nullptr;      // รากซ้ำ
        NodePtr Ai = exactDiv(polyEvalP(R, roots[i]), den);
        if (isZero(Ai)) continue;
        NodePtr ui = sAdd(sMul(lin[i].first, mkVar(var)), lin[i].second);
        res = sAdd(res, sMul(Ai, mkLn(mkAbsN(ui))));
    }
    return res;
}

// ฟังก์ชันตรรกยะ: varNums (พหุนามทุกตัว) / varDens
NodePtr rationalIntegral(const std::vector<NodePtr>& varNums, const std::vector<NodePtr>& varDens, char var) {
    Poly P;
    if (!polyCoeffs(productOf(varNums), var, P)) return nullptr;
    polyTrim(P);

    struct DF { NodePtr base; int k; };
    std::vector<DF> dfs;
    for (const auto& d : varDens) {
        if (d->kind == NKind::Pow && d->b->kind == NKind::Num && d->b->num >= 1 &&
            std::fabs(d->b->num - std::round(d->b->num)) < 1e-12)
            dfs.push_back({d->a, static_cast<int>(std::round(d->b->num))});
        else dfs.push_back({d, 1});
    }

    if (dfs.size() == 1) {
        Poly B; if (!polyCoeffs(dfs[0].base, var, B)) return nullptr;
        polyTrim(B);
        int deg = static_cast<int>(B.size()) - 1;
        if (deg == 1) return shiftMethodIntegral(P, B[1], B[0], dfs[0].k, var);
        if (deg == 2 && dfs[0].k == 1 && allNum(B)) {
            double c = B[0]->num, bb = B[1]->num, aa = B[2]->num;
            double D = bb * bb - 4 * aa * c;
            if (D > 1e-12) {                                   // รากจริง 2 ค่า: ถ้าเป็นเศษส่วนสวย ใช้ partial fractions
                double s = std::sqrt(D), r1 = (-bb + s) / (2 * aa), r2 = (-bb - s) / (2 * aa);
                bool nice = false;
                for (int m = 1; m <= 12 && !nice; ++m)
                    nice = std::fabs(r1 * m - std::round(r1 * m)) < 1e-9 && std::fabs(r2 * m - std::round(r2 * m)) < 1e-9;
                if (nice) {
                    Poly Ps = P; for (auto& cf : Ps) cf = sDiv(cf, mkNum(aa));
                    return partialFractionsIntegral(Ps, {{mkNum(1.0), mkNum(-r1)}, {mkNum(1.0), mkNum(-r2)}}, var);
                }
            }
            Poly S, R; polyDivMod(P, B, S, R);
            while (R.size() < 2) R.push_back(mkNum(0.0));
            NodePtr p = R[1], q = R[0];
            NodePtr Q = polyToNode(B, var);
            NodePtr lin = sAdd(sMul(mkNum(2.0 * aa), mkVar(var)), mkNum(bb));      // 2ax + b
            NodePtr I;
            if (D < -1e-12) {
                double s = std::sqrt(-D);
                I = sMul(mkNum(2.0 / s), mkFunc("atan", {polyToNode(Poly{mkNum(bb / s), mkNum(2.0 * aa / s)}, var)}));
            } else if (D > 1e-12) {
                NodePtr s = mkNum(std::sqrt(D));
                I = sMul(sDiv(mkNum(1.0), s), mkLn(mkAbsN(sDiv(sSub(lin, s), sAdd(lin, s)))));
            } else {
                I = sDiv(mkNum(-2.0), lin);
            }
            NodePtr res = isZero(S[0]) && S.size() == 1 ? mkNum(0.0) : polyIntegralNode(S, var);
            NodePtr twoA = mkNum(2.0 * aa);
            if (!isZero(p)) res = sAdd(res, sMul(sDiv(p, twoA), mkLn(mkAbsN(Q))));
            NodePtr rest = sSub(q, sDiv(sMul(p, mkNum(bb)), twoA));
            if (!isZero(rest)) res = sAdd(res, sMul(rest, I));
            return res;
        }
        return nullptr;
    }

    // ตัวส่วนหลายตัวประกอบเชิงเส้น (กำลัง 1 ทั้งหมด) -> partial fractions
    std::vector<std::pair<NodePtr, NodePtr>> lin;
    for (const auto& df : dfs) {
        NodePtr a, b;
        if (df.k != 1 || !linearForm(df.base, var, a, b) || isZero(a)) return nullptr;
        lin.push_back({a, b});
    }
    return partialFractionsIntegral(P, lin, var);
}

// พหุนาม × (exp | sin | cos | c^x | ln) ของอาร์กิวเมนต์เชิงเส้น : integration by parts แบบตาราง
NodePtr polyTimesTranscendental(const Poly& P, const NodePtr& T, char var) {
    NodePtr a, b;
    if (T->kind == NKind::Func && T->args.size() == 1 && linearForm(T->args[0], var, a, b) && !isZero(a)) {
        const NodePtr& u = T->args[0];
        const std::string& f = T->fname;
        if (f == "ln") {                                              // ∫ P ln(u) = Q ln u - ∫ Q a/u
            Poly Q; Poly integ = P;
            Q.assign(P.size() + 1, mkNum(0.0));
            for (size_t k = 0; k < P.size(); ++k) Q[k + 1] = exactDiv(P[k], mkNum(static_cast<double>(k + 1)));
            polyTrim(Q);
            NodePtr r = exactDiv(sNeg(b), a);
            NodePtr rem = integrateOverLinear(Q, r, u, var);
            return sSub(sMul(polyToNode(Q, var), mkLn(u)), rem);
        }
        if (f != "exp" && f != "sin" && f != "cos") return nullptr;
        NodePtr result = mkNum(0.0);
        Poly cur = P;
        for (size_t k = 0; k < P.size(); ++k) {
            NodePtr G;                                                // G_{k+1} : (k+1) ครั้งของปริพันธ์ g(u)
            int m = static_cast<int>(k) + 1;
            NodePtr ak = sPow(a, mkNum(static_cast<double>(m)));
            if (f == "exp") G = sDiv(mkFunc("exp", {u}), ak);
            else {
                static const char* sinCycle[] = {"-cos", "-sin", "+cos", "+sin"};
                static const char* cosCycle[] = {"+sin", "-cos", "-sin", "+cos"};
                const char* c = (f == "sin" ? sinCycle : cosCycle)[(m - 1) % 4];
                NodePtr fn = mkFunc(std::string(c + 1), {u});
                G = sDiv(c[0] == '-' ? sNeg(fn) : fn, ak);
            }
            NodePtr term = sMul(polyToNode(cur, var), G);
            result = (k % 2 == 0) ? sAdd(result, term) : sSub(result, term);
            cur = polyDerivP(cur);
        }
        return result;
    }
    if (T->kind == NKind::Pow && !dependsOn(T->a, var) && linearForm(T->b, var, a, b) && !isZero(a)) {   // c^(ax+b)
        bool isE = T->a->kind == NKind::Num && std::fabs(T->a->num - M_E) < 1e-12;
        NodePtr lam = isE ? a : sMul(a, mkLn(T->a));
        NodePtr E = isE ? mkFunc("exp", {T->b}) : T;
        NodePtr result = mkNum(0.0);
        Poly cur = P;
        for (size_t k = 0; k < P.size(); ++k) {
            NodePtr G = sDiv(E, sPow(lam, mkNum(static_cast<double>(k + 1))));
            NodePtr term = sMul(polyToNode(cur, var), G);
            result = (k % 2 == 0) ? sAdd(result, term) : sSub(result, term);
            cur = polyDerivP(cur);
        }
        return result;
    }
    return nullptr;
}

void collectCandidates(const NodePtr& n, char var, std::vector<NodePtr>& out) {
    if (!dependsOn(n, var)) return;
    auto add = [&](const NodePtr& g) {
        if (!dependsOn(g, var) || (g->kind == NKind::Var)) return;
        std::string s = nodeToString(g);
        for (const auto& o : out) if (nodeToString(o) == s) return;
        out.push_back(g);
    };
    switch (n->kind) {
        case NKind::Num: case NKind::Var: return;
        case NKind::Neg: case NKind::Percent: collectCandidates(n->a, var, out); return;
        case NKind::Func:
            add(n);
            for (const auto& ag : n->args) { add(ag); collectCandidates(ag, var, out); }
            return;
        case NKind::Pow:
            add(n); add(n->a); add(n->b);
            collectCandidates(n->a, var, out); collectCandidates(n->b, var, out);
            return;
        default:
            collectCandidates(n->a, var, out); collectCandidates(n->b, var, out);
    }
}

// u-substitution: integrand = C * g'(x) * H(g(x))
NodePtr substitutionIntegral(const std::vector<NodePtr>& varNums, const std::vector<NodePtr>& varDens, char var, int depth) {
    std::vector<NodePtr> cands;
    for (const auto& f : varNums) collectCandidates(f, var, cands);
    for (const auto& f : varDens) {
        if (f->kind != NKind::Var) {
            bool dup = false; std::string s = nodeToString(f);
            for (const auto& o : cands) if (nodeToString(o) == s) dup = true;
            if (!dup && dependsOn(f, var)) cands.push_back(f);
        }
        collectCandidates(f, var, cands);
    }
    const char U = '\x01';
    for (const auto& g : cands) {
        std::string gs = nodeToString(g);
        std::vector<NodePtr> oN, oD, rN, rD;
        for (const auto& f : varNums) (containsNode(f, gs) ? oN : rN).push_back(f);
        for (const auto& f : varDens) (containsNode(f, gs) ? oD : rD).push_back(f);
        if (oN.empty() && oD.empty()) continue;

        NodePtr dgNode = diffNode(g, var);
        if (isZero(dgNode)) continue;
        std::vector<NodePtr> dgN, dgD;
        collectFactors(dgNode, dgN, dgD);

        // วิธี B: ดึงตัวประกอบของ g' ออกจากตัวประกอบทั้งหมดโดยตรง (ครอบคลุมเช่น exp(x)*sin(exp(x)))
        {
            std::vector<NodePtr> n2 = varNums, d2 = varDens;
            double cNum = 1.0; bool okB = true;
            for (const auto& f : dgN) {
                if (f->kind == NKind::Num) { cNum *= f->num; continue; }
                std::string s = nodeToString(f); bool found = false;
                for (size_t i = 0; i < n2.size(); ++i) if (nodeToString(n2[i]) == s) { n2.erase(n2.begin() + i); found = true; break; }
                if (!found) { okB = false; break; }
            }
            for (const auto& f : dgD) {
                if (!okB) break;
                if (f->kind == NKind::Num) { cNum /= f->num; continue; }
                std::string s = nodeToString(f); bool found = false;
                for (size_t i = 0; i < d2.size(); ++i) if (nodeToString(d2[i]) == s) { d2.erase(d2.begin() + i); found = true; break; }
                if (!found) okB = false;
            }
            if (okB && cNum != 0.0) {
                NodePtr HB = substNode(rebuildFrac(n2, d2), gs, mkVar(U));
                if (!dependsOn(HB, var)) {
                    NodePtr rB = integrateNode(HB, U, depth + 1);
                    if (rB) return sMul(mkNum(1.0 / cNum), substNode(rB, nodeToString(mkVar(U)), g));
                }
            }
        }

        // วิธี A: C = rest / g'  : ตัดตัวประกอบที่เหมือนกันทางโครงสร้าง
        std::vector<NodePtr> cn = rN, cd = rD;
        cn.insert(cn.end(), dgD.begin(), dgD.end());
        cd.insert(cd.end(), dgN.begin(), dgN.end());
        for (size_t i = 0; i < cn.size(); ++i) {
            if (cn[i]->kind == NKind::Num) continue;
            std::string si = nodeToString(cn[i]);
            for (size_t j = 0; j < cd.size(); ++j) {
                if (cd[j]->kind != NKind::Num && nodeToString(cd[j]) == si) {
                    cn.erase(cn.begin() + i); cd.erase(cd.begin() + j); --i; break;
                }
            }
        }
        bool constant = true;
        for (const auto& f : cn) if (dependsOn(f, var)) constant = false;
        for (const auto& f : cd) if (dependsOn(f, var)) constant = false;
        if (!constant) continue;
        NodePtr C = exactDiv(productOf(cn), productOf(cd));

        NodePtr H = rebuildFrac(oN, oD);
        H = substNode(H, gs, mkVar(U));
        if (dependsOn(H, var)) continue;
        NodePtr r = integrateNode(H, U, depth + 1);
        if (!r) continue;
        return sMul(C, substNode(r, nodeToString(mkVar(U)), g));
    }
    return nullptr;
}

NodePtr integrateNode(const NodePtr& n, char var, int depth) {
    if (depth > 10) return nullptr;
    if (!dependsOn(n, var)) return sMul(n, mkVar(var));

    Poly poly;
    if (polyCoeffs(n, var, poly)) { polyTrim(poly); return polyIntegralNode(poly, var); }

    // ผลบวก/ผลต่าง (ทีละพจน์)
    if (n->kind == NKind::Add || n->kind == NKind::Sub) {
        NodePtr l = integrateNode(n->a, var, depth + 1);
        NodePtr r = l ? integrateNode(n->b, var, depth + 1) : nullptr;
        if (l && r) return n->kind == NKind::Add ? sAdd(l, r) : sSub(l, r);
    }
    if (n->kind == NKind::Neg) {
        NodePtr r = integrateNode(n->a, var, depth + 1);
        if (r) return sNeg(r);
    }

    // แยกตัวประกอบ: ดึงค่าคงที่ออก
    std::vector<NodePtr> nums, dens, vn, vd, cn, cd;
    collectFactors(n, nums, dens);
    for (const auto& f : nums) (dependsOn(f, var) ? vn : cn).push_back(f);
    for (const auto& f : dens) (dependsOn(f, var) ? vd : cd).push_back(f);
    if (!cn.empty() || !cd.empty()) {
        NodePtr C = rebuildFrac(cn, cd);
        if (!(C->kind == NKind::Num && C->num == 1.0)) {
            NodePtr r = integrateNode(rebuildFrac(vn, vd), var, depth + 1);
            if (r) return sMul(C, r);
            return nullptr;
        }
    }

    // รวมกำลังของฐานเดียวกัน เช่น x * x^0.5 -> x^1.5, (x+1)^2/(x+1) -> (x+1)
    {
        struct PF { NodePtr base; double e; };
        std::vector<PF> pf; std::vector<NodePtr> others; bool merged = false;
        auto addFactor = [&](const NodePtr& f, double sign) {
            NodePtr base = f; double ex = 1.0;
            if (f->kind == NKind::Pow && f->b->kind == NKind::Num) { base = f->a; ex = f->b->num; }
            std::string s = nodeToString(base);
            for (auto& q : pf) if (nodeToString(q.base) == s) { q.e += sign * ex; merged = true; return; }
            pf.push_back({base, sign * ex});
        };
        for (const auto& f : vn) addFactor(f, 1.0);
        for (const auto& f : vd) addFactor(f, -1.0);
        if (merged) {
            std::vector<NodePtr> nn, dd;
            for (const auto& q : pf) {
                if (std::fabs(q.e) < 1e-12) continue;
                if (q.e > 0) nn.push_back(sPow(q.base, mkNum(q.e)));
                else dd.push_back(q.e == -1.0 ? q.base : sPow(q.base, mkNum(-q.e)));
            }
            NodePtr r = integrateNode(rebuildFrac(nn, dd), var, depth + 1);
            if (r) return r;
        }
    }

    // ตัวประกอบเดี่ยว
    if (vn.size() == 1 && vd.empty()) { NodePtr r = integrateBasic(vn[0], var); if (r) return r; }
    if (vn.empty() && vd.size() == 1 && vd[0]->kind == NKind::Pow && vd[0]->b->kind == NKind::Num) {   // 1/(...)^p เช่น 1/sqrt(Q)
        NodePtr r = integrateBasic(sPow(vd[0]->a, mkNum(-vd[0]->b->num)), var);
        if (r) return r;
    }

    // sin/cos × sin/cos (product-to-sum)
    if (vd.empty() && vn.size() == 2 && vn[0]->kind == NKind::Func && vn[1]->kind == NKind::Func &&
        vn[0]->args.size() == 1 && vn[1]->args.size() == 1 &&
        (vn[0]->fname == "sin" || vn[0]->fname == "cos") && (vn[1]->fname == "sin" || vn[1]->fname == "cos")) {
        NodePtr A = vn[0]->args[0], B = vn[1]->args[0];
        auto canon = [&](const NodePtr& t) { Poly pp; if (polyCoeffs(t, var, pp)) { polyTrim(pp); return polyToNode(pp, var); } return t; };
        auto trig = [&](const std::string& nm, const NodePtr& arg) -> NodePtr {      // sin/cos ของค่าคงที่ -> ตัวเลข
            if (arg->kind == NKind::Num) return mkNum(nm == "sin" ? std::sin(arg->num) : std::cos(arg->num));
            return mkFunc(nm, {arg});
        };
        NodePtr sum = canon(sAdd(A, B)), diff = canon(sSub(A, B)), half = mkNum(0.5);
        NodePtr rewritten;
        const std::string &f1 = vn[0]->fname, &f2 = vn[1]->fname;
        if (f1 == "sin" && f2 == "cos")      rewritten = sMul(half, sAdd(trig("sin", sum), trig("sin", diff)));
        else if (f1 == "cos" && f2 == "sin") rewritten = sMul(half, sSub(trig("sin", sum), trig("sin", diff)));
        else if (f1 == "sin")                rewritten = sMul(half, sSub(trig("cos", diff), trig("cos", sum)));
        else                                 rewritten = sMul(half, sAdd(trig("cos", diff), trig("cos", sum)));
        NodePtr r = integrateNode(rewritten, var, depth + 1);
        if (r) return r;
    }

    // พหุนาม × ฟังก์ชันอดิศัย
    if (vd.empty()) {
        std::vector<NodePtr> polyF; NodePtr T; int tc = 0;
        for (const auto& f : vn) {
            Poly pp;
            if (polyCoeffs(f, var, pp)) polyF.push_back(f); else { T = f; ++tc; }
        }
        if (tc == 1 && !polyF.empty()) {
            Poly P; 
            if (polyCoeffs(productOf(polyF), var, P)) {
                polyTrim(P);
                NodePtr r = polyTimesTranscendental(P, T, var);
                if (r) return r;
            }
        }
    }

    // ฟังก์ชันตรรกยะ
    if (!vd.empty()) {
        NodePtr r = rationalIntegral(vn, vd, var);
        if (r) return r;
    }

    return substitutionIntegral(vn, vd, var, depth);
}


} // namespace

std::string Calculator::symbolicIntegral(const std::string &expr, char var) {
    SymbolicParser parser(expr);
    NodePtr tree = normNode(parser.parse());
    NodePtr F = integrateNode(tree, var, 0);
    if (!F) throw std::runtime_error("No elementary closed-form antiderivative was found (the integrand may be non-elementary, e.g. e^(x^2) or sin(x)/x, or this form is not supported yet).");
    g_fractionPrint = true;                         // แสดงเศษส่วนสวย ๆ เช่น (1/3) แทน 0.333333333333
    std::string out = nodeToString(prettyNode(F));
    g_fractionPrint = false;
    // ตัดวงเล็บนอกสุดที่ครอบทั้งสูตร
    if (out.size() >= 2 && out.front() == '(') {
        int depth = 0; size_t i = 0;
        for (; i < out.size(); ++i) { if (out[i] == '(') ++depth; else if (out[i] == ')' && --depth == 0) break; }
        if (i == out.size() - 1) out = out.substr(1, out.size() - 2);
    }
    return out;
}

std::string Calculator::symbolicDerivative(const std::string &expr, char var) {
    SymbolicParser parser(expr);
    NodePtr tree = parser.parse();
    NodePtr d = diffNode(tree, var);
    return nodeToString(d);
}

double Calculator::derivative(const std::string &expr, double x0, double h) {
    double savedX = variables['x'];
    double xph = x0 + h, xmh = x0 - h;

    variables['x'] = xph;
    double fph = evaluate(expr);

    variables['x'] = xmh;
    double fmh = evaluate(expr);

    variables['x'] = savedX;
    return (fph - fmh) / (2.0 * h);
}

double Calculator::integral(const std::string &expr, double a, double b, int n) {
    if (a == b) return 0.0;
    if (a > b && (!std::isinf(a) && !std::isinf(b))) return -integral(expr, b, a, n);

    double savedX = variables['x'];

    // ---------- 1. การจัดการขอบเขต Infinity (Improper Integrals) ----------
    
    // 1.1 ขอบเขตเป็น (-inf, +inf) -> แปลงเป็น t ช่วง (-1, 1) ด้วย x = t / (1 - t^2)
    if (std::isinf(a) && a < 0 && std::isinf(b) && b > 0) {
        auto transformedExpr = [&](double t) {
            if (std::abs(t) >= 1.0) return 0.0;
            double denom = 1.0 - t * t;
            double x_val = t / denom;
            variables['x'] = x_val;
            double fVal = evaluate(expr);
            double dxdt = (1.0 + t * t) / (denom * denom);
            return fVal * dxdt;
        };

        double eps = 1e-6;
        double t_a = -1.0 + eps;
        double t_b = 1.0 - eps;
        double h = (t_b - t_a) / n;

        double sum = transformedExpr(t_a) + transformedExpr(t_b);
        for (int i = 1; i < n; ++i) {
            double ti = t_a + i * h;
            sum += (i % 2 == 0 ? 2.0 : 4.0) * transformedExpr(ti);
        }
        variables['x'] = savedX;
        return sum * h / 3.0;
    }

    // 1.2 ขอบเขตเป็น [a, +inf) -> แปลงเป็น t ช่วง [0, 1) ด้วย x = a + t / (1 - t)
    if (std::isinf(b) && b > 0) {
        auto transformedExpr = [&](double t) {
            if (t >= 1.0) return 0.0;
            double denom = 1.0 - t;
            double x_val = a + t / denom;
            variables['x'] = x_val;
            double fVal = evaluate(expr);
            double dxdt = 1.0 / (denom * denom);
            return fVal * dxdt;
        };

        double eps = 1e-6;
        double t_a = 0.0;
        double t_b = 1.0 - eps;
        double h = (t_b - t_a) / n;

        double sum = transformedExpr(t_a) + transformedExpr(t_b);
        for (int i = 1; i < n; ++i) {
            double ti = t_a + i * h;
            sum += (i % 2 == 0 ? 2.0 : 4.0) * transformedExpr(ti);
        }
        variables['x'] = savedX;
        return sum * h / 3.0;
    }

    // 1.3 ขอบเขตเป็น (-inf, b] -> แปลงเป็น t ช่วง (0, 1] ด้วย x = b - (1 - t) / t
    if (std::isinf(a) && a < 0) {
        auto transformedExpr = [&](double t) {
            if (t <= 0.0) return 0.0;
            double x_val = b - (1.0 - t) / t;
            variables['x'] = x_val;
            double fVal = evaluate(expr);
            double dxdt = 1.0 / (t * t);
            return fVal * dxdt;
        };

        double eps = 1e-6;
        double t_a = eps;
        double t_b = 1.0;
        double h = (t_b - t_a) / n;

        double sum = transformedExpr(t_a) + transformedExpr(t_b);
        for (int i = 1; i < n; ++i) {
            double ti = t_a + i * h;
            sum += (i % 2 == 0 ? 2.0 : 4.0) * transformedExpr(ti);
        }
        variables['x'] = savedX;
        return sum * h / 3.0;
    }

    // ---------- 2. การคำนวณช่วงขอบเขตจำกัดปกติ [a, b] ----------
    if (n % 2 != 0) n += 1;
    if (n < 2) n = 2;

    double h = (b - a) / n;

    auto safeEval = [&](double x_val) {
        variables['x'] = x_val;
        try {
            double res = evaluate(expr);
            if (std::isnan(res) || std::isinf(res)) throw std::runtime_error("Overflow");
            return res;
        } catch (...) {
            variables['x'] = x_val + 1e-7;
            return evaluate(expr);
        }
    };

    double sum = safeEval(a) + safeEval(b);

    for (int i = 1; i < n; ++i) {
        double xi = a + i * h;
        double fi = safeEval(xi);
        sum += (i % 2 == 0 ? 2.0 : 4.0) * fi;
    }

    variables['x'] = savedX;
    return sum * h / 3.0;
}

double Calculator::indefiniteIntegral(const std::string &expr, double x0, double x_ref, int n) {
    return integral(expr, x_ref, x0, n);
}

// ---------- Parser Internals ----------

void Calculator::skipWhitespace() {
    while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) {
        ++pos;
    }
}

bool Calculator::eof() const { return pos >= src.size(); }
char Calculator::peek() const { return pos < src.size() ? src[pos] : '\0'; }
char Calculator::get() { return src[pos++]; }

void Calculator::expect(char c) {
    skipWhitespace();
    if (eof() || peek() != c) {
        throw std::runtime_error(std::string("Expected '") + c + "'");
    }
    ++pos;
}

double Calculator::parseExpression() {
    double value = parseTerm();
    for (;;) {
        skipWhitespace();
        if (peek() == '+') { get(); value += parseTerm(); }
        else if (peek() == '-') { get(); value -= parseTerm(); }
        else break;
    }
    return value;
}

double Calculator::parseTerm() {
    double value = parsePower();
    for (;;) {
        skipWhitespace();
        if (peek() == '*') { get(); value *= parsePower(); }
        else if (peek() == '/') {
            get();
            double divisor = parsePower();
            value /= divisor;
        } else if (peek() == '%') {
            get();
            value = alg.percentage(value);
        } else break;
    }
    return value;
}

double Calculator::parsePower() {
    double base = parseUnary();
    skipWhitespace();
    if (peek() == '^') {
        get();
        double exponent = parsePower();
        return alg.power(base, exponent);
    }
    return base;
}

double Calculator::parseUnary() {
    skipWhitespace();
    if (peek() == '-') { get(); return -parseUnary(); }
    if (peek() == '+') { get(); return parseUnary(); }
    return parsePrimary();
}

std::string Calculator::parseIdentifier() {
    std::string name;
    while (!eof() && std::isalpha(static_cast<unsigned char>(peek()))) {
        name += get();
    }
    return name;
}

double Calculator::parsePrimary() {
    skipWhitespace();
    if (eof()) throw std::runtime_error("Unexpected end of expression");

    char c = peek();

    if (c == '(') {
        get();
        double value = parseExpression();
        expect(')');
        return value;
    }

    if (c == '|') {
        get();
        double value = parseExpression();
        expect('|');
        return alg.absolute(value);
    }

    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
        return parseNumber();
    }

    if (std::isalpha(static_cast<unsigned char>(c))) {
        std::string name = parseIdentifier();
        skipWhitespace();
        if (!eof() && peek() == '(') {
            return callFunction(name);
        }
        if (name == "pi") return M_PI;
        if (name == "inf" || name == "infinity") return std::numeric_limits<double>::infinity();
        auto it = variables.find(name[0]);
        if (name.size() == 1 && it != variables.end()) {
            return it->second;
        }
        throw std::runtime_error("Unknown identifier: " + name);
    }

    throw std::runtime_error(std::string("Unexpected character: '") + c + "'");
}

double Calculator::parseNumber() {
    size_t start = pos;
    while (!eof() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.')) {
        ++pos;
    }
    return std::stod(src.substr(start, pos - start));
}

double Calculator::callFunction(const std::string &name) {
    expect('(');
    std::vector<double> args;
    args.push_back(parseExpression());
    skipWhitespace();
    while (peek() == ',') {
        get();
        args.push_back(parseExpression());
        skipWhitespace();
    }
    expect(')');

    auto arg1 = [&]() { return args[0]; };

    if (name == "sqrt") {
        double n = (args.size() >= 2) ? args[1] : 2.0;
        return alg.root(args[0], n);
    }
    if (name == "root") {
        if (args.size() != 2) throw std::runtime_error("root(x, n) needs two arguments");
        return alg.root(args[0], args[1]);
    }
    if (name == "sin")   return std::sin(arg1());
    if (name == "cos")   return std::cos(arg1());
    if (name == "tan")   return std::tan(arg1());
    if (name == "asin")  return std::asin(arg1());
    if (name == "acos")  return std::acos(arg1());
    if (name == "atan")  return std::atan(arg1());
    
    if (name == "log") {
        if (args.size() == 1) return alg.logBase(args[0], 10.0);
        if (args.size() == 2) return alg.logBase(args[0], args[1]);
        throw std::runtime_error("log() accepts 1 or 2 arguments: log(x) or log(x, base)");
    }

    if (name == "ln")    return std::log(arg1());
    if (name == "exp")   return std::exp(arg1());
    if (name == "abs")   return alg.absolute(arg1());
    if (name == "fact")  return static_cast<double>(alg.factorial(static_cast<long long>(std::llround(arg1()))));

    throw std::runtime_error("Unknown function: " + name);
}