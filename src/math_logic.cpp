#include "../lib/math_logic.hpp"
#include "../lib/Calculator_calculus.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <set>
#include <sstream>

using Kind = LogicNode::Kind;

// =====================================================================
//  small helpers
// =====================================================================
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
std::string lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }
bool isIdStart(char c) { return isalpha((unsigned char)c) != 0; }
bool isIdChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

// 2x -> 2*x, 3(x+1) -> 3*(x+1), (x+1)(x-1) -> (x+1)*(x-1)  (used for numeric comparisons)
std::string implicitMul(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        o += c;
        if (i + 1 >= s.size()) break;
        char n = s[i + 1];
        bool cDigit = isdigit((unsigned char)c) || c == '.';
        bool nAlpha = isalpha((unsigned char)n);
        bool ins = false;
        if (cDigit) {
            size_t j = i;
            while (j > 0 && (isdigit((unsigned char)s[j - 1]) || s[j - 1] == '.')) --j;
            bool inIdent = j > 0 && (isalpha((unsigned char)s[j - 1]) || s[j - 1] == '_');
            if (!inIdent) {
                if (nAlpha) {
                    bool sci = (n == 'e' || n == 'E') && i + 2 < s.size() &&
                               (isdigit((unsigned char)s[i + 2]) || s[i + 2] == '+' || s[i + 2] == '-');
                    ins = !sci;
                } else if (n == '(') ins = true;
            }
        } else if (c == ')' && (nAlpha || n == '(' || isdigit((unsigned char)n))) ins = true;
        if (ins) o += '*';
    }
    return o;
}

LogicPtr mkNode(Kind k, LogicPtr a = nullptr, LogicPtr b = nullptr) {
    auto n = std::make_shared<LogicNode>();
    n->kind = k; n->a = std::move(a); n->b = std::move(b);
    return n;
}
LogicPtr mkConst(bool v) { auto n = std::make_shared<LogicNode>(); n->kind = Kind::CONST; n->value = v; return n; }
LogicPtr mkVar(const std::string& name) { auto n = std::make_shared<LogicNode>(); n->kind = Kind::VAR; n->name = name; return n; }
LogicPtr mkNot(const LogicPtr& a) { return mkNode(Kind::NOT, a); }
LogicPtr mkCmp(const std::string& op, const std::string& l, const std::string& r) {
    auto n = std::make_shared<LogicNode>();
    n->kind = Kind::CMP; n->name = op; n->lhs = l; n->rhs = r;
    return n;
}
LogicPtr mkQuant(Kind k, const std::string& var, const std::string& uni, const LogicPtr& body) {
    auto n = std::make_shared<LogicNode>();
    n->kind = k; n->name = var; n->universeText = uni; n->a = body;
    return n;
}

// =====================================================================
//  recursive-descent parser
// =====================================================================
class Parser {
public:
    explicit Parser(std::string text) : s(std::move(text)) {}
    LogicPtr parse() {
        ws();
        if (p >= s.size()) fail("empty formula");
        LogicPtr n = parseIff();
        ws();
        if (p < s.size()) fail(std::string("unexpected '") + s[p] + "'");
        return n;
    }

private:
    std::string s;
    size_t p = 0;

    [[noreturn]] void fail(const std::string& msg) const {
        throw std::invalid_argument(msg + " (at position " + std::to_string(p + 1) + ")");
    }
    void ws() { while (p < s.size() && isspace((unsigned char)s[p])) ++p; }
    bool startsAt(size_t q, const char* t) const { return s.compare(q, strlen(t), t) == 0; }
    std::string wordAt(size_t q) const {
        if (q >= s.size() || !isIdStart(s[q])) return "";
        size_t e = q;
        while (e < s.size() && isIdChar(s[e])) ++e;
        return s.substr(q, e - q);
    }
    bool wordIs(const char* w) { ws(); std::string x = wordAt(p); return !x.empty() && lower(x) == w; }
    bool eatWord(const char* w) { if (wordIs(w)) { p += strlen(w); return true; } return false; }

    // a logical connective starts at q (used to end an arithmetic atom)
    bool atConnective(size_t q) const {
        if (q >= s.size()) return false;
        char c = s[q];
        if (c == '&' || c == '|') return true;
        if (startsAt(q, "->") || startsAt(q, "<->") || startsAt(q, "<=>") || startsAt(q, "=>")) return true;
        if (startsAt(q, "/\\") || startsAt(q, "\\/")) return true;
        if (isIdStart(c) && (q == 0 || !isIdChar(s[q - 1]))) {
            std::string w = lower(wordAt(q));
            static const char* ws_[] = {"and", "or", "xor", "nand", "nor", "implies", "iff"};
            for (const char* x : ws_) if (w == x) return true;
        }
        return false;
    }
    std::string relopAt(size_t q) const {
        static const char* ops[] = {">=", "<=", "!=", "==", "<>", "=", "<", ">"};
        for (const char* o : ops) if (startsAt(q, o)) return o;
        return "";
    }
    static std::string normOp(const std::string& op) {
        if (op == "==") return "=";
        if (op == "<>") return "!=";
        return op;
    }
    size_t matching(size_t open) {
        int d = 0;
        for (size_t q = open; q < s.size(); ++q) {
            if (s[q] == '(') ++d;
            else if (s[q] == ')' && --d == 0) return q;
        }
        p = open;
        fail("missing ')'");
    }

    // ---- precedence climbing ----
    LogicPtr parseIff() {
        LogicPtr l = parseImp();
        for (;;) {
            ws();
            if (startsAt(p, "<->") || startsAt(p, "<=>")) { p += 3; l = mkNode(Kind::IFF, l, parseImp()); }
            else if (eatWord("iff")) l = mkNode(Kind::IFF, l, parseImp());
            else break;
        }
        return l;
    }
    LogicPtr parseImp() {
        LogicPtr l = parseOr();
        ws();
        if (startsAt(p, "->") || startsAt(p, "=>")) { p += 2; return mkNode(Kind::IMPLIES, l, parseImp()); }
        if (eatWord("implies")) return mkNode(Kind::IMPLIES, l, parseImp());
        return l;
    }
    LogicPtr parseOr() {
        LogicPtr l = parseXor();
        for (;;) {
            ws();
            if (startsAt(p, "||")) { p += 2; l = mkNode(Kind::OR, l, parseXor()); }
            else if (startsAt(p, "\\/")) { p += 2; l = mkNode(Kind::OR, l, parseXor()); }
            else if (p < s.size() && s[p] == '|') { ++p; l = mkNode(Kind::OR, l, parseXor()); }
            else if (eatWord("or")) l = mkNode(Kind::OR, l, parseXor());
            else if (eatWord("nor")) l = mkNode(Kind::NOR, l, parseXor());
            else break;
        }
        return l;
    }
    LogicPtr parseXor() {
        LogicPtr l = parseAnd();
        while (eatWord("xor")) l = mkNode(Kind::XOR, l, parseAnd());
        return l;
    }
    LogicPtr parseAnd() {
        LogicPtr l = parseUnary();
        for (;;) {
            ws();
            if (startsAt(p, "&&")) { p += 2; l = mkNode(Kind::AND, l, parseUnary()); }
            else if (startsAt(p, "/\\")) { p += 2; l = mkNode(Kind::AND, l, parseUnary()); }
            else if (p < s.size() && s[p] == '&') { ++p; l = mkNode(Kind::AND, l, parseUnary()); }
            else if (eatWord("and")) l = mkNode(Kind::AND, l, parseUnary());
            else if (eatWord("nand")) l = mkNode(Kind::NAND, l, parseUnary());
            else break;
        }
        return l;
    }
    LogicPtr parseUnary() {
        ws();
        if (p < s.size() && (s[p] == '~' || (s[p] == '!' && !startsAt(p, "!=")))) { ++p; return mkNot(parseUnary()); }
        if (eatWord("not")) return mkNot(parseUnary());
        if (eatWord("forall") || eatWord("all")) return parseQuantifier(Kind::FORALL);
        if (eatWord("exists") || eatWord("exist")) return parseQuantifier(Kind::EXISTS);
        return parsePrimary();
    }
    LogicPtr parseQuantifier(Kind k) {
        ws();
        if (p >= s.size() || !isalpha((unsigned char)s[p])) fail("expected a variable letter after the quantifier");
        char v = s[p++];
        if (p < s.size() && isIdChar(s[p])) fail("a quantifier variable must be a single letter (a-z, A-Z)");
        if (!eatWord("in")) fail("expected 'in', e.g.  forall x in {1,2,3}: x > 0");
        ws();
        size_t q = p; int d = 0;
        while (q < s.size() && !(d == 0 && s[q] == ':')) {
            if (s[q] == '(' || s[q] == '{' || s[q] == '[') ++d;
            else if (s[q] == ')' || s[q] == '}' || s[q] == ']') --d;
            ++q;
        }
        if (q >= s.size()) fail("expected ':' before the body, e.g.  forall x in {1,2,3}: x > 0");
        std::string uni = trim(s.substr(p, q - p));
        if (uni.empty()) fail("empty universe");
        p = q + 1;
        LogicPtr body = parseIff();
        return mkQuant(k, std::string(1, v), uni, body);
    }
    bool arithFollows(size_t q) const {                 // after an atom: does arithmetic / a relation continue?
        while (q < s.size() && isspace((unsigned char)s[q])) ++q;
        if (q >= s.size() || atConnective(q)) return false;
        char c = s[q];
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || c == '%') return true;
        return !relopAt(q).empty();
    }
    LogicPtr parsePrimary() {
        ws();
        if (p >= s.size()) fail("unexpected end of formula");
        char c = s[p];
        if (c == '(') {
            size_t close = matching(p);
            if (arithFollows(close + 1)) return parseArith();
            ++p;
            LogicPtr n = parseIff();
            ws();
            if (p >= s.size() || s[p] != ')') fail("expected ')'");
            ++p;
            return n;
        }
        if (isIdStart(c)) {
            std::string w = wordAt(p);
            std::string lw = lower(w);
            if (w == "T" || lw == "true") { p += w.size(); return mkConst(true); }
            if (w == "F" || lw == "false") { p += w.size(); return mkConst(false); }
            size_t after = p + w.size();
            size_t q = after;
            while (q < s.size() && isspace((unsigned char)s[q])) ++q;
            if (q < s.size() && s[q] == '(') return parseArith();           // sin(x) > 0 ...
            if (arithFollows(after)) return parseArith();
            p = after;
            return mkVar(w);
        }
        if (isdigit((unsigned char)c) || c == '.' || c == '-' || c == '+') return parseArith();
        fail(std::string("unexpected '") + c + "'");
    }
    // comparison atom (possibly chained:  1 < x <= 5  ==  (1 < x) & (x <= 5))
    LogicPtr parseArith() {
        std::vector<std::string> exprs, ops;
        std::string cur;
        int depth = 0;
        while (p < s.size()) {
            char c = s[p];
            if (depth == 0) {
                if (atConnective(p) || c == ')') break;
                std::string op = relopAt(p);
                if (!op.empty()) { exprs.push_back(trim(cur)); cur.clear(); ops.push_back(normOp(op)); p += op.size(); continue; }
            }
            if (c == '(') ++depth;
            else if (c == ')') --depth;
            cur += c; ++p;
        }
        exprs.push_back(trim(cur));
        if (ops.empty()) fail("expected a comparison such as  x > 2  or  x^2 = 4");
        for (auto& e : exprs) if (e.empty()) fail("missing expression next to a comparison operator");
        LogicPtr res;
        for (size_t i = 0; i < ops.size(); ++i) {
            LogicPtr c = mkCmp(ops[i], exprs[i], exprs[i + 1]);
            res = res ? mkNode(Kind::AND, res, c) : c;
        }
        return res;
    }
};

}  // namespace

// =====================================================================
//  public: construction, parsing, printing
// =====================================================================
MathLogic::MathLogic(Calculator* calc) : calc_(calc) {}
void MathLogic::setCalculator(Calculator* calc) { calc_ = calc; }

std::string MathLogic::normalizeSymbols(const std::string& text) {
    static const std::vector<std::pair<std::string, std::string>> tbl = {
        {"\xC2\xAC", "~"}, {"\xE2\x88\xA7", "&"}, {"\xE2\x88\xA8", "|"}, {"\xE2\x8A\x95", " xor "},
        {"\xE2\x8A\xBC", " nand "}, {"\xE2\x8A\xBD", " nor "}, {"\xE2\x86\x92", "->"}, {"\xE2\x87\x92", "->"},
        {"\xE2\x9F\xB6", "->"}, {"\xE2\x86\x94", "<->"}, {"\xE2\x87\x94", "<->"}, {"\xE2\x89\xA4", "<="},
        {"\xE2\x89\xA5", ">="}, {"\xE2\x89\xA0", "!="}, {"\xE2\x88\x80", " forall "}, {"\xE2\x88\x83", " exists "},
        {"\xE2\x88\x88", " in "}, {"\xE2\x8A\xA4", "T"}, {"\xE2\x8A\xA5", "F"}};
    std::string out = text;
    for (auto& kv : tbl) {
        size_t pos = 0;
        while ((pos = out.find(kv.first, pos)) != std::string::npos) { out.replace(pos, kv.first.size(), kv.second); pos += kv.second.size(); }
    }
    return out;
}

LogicPtr MathLogic::parse(const std::string& text) const {
    return Parser(normalizeSymbols(text)).parse();
}

namespace {
int precOf(Kind k) {
    switch (k) {
        case Kind::IFF: return 1;
        case Kind::IMPLIES: return 2;
        case Kind::OR: case Kind::NOR: return 3;
        case Kind::XOR: return 4;
        case Kind::AND: case Kind::NAND: return 5;
        case Kind::NOT: return 6;
        case Kind::FORALL: case Kind::EXISTS: return 0;
        default: return 7;
    }
}
std::string print(const LogicPtr& n, MathLogic::Style st);
std::string wrap(const LogicPtr& child, int parentPrec, bool strictRight, MathLogic::Style st, Kind parentKind, bool isRight) {
    std::string t = print(child, st);
    int cp = precOf(child->kind);
    bool paren = cp < parentPrec;
    if (cp == parentPrec) {
        // same precedence: keep the exact tree shape
        if (parentKind == Kind::IMPLIES) paren = !isRight;
        else paren = isRight || child->kind != parentKind;
        (void)strictRight;
    }
    return paren ? "(" + t + ")" : t;
}
std::string print(const LogicPtr& n, MathLogic::Style st) {
    bool sym = st == MathLogic::Style::Symbols;
    switch (n->kind) {
        case Kind::CONST: return n->value ? "T" : "F";
        case Kind::VAR: return n->name;
        case Kind::CMP: return n->lhs + " " + n->name + " " + n->rhs;
        case Kind::NOT: {
            int cp = precOf(n->a->kind);
            std::string t = print(n->a, st);
            return std::string(sym ? "\xC2\xAC" : "~") + (cp < 7 && n->a->kind != Kind::NOT ? "(" + t + ")" : t);
        }
        case Kind::FORALL: case Kind::EXISTS:
            return std::string(n->kind == Kind::FORALL ? (sym ? "\xE2\x88\x80" : "forall ") : (sym ? "\xE2\x88\x83" : "exists ")) +
                   n->name + (sym ? " \xE2\x88\x88 " : " in ") + n->universeText + ": " + print(n->a, st);
        default: break;
    }
    int pr = precOf(n->kind);
    const char* op = "";
    switch (n->kind) {
        case Kind::AND: op = sym ? " \xE2\x88\xA7 " : " & "; break;
        case Kind::OR: op = sym ? " \xE2\x88\xA8 " : " | "; break;
        case Kind::XOR: op = sym ? " \xE2\x8A\x95 " : " xor "; break;
        case Kind::NAND: op = " nand "; break;
        case Kind::NOR: op = " nor "; break;
        case Kind::IMPLIES: op = sym ? " \xE2\x86\x92 " : " -> "; break;
        case Kind::IFF: op = sym ? " \xE2\x86\x94 " : " <-> "; break;
        default: break;
    }
    return wrap(n->a, pr, false, st, n->kind, false) + op + wrap(n->b, pr, true, st, n->kind, true);
}
}  // namespace

std::string MathLogic::toString(const LogicPtr& n, Style style) { return print(n, style); }

// =====================================================================
//  semantics
// =====================================================================
struct MathLogic::Env {
    const std::map<std::string, bool>* props = nullptr;
    std::map<char, double> nums;
};

namespace {
void collectVars(const LogicPtr& n, std::set<std::string>& out) {
    if (!n) return;
    if (n->kind == Kind::VAR) out.insert(n->name);
    collectVars(n->a, out);
    collectVars(n->b, out);
}
bool nearlyEqual(double a, double b) { return std::fabs(a - b) <= 1e-9 * std::max({1.0, std::fabs(a), std::fabs(b)}); }
}  // namespace

std::vector<std::string> MathLogic::variables(const LogicPtr& n) const {
    std::set<std::string> s;
    collectVars(n, s);
    return std::vector<std::string>(s.begin(), s.end());
}

double MathLogic::arith(const std::string& expr, const Env& env) const {
    if (!calc_) throw std::runtime_error("comparisons such as x > 2 need a Calculator (MathLogic(&calc))");
    for (auto& kv : env.nums) calc_->setVariable(kv.first, kv.second);
    try { return calc_->evaluate(implicitMul(expr)); }
    catch (const std::exception& e) { throw std::runtime_error("cannot evaluate '" + expr + "': " + e.what()); }
}

std::vector<double> MathLogic::universe(const std::string& text0) const {
    std::string t = trim(text0);
    if (!calc_) throw std::runtime_error("quantifiers need a Calculator (MathLogic(&calc))");
    auto num = [&](const std::string& e) { Env env; return arith(e, env); };
    std::vector<double> out;
    auto addRange = [&](const std::string& spec) {
        size_t dd = spec.find("..");
        if (dd == std::string::npos) { out.push_back(num(spec)); return; }
        std::string lo = trim(spec.substr(0, dd)), rest = trim(spec.substr(dd + 2));
        std::string stepTxt = "1";
        size_t st = lower(rest).find(" step ");
        if (st != std::string::npos) { stepTxt = rest.substr(st + 6); rest = trim(rest.substr(0, st)); }
        double a = num(lo), b = num(rest), step = num(stepTxt);
        if (step == 0) throw std::runtime_error("range step cannot be 0");
        if ((b - a) / step > 1e6) throw std::runtime_error("universe too large (max 1,000,000 elements)");
        if (step > 0) for (double v = a; v <= b + 1e-9; v += step) out.push_back(v);
        else for (double v = a; v >= b - 1e-9; v += step) out.push_back(v);
    };
    if (!t.empty() && t.front() == '{' && t.back() == '}') {
        std::string inner = t.substr(1, t.size() - 2);
        std::string cur; int d = 0;
        for (char c : inner + ",") {
            if (c == '(' || c == '{') ++d; else if (c == ')' || c == '}') --d;
            if (c == ',' && d == 0) { if (!trim(cur).empty()) addRange(trim(cur)); cur.clear(); }
            else cur += c;
        }
    } else addRange(t);
    if (out.empty()) throw std::runtime_error("the universe is empty");
    return out;
}

bool MathLogic::eval(const LogicPtr& n, Env& env) const {
    switch (n->kind) {
        case Kind::CONST: return n->value;
        case Kind::VAR: {
            if (env.props) {
                auto it = env.props->find(n->name);
                if (it != env.props->end()) return it->second;
            }
            throw std::invalid_argument("no truth value given for variable '" + n->name + "'");
        }
        case Kind::NOT: return !eval(n->a, env);
        case Kind::AND: { bool x = eval(n->a, env); bool y = eval(n->b, env); return x && y; }
        case Kind::OR: { bool x = eval(n->a, env); bool y = eval(n->b, env); return x || y; }
        case Kind::XOR: return eval(n->a, env) != eval(n->b, env);
        case Kind::NAND: { bool x = eval(n->a, env); bool y = eval(n->b, env); return !(x && y); }
        case Kind::NOR: { bool x = eval(n->a, env); bool y = eval(n->b, env); return !(x || y); }
        case Kind::IMPLIES: { bool x = eval(n->a, env); bool y = eval(n->b, env); return !x || y; }
        case Kind::IFF: return eval(n->a, env) == eval(n->b, env);
        case Kind::CMP: {
            double l = arith(n->lhs, env), r = arith(n->rhs, env);
            const std::string& o = n->name;
            if (o == "=") return nearlyEqual(l, r);
            if (o == "!=") return !nearlyEqual(l, r);
            if (o == "<") return l < r && !nearlyEqual(l, r);
            if (o == "<=") return l < r || nearlyEqual(l, r);
            if (o == ">") return l > r && !nearlyEqual(l, r);
            return l > r || nearlyEqual(l, r);
        }
        case Kind::FORALL: case Kind::EXISTS: {
            std::vector<double> uni = universe(n->universeText);
            char v = n->name[0];
            bool isAll = n->kind == Kind::FORALL;
            auto saved = env.nums.find(v) != env.nums.end() ? std::make_pair(true, env.nums[v]) : std::make_pair(false, 0.0);
            bool result = isAll;
            for (double x : uni) {
                env.nums[v] = x;
                bool b = eval(n->a, env);
                if (isAll && !b) { result = false; break; }
                if (!isAll && b) { result = true; break; }
            }
            if (saved.first) env.nums[v] = saved.second; else env.nums.erase(v);
            return result;
        }
    }
    return false;
}

bool MathLogic::evaluate(const LogicPtr& n, const std::map<std::string, bool>& assignment) const {
    Env env; env.props = &assignment;
    return eval(n, env);
}
bool MathLogic::evaluate(const std::string& formula, const std::map<std::string, bool>& assignment) const {
    return evaluate(parse(formula), assignment);
}

std::map<std::string, bool> MathLogic::parseAssignment(const std::string& text) {
    std::map<std::string, bool> m;
    std::string cur;
    for (char c : text + ",") {
        if (c == ',' || c == ';' || c == '\n') {
            std::string t = trim(cur); cur.clear();
            if (t.empty()) continue;
            size_t eq = t.find('=');
            if (eq == std::string::npos) throw std::invalid_argument("use the form  p=T, q=F  (T/F or 1/0)");
            std::string name = trim(t.substr(0, eq)), val = lower(trim(t.substr(eq + 1)));
            if (name.empty()) throw std::invalid_argument("missing variable name before '='");
            bool b;
            if (val == "t" || val == "true" || val == "1") b = true;
            else if (val == "f" || val == "false" || val == "0") b = false;
            else throw std::invalid_argument("truth value of '" + name + "' must be T/F (or 1/0), got '" + val + "'");
            m[name] = b;
        } else cur += c;
    }
    return m;
}

// =====================================================================
//  truth tables
// =====================================================================
namespace {
typedef unsigned long long ull;
const int MAX_VARS = 22;

void collectSubs(const LogicPtr& n, std::vector<LogicPtr>& out, std::set<std::string>& seen) {
    if (!n) return;
    if (n->kind != Kind::FORALL && n->kind != Kind::EXISTS) { collectSubs(n->a, out, seen); collectSubs(n->b, out, seen); }
    if (n->kind == Kind::VAR || n->kind == Kind::CONST) return;
    std::string s = print(n, MathLogic::Style::ASCII);
    if (seen.insert(s).second) out.push_back(n);
}
}  // namespace

std::vector<std::string> MathLogic::unionVars(const std::vector<LogicPtr>& fs) const {
    std::set<std::string> s;
    for (auto& f : fs) collectVars(f, s);
    return std::vector<std::string>(s.begin(), s.end());
}

std::vector<bool> MathLogic::column(const LogicPtr& f, const std::vector<std::string>& vars) const {
    size_t n = vars.size();
    if ((int)n > MAX_VARS) throw std::invalid_argument("too many variables (" + std::to_string(n) + "); the limit is " + std::to_string(MAX_VARS));
    ull rows = 1ULL << n;
    std::vector<bool> out(rows);
    std::map<std::string, bool> a;
    Env env; env.props = &a;
    for (ull r = 0; r < rows; ++r) {
        for (size_t i = 0; i < n; ++i) a[vars[i]] = !((r >> (n - 1 - i)) & 1ULL);
        out[r] = eval(f, env);
    }
    return out;
}

MathLogic::TruthTable MathLogic::truthTable(const std::string& formula, bool subformulas) const {
    LogicPtr f = parse(formula);
    TruthTable t;
    t.vars = variables(f);
    size_t n = t.vars.size();
    if ((int)n > 16) throw std::invalid_argument("a printed truth table is limited to 16 variables (" + std::to_string(n) + " given); use Classify / Normal forms for larger formulas");
    std::vector<LogicPtr> cols;
    if (subformulas) { std::set<std::string> seen; collectSubs(f, cols, seen); }
    if (cols.empty() || print(cols.back(), Style::ASCII) != print(f, Style::ASCII)) cols.push_back(f);
    for (auto& c : cols) t.columns.push_back(print(c, Style::ASCII));
    ull rows = 1ULL << n;
    std::map<std::string, bool> a;
    Env env; env.props = &a;
    for (ull r = 0; r < rows; ++r) {
        std::vector<bool> in(n), vals(cols.size());
        for (size_t i = 0; i < n; ++i) { in[i] = !((r >> (n - 1 - i)) & 1ULL); a[t.vars[i]] = in[i]; }
        for (size_t c = 0; c < cols.size(); ++c) vals[c] = eval(cols[c], env);
        t.inputs.push_back(in); t.values.push_back(vals);
    }
    return t;
}

MathLogic::TruthTable MathLogic::truthTableOf(const std::vector<std::string>& formulas) const {
    std::vector<LogicPtr> fs;
    for (auto& s : formulas) fs.push_back(parse(s));
    TruthTable t;
    t.vars = unionVars(fs);
    size_t n = t.vars.size();
    if ((int)n > 16) throw std::invalid_argument("a printed truth table is limited to 16 variables");
    for (auto& f : fs) t.columns.push_back(print(f, Style::ASCII));
    ull rows = 1ULL << n;
    std::map<std::string, bool> a;
    Env env; env.props = &a;
    for (ull r = 0; r < rows; ++r) {
        std::vector<bool> in(n), vals(fs.size());
        for (size_t i = 0; i < n; ++i) { in[i] = !((r >> (n - 1 - i)) & 1ULL); a[t.vars[i]] = in[i]; }
        for (size_t c = 0; c < fs.size(); ++c) vals[c] = eval(fs[c], env);
        t.inputs.push_back(in); t.values.push_back(vals);
    }
    return t;
}

std::string MathLogic::formatTruthTable(const TruthTable& t, bool useTF, size_t maxRows, const std::vector<bool>* mark) {
    std::vector<std::string> heads = t.vars;
    for (auto& c : t.columns) heads.push_back(c);
    std::vector<size_t> w(heads.size());
    for (size_t i = 0; i < heads.size(); ++i) w[i] = std::max<size_t>(1, heads[i].size());
    auto center = [](const std::string& s, size_t width) {
        size_t left = (width - s.size()) / 2;
        return std::string(left, ' ') + s + std::string(width - s.size() - left, ' ');
    };
    std::ostringstream o;
    auto line = [&](const std::vector<std::string>& cells, const std::string& tail) {
        for (size_t i = 0; i < cells.size(); ++i) { if (i) o << " | "; o << center(cells[i], w[i]); }
        o << tail << "\n";
    };
    line(heads, "");
    size_t total = 0; for (size_t i = 0; i < w.size(); ++i) total += w[i] + (i ? 3 : 0);
    o << std::string(total, '-') << "\n";
    const char* T = useTF ? "T" : "1"; const char* F = useTF ? "F" : "0";
    size_t shown = std::min(maxRows, t.values.size());
    for (size_t r = 0; r < shown; ++r) {
        std::vector<std::string> cells;
        for (bool b : t.inputs[r]) cells.push_back(b ? T : F);
        for (bool b : t.values[r]) cells.push_back(b ? T : F);
        line(cells, (mark && r < mark->size() && (*mark)[r]) ? "   <-- critical row" : "");
    }
    if (shown < t.values.size()) o << "... " << (t.values.size() - shown) << " more rows (total " << t.values.size() << ")\n";
    return o.str();
}

// =====================================================================
//  classification / consequence
// =====================================================================
MathLogic::Classification MathLogic::classify(const std::string& formula) const {
    LogicPtr f = parse(formula);
    Classification c;
    c.vars = variables(f);
    std::vector<bool> col = column(f, c.vars);
    size_t n = c.vars.size();
    c.rows = col.size();
    ull mask = (1ULL << n) - 1;
    for (ull r = 0; r < col.size(); ++r) if (col[r]) { ++c.trueRows; c.minterms.push_back(mask ^ r); }
    std::sort(c.minterms.begin(), c.minterms.end());
    c.kind = c.trueRows == c.rows ? Verdict::Tautology : c.trueRows == 0 ? Verdict::Contradiction : Verdict::Contingency;
    return c;
}
// (the enum class named Kind inside MathLogic shadows LogicNode::Kind only inside member scopes)

bool MathLogic::equivalent(const std::string& a, const std::string& b) const {
    LogicPtr fa = parse(a), fb = parse(b);
    auto vars = unionVars({fa, fb});
    return column(fa, vars) == column(fb, vars);
}
bool MathLogic::entails(const std::string& a, const std::string& b) const {
    LogicPtr fa = parse(a), fb = parse(b);
    auto vars = unionVars({fa, fb});
    auto ca = column(fa, vars), cb = column(fb, vars);
    for (size_t i = 0; i < ca.size(); ++i) if (ca[i] && !cb[i]) return false;
    return true;
}

namespace {
std::string assignText(const std::vector<std::string>& vars, ull row, bool useTF = true) {
    std::string s;
    size_t n = vars.size();
    for (size_t i = 0; i < n; ++i) {
        bool v = !((row >> (n - 1 - i)) & 1ULL);
        s += (i ? ", " : "") + vars[i] + "=" + (useTF ? (v ? "T" : "F") : (v ? "1" : "0"));
    }
    return s.empty() ? "(no variables)" : s;
}
std::string joinList(const std::vector<ull>& v, size_t cap = 64) {
    std::string s;
    for (size_t i = 0; i < v.size() && i < cap; ++i) s += (i ? ", " : "") + std::to_string(v[i]);
    if (v.size() > cap) s += ", ... (" + std::to_string(v.size()) + " total)";
    return s;
}
}  // namespace

std::string MathLogic::classifyReport(const std::string& formula) const {
    LogicPtr f = parse(formula);
    Classification c = classify(formula);
    std::vector<bool> col = column(f, c.vars);
    std::ostringstream o;
    o << "Formula:   " << print(f, Style::ASCII) << "\n";
    o << "Variables: ";
    for (size_t i = 0; i < c.vars.size(); ++i) o << (i ? ", " : "") << c.vars[i];
    if (c.vars.empty()) o << "(none)";
    o << "   (" << c.rows << " row" << (c.rows == 1 ? "" : "s") << ")\n";
    o << "True in " << c.trueRows << " of " << c.rows << " rows\n\n";
    const char* kind = c.kind == Verdict::Tautology ? "TAUTOLOGY (always true, valid)"
                     : c.kind == Verdict::Contradiction ? "CONTRADICTION (always false, unsatisfiable)"
                     : "CONTINGENCY (sometimes true, sometimes false; satisfiable but not valid)";
    o << "Result: " << kind << "\n";
    bool foundT = false, foundF = false;
    for (ull r = 0; r < col.size() && !(foundT && foundF); ++r) {
        if (col[r] && !foundT) { foundT = true; o << "  A model (makes it true):     " << assignText(c.vars, r) << "\n"; }
        if (!col[r] && !foundF) { foundF = true; o << "  A counter-model (makes it false): " << assignText(c.vars, r) << "\n"; }
    }
    if (!c.vars.empty()) {
        o << "\nMinterms  m(" << joinList(c.minterms) << ")   [first variable = most significant bit, T = 1]\n";
        std::vector<ull> max;
        ull mask = (1ULL << c.vars.size()) - 1;
        for (ull r = 0; r < col.size(); ++r) if (!col[r]) max.push_back(mask ^ r);
        std::sort(max.begin(), max.end());
        o << "Maxterms  M(" << joinList(max) << ")\n";
    }
    return o.str();
}

// =====================================================================
//  standard laws and rules (pattern matching with meta-variables p, q, r, s)
// =====================================================================
namespace {
struct LawSpec { const char* name; const char* lhs; const char* rhs; };
const LawSpec LAWS[] = {
    {"Double negation", "~~p", "p"},
    {"Idempotent (and)", "p & p", "p"}, {"Idempotent (or)", "p | p", "p"},
    {"Commutative (and)", "p & q", "q & p"}, {"Commutative (or)", "p | q", "q | p"},
    {"Commutative (xor)", "p xor q", "q xor p"}, {"Commutative (iff)", "p <-> q", "q <-> p"},
    {"Associative (and)", "(p & q) & r", "p & (q & r)"}, {"Associative (or)", "(p | q) | r", "p | (q | r)"},
    {"Distributive (and over or)", "p & (q | r)", "(p & q) | (p & r)"},
    {"Distributive (or over and)", "p | (q & r)", "(p | q) & (p | r)"},
    {"De Morgan (not and)", "~(p & q)", "~p | ~q"}, {"De Morgan (not or)", "~(p | q)", "~p & ~q"},
    {"Absorption (and)", "p & (p | q)", "p"}, {"Absorption (or)", "p | (p & q)", "p"},
    {"Identity (and)", "p & T", "p"}, {"Identity (or)", "p | F", "p"},
    {"Domination (or)", "p | T", "T"}, {"Domination (and)", "p & F", "F"},
    {"Complement (excluded middle)", "p | ~p", "T"}, {"Complement (contradiction)", "p & ~p", "F"},
    {"Material implication", "p -> q", "~p | q"},
    {"Contrapositive", "p -> q", "~q -> ~p"},
    {"Negation of implication", "~(p -> q)", "p & ~q"},
    {"Implication as or", "p | q", "~p -> q"},
    {"Biconditional (two implications)", "p <-> q", "(p -> q) & (q -> p)"},
    {"Biconditional (same truth value)", "p <-> q", "(p & q) | (~p & ~q)"},
    {"Negation of biconditional", "~(p <-> q)", "p xor q"},
    {"Exclusive or definition", "p xor q", "(p | q) & ~(p & q)"},
    {"Exportation", "(p & q) -> r", "p -> (q -> r)"},
    {"Distribution of implication (and)", "p -> (q & r)", "(p -> q) & (p -> r)"},
    {"Distribution of implication (or)", "(p | q) -> r", "(p -> r) & (q -> r)"},
    {"NAND definition", "p nand q", "~(p & q)"}, {"NOR definition", "p nor q", "~(p | q)"},
};

struct RuleSpec { const char* name; std::vector<const char*> premises; const char* conclusion; bool fallacy; };
const RuleSpec RULES[] = {
    {"Modus Ponens (MP)", {"p -> q", "p"}, "q", false},
    {"Modus Tollens (MT)", {"p -> q", "~q"}, "~p", false},
    {"Hypothetical Syllogism (HS)", {"p -> q", "q -> r"}, "p -> r", false},
    {"Disjunctive Syllogism (DS)", {"p | q", "~p"}, "q", false},
    {"Disjunctive Syllogism (DS)", {"p | q", "~q"}, "p", false},
    {"Simplification", {"p & q"}, "p", false},
    {"Simplification", {"p & q"}, "q", false},
    {"Conjunction", {"p", "q"}, "p & q", false},
    {"Addition", {"p"}, "p | q", false},
    {"Constructive Dilemma", {"(p -> q) & (r -> s)", "p | r"}, "q | s", false},
    {"Destructive Dilemma", {"(p -> q) & (r -> s)", "~q | ~s"}, "~p | ~r", false},
    {"Resolution", {"p | q", "~p | r"}, "q | r", false},
    {"Proof by cases", {"p -> r", "q -> r", "p | q"}, "r", false},
    {"Biconditional elimination", {"p <-> q"}, "p -> q", false},
    {"Biconditional elimination", {"p <-> q"}, "q -> p", false},
    {"Biconditional introduction", {"p -> q", "q -> p"}, "p <-> q", false},
    {"Contraposition", {"p -> q"}, "~q -> ~p", false},
    {"Double negation elimination", {"~~p"}, "p", false},
    {"Affirming the consequent (fallacy)", {"p -> q", "q"}, "p", true},
    {"Denying the antecedent (fallacy)", {"p -> q", "~p"}, "~q", true},
    {"Affirming a disjunct (fallacy)", {"p | q", "p"}, "~q", true},
};

bool sameTree(const LogicPtr& x, const LogicPtr& y) { return print(x, MathLogic::Style::ASCII) == print(y, MathLogic::Style::ASCII); }

bool matchPat(const LogicPtr& pat, const LogicPtr& t, std::map<std::string, LogicPtr>& b) {
    if (pat->kind == Kind::VAR) {
        auto it = b.find(pat->name);
        if (it != b.end()) return sameTree(it->second, t);
        b[pat->name] = t;
        return true;
    }
    if (pat->kind != t->kind) return false;
    if (pat->kind == Kind::CONST) return pat->value == t->value;
    if (pat->kind == Kind::CMP || pat->kind == Kind::FORALL || pat->kind == Kind::EXISTS) return false;
    if (pat->a && !matchPat(pat->a, t->a, b)) return false;
    if (pat->b && !matchPat(pat->b, t->b, b)) return false;
    return true;
}
LogicPtr P(const char* s) { return Parser(s).parse(); }
}  // namespace

std::string MathLogic::identifyLaw(const LogicPtr& a, const LogicPtr& b) {
    for (const LawSpec& L : LAWS) {
        LogicPtr l = P(L.lhs), r = P(L.rhs);
        for (int dir = 0; dir < 2; ++dir) {
            std::map<std::string, LogicPtr> bind;
            if (matchPat(dir ? r : l, a, bind) && matchPat(dir ? l : r, b, bind)) return L.name;
        }
    }
    return "";
}

std::string MathLogic::equivalenceReport(const std::string& a, const std::string& b) const {
    LogicPtr fa = parse(a), fb = parse(b);
    auto vars = unionVars({fa, fb});
    auto ca = column(fa, vars), cb = column(fb, vars);
    bool eq = ca == cb, ab = true, ba = true;
    ull diff = 0; bool haveDiff = false;
    for (size_t i = 0; i < ca.size(); ++i) {
        if (ca[i] && !cb[i]) ab = false;
        if (cb[i] && !ca[i]) ba = false;
        if (ca[i] != cb[i] && !haveDiff) { haveDiff = true; diff = i; }
    }
    std::ostringstream o;
    o << "A = " << print(fa, Style::ASCII) << "\nB = " << print(fb, Style::ASCII) << "\n\n";
    o << "A and B equivalent (A <-> B is a tautology)?  " << (eq ? "YES" : "NO") << "\n";
    o << "A implies B (A |= B)?  " << (ab ? "yes" : "no") << "\n";
    o << "B implies A (B |= A)?  " << (ba ? "yes" : "no") << "\n";
    if (eq) {
        std::string law = identifyLaw(fa, fb);
        o << "Named law: " << (law.empty() ? "(no single standard law; equivalent by truth table)" : law) << "\n";
    } else if (haveDiff) {
        o << "They differ at " << assignText(vars, diff) << " :  A = " << (ca[diff] ? "T" : "F") << ",  B = " << (cb[diff] ? "T" : "F") << "\n";
    }
    if (vars.size() <= 6) {
        o << "\n" << formatTruthTable(truthTableOf({a, b}));
    }
    return o.str();
}

// =====================================================================
//  arguments
// =====================================================================
namespace {
std::string detectPattern(const std::vector<LogicPtr>& prem, const LogicPtr& concl, bool fallacies) {
    size_t k = prem.size();
    if (k == 0 || k > 3) return "";
    for (const RuleSpec& R : RULES) {
        if (R.fallacy != fallacies || R.premises.size() != k) continue;
        std::vector<LogicPtr> pats;
        for (const char* ps : R.premises) pats.push_back(P(ps));
        LogicPtr cp = P(R.conclusion);
        std::vector<size_t> idx(k);
        for (size_t i = 0; i < k; ++i) idx[i] = i;
        do {
            std::map<std::string, LogicPtr> bind;
            bool ok = true;
            for (size_t i = 0; i < k && ok; ++i) ok = matchPat(pats[i], prem[idx[i]], bind);
            if (ok && matchPat(cp, concl, bind)) return R.name;
        } while (std::next_permutation(idx.begin(), idx.end()));
    }
    return "";
}
}  // namespace

MathLogic::ArgumentResult MathLogic::checkArgument(const std::vector<std::string>& premises, const std::string& conclusion) const {
    std::vector<LogicPtr> prem;
    for (auto& s : premises) prem.push_back(parse(s));
    LogicPtr concl = parse(conclusion);
    std::vector<LogicPtr> all = prem; all.push_back(concl);
    ArgumentResult res;
    res.vars = unionVars(all);
    std::vector<std::vector<bool>> cols;
    for (auto& f : prem) cols.push_back(column(f, res.vars));
    std::vector<bool> cc = column(concl, res.vars);
    size_t n = res.vars.size();
    res.valid = true;
    for (ull r = 0; r < cc.size(); ++r) {
        bool allTrue = true;
        for (auto& c : cols) if (!c[r]) { allTrue = false; break; }
        if (allTrue && !cc[r]) {
            res.valid = false;
            if (res.counterexamples.size() < 16) {
                std::map<std::string, bool> m;
                for (size_t i = 0; i < n; ++i) m[res.vars[i]] = !((r >> (n - 1 - i)) & 1ULL);
                res.counterexamples.push_back(m);
            }
        }
    }
    res.rule = detectPattern(prem, concl, false);
    if (!res.valid) res.fallacy = detectPattern(prem, concl, true);
    return res;
}

std::string MathLogic::argumentReport(const std::vector<std::string>& premises, const std::string& conclusion) const {
    if (premises.empty()) throw std::invalid_argument("enter at least one premise");
    ArgumentResult r = checkArgument(premises, conclusion);
    std::vector<std::string> fs = premises; fs.push_back(conclusion);
    std::ostringstream o;
    for (size_t i = 0; i < premises.size(); ++i) o << "Premise " << i + 1 << ":  " << print(parse(premises[i]), Style::ASCII) << "\n";
    o << "Conclusion: " << print(parse(conclusion), Style::ASCII) << "\n\n";
    if ((int)r.vars.size() <= 8) {
        TruthTable t = truthTableOf(fs);
        std::vector<bool> mark(t.values.size());
        for (size_t row = 0; row < t.values.size(); ++row) {
            bool all = true;
            for (size_t c = 0; c < premises.size(); ++c) all = all && t.values[row][c];
            mark[row] = all;
        }
        o << formatTruthTable(t, true, 4096, &mark) << "\n";
        o << "(critical rows = rows where every premise is true; the argument is valid iff the conclusion is true in all of them)\n\n";
    }
    if (r.valid) {
        o << "VALID argument: the conclusion follows from the premises (premises |= conclusion).\n";
        o << "(the conditional  (P1 & P2 & ...) -> C  is a tautology)\n";
        if (!r.rule.empty()) o << "Form of reasoning: " << r.rule << "\n";
    } else {
        o << "INVALID argument: there " << (r.counterexamples.size() == 1 ? "is a counterexample" : "are counterexamples") << " (premises true, conclusion false):\n";
        for (auto& m : r.counterexamples) {
            o << "  ";
            bool first = true;
            for (auto& kv : m) { o << (first ? "" : ", ") << kv.first << "=" << (kv.second ? "T" : "F"); first = false; }
            o << "\n";
        }
        if (!r.fallacy.empty()) o << "Common fallacy: " << r.fallacy << "\n";
    }
    return o.str();
}

// =====================================================================
//  proofs by inference rules (forward chaining + conditional proof)
// =====================================================================
namespace {
struct Line { LogicPtr f; std::string why; int depth; };

LogicPtr negOf(const LogicPtr& f) { return f->kind == Kind::NOT ? f->a : mkNot(f); }
std::string key(const LogicPtr& f) { return print(f, MathLogic::Style::ASCII); }

struct Prover {
    std::vector<Line> lines;
    std::map<std::string, int> idx;       // formula -> line number (1-based)
    int depth = 0;
    static const size_t MAX_LINES = 160;

    bool have(const LogicPtr& f) const { return idx.count(key(f)) > 0; }
    int num(const LogicPtr& f) const { auto it = idx.find(key(f)); return it == idx.end() ? 0 : it->second; }
    bool add(const LogicPtr& f, const std::string& why) {
        std::string k = key(f);
        if (idx.count(k) || lines.size() >= MAX_LINES) return false;
        lines.push_back({f, why, depth});
        idx[k] = (int)lines.size();
        return true;
    }
    static std::string refs(int a, int b = 0, int c = 0) {
        std::string s = std::to_string(a);
        if (b) s += "," + std::to_string(b);
        if (c) s += "," + std::to_string(c);
        return s;
    }
    void closure() {
        for (int pass = 0; pass < 20; ++pass) {
            bool changed = false;
            size_t n = lines.size();
            for (size_t i = 0; i < n && lines.size() < MAX_LINES; ++i) {
                LogicPtr f = lines[i].f; int ni = (int)i + 1;
                switch (f->kind) {
                    case Kind::AND:
                        changed |= add(f->a, "Simplification " + refs(ni));
                        changed |= add(f->b, "Simplification " + refs(ni));
                        break;
                    case Kind::IFF:
                        changed |= add(mkNode(Kind::IMPLIES, f->a, f->b), "Biconditional " + refs(ni));
                        changed |= add(mkNode(Kind::IMPLIES, f->b, f->a), "Biconditional " + refs(ni));
                        break;
                    case Kind::NOT:
                        if (f->a->kind == Kind::NOT) changed |= add(f->a->a, "Double negation " + refs(ni));
                        break;
                    case Kind::IMPLIES: {
                        if (have(f->a)) changed |= add(f->b, "Modus Ponens " + refs(ni, num(f->a)));
                        if (have(negOf(f->b))) changed |= add(negOf(f->a), "Modus Tollens " + refs(ni, num(negOf(f->b))));
                        for (size_t j = 0; j < lines.size(); ++j) {
                            const LogicPtr& g = lines[j].f;
                            if (g->kind == Kind::IMPLIES && key(g->a) == key(f->b) && key(f) != key(g))
                                changed |= add(mkNode(Kind::IMPLIES, f->a, g->b), "Hypothetical Syllogism " + refs(ni, (int)j + 1));
                        }
                        break;
                    }
                    case Kind::OR: {
                        if (have(negOf(f->a))) changed |= add(f->b, "Disjunctive Syllogism " + refs(ni, num(negOf(f->a))));
                        if (have(negOf(f->b))) changed |= add(f->a, "Disjunctive Syllogism " + refs(ni, num(negOf(f->b))));
                        for (size_t j = 0; j < lines.size(); ++j) {
                            const LogicPtr& g = lines[j].f;
                            if (g->kind != Kind::OR) continue;
                            if (key(g->a) == key(negOf(f->a))) changed |= add(mkNode(Kind::OR, f->b, g->b), "Resolution " + refs(ni, (int)j + 1));
                            if (key(g->a) == key(negOf(f->b))) changed |= add(mkNode(Kind::OR, f->a, g->b), "Resolution " + refs(ni, (int)j + 1));
                            if (key(g->b) == key(negOf(f->a))) changed |= add(mkNode(Kind::OR, f->b, g->a), "Resolution " + refs(ni, (int)j + 1));
                            if (key(g->b) == key(negOf(f->b))) changed |= add(mkNode(Kind::OR, f->a, g->a), "Resolution " + refs(ni, (int)j + 1));
                        }
                        break;
                    }
                    default: break;
                }
            }
            if (!changed) break;
        }
    }
    bool prove(const LogicPtr& goal, int level) {
        closure();
        if (have(goal)) return true;
        if (level > 4) return false;
        switch (goal->kind) {
            case Kind::AND: {
                if (prove(goal->a, level + 1) && prove(goal->b, level + 1))
                    return add(goal, "Conjunction " + refs(num(goal->a), num(goal->b))) || have(goal);
                return false;
            }
            case Kind::OR: {
                if (prove(goal->a, level + 1)) return add(goal, "Addition " + refs(num(goal->a))) || have(goal);
                if (prove(goal->b, level + 1)) return add(goal, "Addition " + refs(num(goal->b))) || have(goal);
                return false;
            }
            case Kind::IMPLIES: {                                   // conditional proof
                Prover sub = *this;
                sub.depth = depth + 1;
                if (!sub.add(goal->a, "Assumption (for conditional proof)")) {
                    if (!sub.have(goal->a)) return false;
                }
                int assumeLine = sub.num(goal->a);
                if (!sub.prove(goal->b, level + 1)) return false;
                int concl = sub.num(goal->b);
                sub.depth = depth;
                *this = sub;
                return add(goal, "Conditional proof " + refs(assumeLine, concl)) || have(goal);
            }
            case Kind::IFF: {
                LogicPtr fwd = mkNode(Kind::IMPLIES, goal->a, goal->b), bwd = mkNode(Kind::IMPLIES, goal->b, goal->a);
                if (prove(fwd, level + 1) && prove(bwd, level + 1))
                    return add(goal, "Biconditional introduction " + refs(num(fwd), num(bwd))) || have(goal);
                return false;
            }
            default: return false;
        }
    }
};
}  // namespace

std::string MathLogic::proveReport(const std::vector<std::string>& premises, const std::string& goalText) const {
    if (premises.empty()) throw std::invalid_argument("enter at least one premise");
    LogicPtr goal = parse(goalText);
    Prover pr;
    for (auto& s : premises) pr.add(parse(s), "Premise");
    bool ok = pr.prove(goal, 0);
    std::ostringstream o;
    o << "Goal: " << print(goal, Style::ASCII) << "\n\n";
    for (size_t i = 0; i < pr.lines.size(); ++i) {
        o << (i + 1 < 10 ? " " : "") << i + 1 << ". ";
        for (int d = 0; d < pr.lines[i].depth; ++d) o << "|  ";
        o << print(pr.lines[i].f, Style::ASCII) << "     [" << pr.lines[i].why << "]\n";
    }
    o << "\n";
    if (ok) {
        o << "PROVED: the goal follows from the premises by the rules above.\n";
    } else {
        o << "No proof found with the basic rules (MP, MT, HS, DS, Simp, Conj, Add, Resolution, Bicond., Conditional proof).\n";
        bool valid = checkArgument(premises, goalText).valid;
        o << (valid ? "However, the truth table shows the argument IS valid (semantic consequence), so a longer proof exists.\n"
                    : "The truth table also shows the argument is NOT valid, so no proof exists.\n");
    }
    return o.str();
}

// =====================================================================
//  normal forms / Boolean algebra
// =====================================================================
namespace {
LogicPtr nnfRec(const LogicPtr& n, bool neg) {
    switch (n->kind) {
        case Kind::CONST: return mkConst(neg ? !n->value : n->value);
        case Kind::VAR: return neg ? mkNot(n) : n;
        case Kind::NOT: return nnfRec(n->a, !neg);
        case Kind::AND: return neg ? mkNode(Kind::OR, nnfRec(n->a, true), nnfRec(n->b, true))
                                   : mkNode(Kind::AND, nnfRec(n->a, false), nnfRec(n->b, false));
        case Kind::OR: return neg ? mkNode(Kind::AND, nnfRec(n->a, true), nnfRec(n->b, true))
                                  : mkNode(Kind::OR, nnfRec(n->a, false), nnfRec(n->b, false));
        case Kind::XOR: return nnfRec(mkNode(Kind::OR, mkNode(Kind::AND, n->a, mkNot(n->b)), mkNode(Kind::AND, mkNot(n->a), n->b)), neg);
        case Kind::NAND: return nnfRec(mkNot(mkNode(Kind::AND, n->a, n->b)), neg);
        case Kind::NOR: return nnfRec(mkNot(mkNode(Kind::OR, n->a, n->b)), neg);
        case Kind::IMPLIES: return nnfRec(mkNode(Kind::OR, mkNot(n->a), n->b), neg);
        case Kind::IFF: return nnfRec(mkNode(Kind::AND, mkNode(Kind::IMPLIES, n->a, n->b), mkNode(Kind::IMPLIES, n->b, n->a)), neg);
        case Kind::CMP: {
            if (!neg) return n;
            const std::string& o = n->name;
            std::string no = o == "=" ? "!=" : o == "!=" ? "=" : o == "<" ? ">=" : o == "<=" ? ">" : o == ">" ? "<=" : "<";
            return mkCmp(no, n->lhs, n->rhs);
        }
        case Kind::FORALL: return mkQuant(neg ? Kind::EXISTS : Kind::FORALL, n->name, n->universeText, nnfRec(n->a, neg));
        case Kind::EXISTS: return mkQuant(neg ? Kind::FORALL : Kind::EXISTS, n->name, n->universeText, nnfRec(n->a, neg));
    }
    return n;
}

struct Imp { ull bits, dc; };

std::vector<Imp> qmPrimes(const std::vector<ull>& minterms) {
    std::set<std::pair<ull, ull>> cur;
    for (ull m : minterms) cur.insert({m, 0});
    std::vector<Imp> primes;
    while (!cur.empty()) {
        std::vector<std::pair<ull, ull>> v(cur.begin(), cur.end());
        std::vector<char> used(v.size(), 0);
        std::set<std::pair<ull, ull>> next;
        for (size_t i = 0; i < v.size(); ++i)
            for (size_t j = i + 1; j < v.size(); ++j) {
                if (v[i].second != v[j].second) continue;
                ull diff = v[i].first ^ v[j].first;
                if (diff && !(diff & (diff - 1))) {
                    used[i] = used[j] = 1;
                    next.insert({v[i].first & ~diff, v[i].second | diff});
                }
            }
        for (size_t i = 0; i < v.size(); ++i) if (!used[i]) primes.push_back({v[i].first, v[i].second});
        cur = next;
    }
    return primes;
}
bool covers(const Imp& im, ull m) { return (m & ~im.dc) == im.bits; }

std::vector<Imp> qmCover(const std::vector<ull>& minterms, const std::vector<Imp>& primes) {
    std::vector<Imp> chosen;
    std::set<ull> left(minterms.begin(), minterms.end());
    std::vector<char> take(primes.size(), 0);
    auto choose = [&](size_t i) {
        if (take[i]) return;
        take[i] = 1; chosen.push_back(primes[i]);
        for (auto it = left.begin(); it != left.end();) if (covers(primes[i], *it)) it = left.erase(it); else ++it;
    };
    for (ull m : minterms) {                           // essential prime implicants
        int cnt = 0; size_t which = 0;
        for (size_t i = 0; i < primes.size(); ++i) if (covers(primes[i], m)) { ++cnt; which = i; }
        if (cnt == 1) choose(which);
    }
    while (!left.empty()) {                            // greedy cover for the rest
        size_t best = 0; int bestCnt = -1;
        for (size_t i = 0; i < primes.size(); ++i) {
            if (take[i]) continue;
            int c = 0; for (ull m : left) if (covers(primes[i], m)) ++c;
            if (c > bestCnt) { bestCnt = c; best = i; }
        }
        if (bestCnt <= 0) break;
        choose(best);
    }
    return chosen;
}
std::string termStr(const Imp& im, const std::vector<std::string>& vars, bool clause) {
    size_t n = vars.size();
    std::vector<std::string> lits;
    for (size_t i = 0; i < n; ++i) {
        size_t pos = n - 1 - i;
        if ((im.dc >> pos) & 1ULL) continue;
        bool val = (im.bits >> pos) & 1ULL;
        if (clause) val = !val;
        lits.push_back(val ? vars[i] : "~" + vars[i]);
    }
    if (lits.empty()) return clause ? "F" : "T";
    std::string s;
    for (size_t i = 0; i < lits.size(); ++i) s += (i ? (clause ? " | " : " & ") : "") + lits[i];
    return s;
}
std::string join(std::vector<std::string> parts, bool wrapEach, const char* sep) {
    std::sort(parts.begin(), parts.end());
    std::string s;
    for (size_t i = 0; i < parts.size(); ++i) {
        bool multi = parts[i].find(' ') != std::string::npos;
        s += (i ? sep : "") + ((wrapEach && multi && parts.size() > 1) ? "(" + parts[i] + ")" : parts[i]);
    }
    return s;
}
}  // namespace

std::string MathLogic::nnf(const std::string& formula) const { return print(nnfRec(parse(formula), false), Style::ASCII); }

namespace {
struct Rows { std::vector<std::string> vars; std::vector<ull> minterms, maxterms; ull total = 0; };
}  // namespace

static void rowsOf(const MathLogic& ml, const std::string& formula, std::vector<std::string>& vars,
                   std::vector<unsigned long long>& mins, std::vector<unsigned long long>& maxs, unsigned long long& total) {
    MathLogic::Classification c = ml.classify(formula);
    vars = c.vars; mins = c.minterms; total = c.rows;
    std::set<ull> in(mins.begin(), mins.end());
    maxs.clear();
    for (ull m = 0; m < total; ++m) if (!in.count(m)) maxs.push_back(m);
}

std::string MathLogic::canonicalDNF(const std::string& formula) const {
    std::vector<std::string> vars; std::vector<ull> mins, maxs; ull total;
    rowsOf(*this, formula, vars, mins, maxs, total);
    if (mins.empty()) return "F";
    if (vars.empty()) return "T";
    size_t n = vars.size();
    std::vector<std::string> terms;
    for (ull m : mins) { Imp im{m, 0}; (void)n; terms.push_back(termStr(im, vars, false)); }
    std::string s;
    std::sort(terms.begin(), terms.end());
    for (size_t i = 0; i < terms.size(); ++i) s += (i ? " | " : "") + (terms.size() > 1 && n > 1 ? "(" + terms[i] + ")" : terms[i]);
    return s;
}
std::string MathLogic::canonicalCNF(const std::string& formula) const {
    std::vector<std::string> vars; std::vector<ull> mins, maxs; ull total;
    rowsOf(*this, formula, vars, mins, maxs, total);
    if (maxs.empty()) return "T";
    if (vars.empty()) return "F";
    size_t n = vars.size();
    std::vector<std::string> cl;
    for (ull m : maxs) { Imp im{m, 0}; cl.push_back(termStr(im, vars, true)); }
    std::sort(cl.begin(), cl.end());
    std::string s;
    for (size_t i = 0; i < cl.size(); ++i) s += (i ? " & " : "") + (cl.size() > 1 && n > 1 ? "(" + cl[i] + ")" : cl[i]);
    return s;
}
std::string MathLogic::minimalDNF(const std::string& formula) const {
    std::vector<std::string> vars; std::vector<ull> mins, maxs; ull total;
    rowsOf(*this, formula, vars, mins, maxs, total);
    if (mins.empty()) return "F";
    if (mins.size() == total) return "T";
    if (vars.size() > 12) throw std::invalid_argument("minimal forms are limited to 12 variables");
    auto primes = qmPrimes(mins);
    auto cover = qmCover(mins, primes);
    std::vector<std::string> parts;
    for (auto& im : cover) parts.push_back(termStr(im, vars, false));
    return join(parts, true, " | ");
}
std::string MathLogic::minimalCNF(const std::string& formula) const {
    std::vector<std::string> vars; std::vector<ull> mins, maxs; ull total;
    rowsOf(*this, formula, vars, mins, maxs, total);
    if (maxs.empty()) return "T";
    if (maxs.size() == total) return "F";
    if (vars.size() > 12) throw std::invalid_argument("minimal forms are limited to 12 variables");
    auto primes = qmPrimes(maxs);
    auto cover = qmCover(maxs, primes);
    std::vector<std::string> parts;
    for (auto& im : cover) parts.push_back(termStr(im, vars, true));
    return join(parts, true, " & ");
}

std::string MathLogic::normalFormsReport(const std::string& formula) const {
    LogicPtr f = parse(formula);
    std::vector<std::string> vars; std::vector<ull> mins, maxs; ull total;
    rowsOf(*this, formula, vars, mins, maxs, total);
    std::ostringstream o;
    o << "Formula: " << print(f, Style::ASCII) << "\n";
    o << "Variables: ";
    for (size_t i = 0; i < vars.size(); ++i) o << (i ? ", " : "") << vars[i];
    if (vars.empty()) o << "(none)";
    o << "\n\n";
    o << "Negation normal form (NNF):\n  " << nnf(formula) << "\n\n";
    o << "Canonical DNF (sum of minterms):\n  " << canonicalDNF(formula) << "\n";
    o << "  m(" << joinList(mins) << ")\n\n";
    o << "Canonical CNF (product of maxterms):\n  " << canonicalCNF(formula) << "\n";
    o << "  M(" << joinList(maxs) << ")\n\n";
    if (vars.size() <= 12) {
        o << "Minimal DNF (Quine-McCluskey):\n  " << minimalDNF(formula) << "\n\n";
        o << "Minimal CNF:\n  " << minimalCNF(formula) << "\n";
    } else o << "(minimal forms are limited to 12 variables)\n";
    return o.str();
}

// =====================================================================
//  other reports
// =====================================================================
std::string MathLogic::conditionalReport(const std::string& formula) const {
    LogicPtr f = parse(formula);
    if (f->kind != Kind::IMPLIES) throw std::invalid_argument("enter a conditional of the form  p -> q  (the main connective must be ->)");
    LogicPtr p = f->a, q = f->b;
    LogicPtr conv = mkNode(Kind::IMPLIES, q, p);
    LogicPtr inv = mkNode(Kind::IMPLIES, negOf(p), negOf(q));
    LogicPtr contra = mkNode(Kind::IMPLIES, negOf(q), negOf(p));
    LogicPtr neg = mkNode(Kind::AND, p, negOf(q));
    std::string sp = print(p, Style::ASCII), sq = print(q, Style::ASCII);
    std::vector<std::string> strs = {print(f, Style::ASCII), print(conv, Style::ASCII), print(inv, Style::ASCII), print(contra, Style::ASCII), print(mkNot(f), Style::ASCII), print(neg, Style::ASCII)};
    std::ostringstream o;
    o << "Conditional      (p -> q)         " << strs[0] << "\n";
    o << "Converse         (q -> p)         " << strs[1] << "\n";
    o << "Inverse          (~p -> ~q)       " << strs[2] << "\n";
    o << "Contrapositive   (~q -> ~p)       " << strs[3] << "\n";
    o << "Negation         ~(p -> q)  =  p & ~q   " << strs[5] << "\n\n";
    auto eq = [&](const LogicPtr& a, const LogicPtr& b) { return equivalent(print(a, Style::ASCII), print(b, Style::ASCII)); };
    o << "Conditional  ==  Contrapositive ?  " << (eq(f, contra) ? "YES (always equivalent)" : "no") << "\n";
    o << "Converse     ==  Inverse        ?  " << (eq(conv, inv) ? "YES (always equivalent)" : "no") << "\n";
    o << "Conditional  ==  Converse       ?  " << (eq(f, conv) ? "yes (for this formula)" : "NO (a conditional and its converse differ)") << "\n";
    o << "~(p -> q)    ==  p & ~q         ?  " << (eq(mkNot(f), neg) ? "YES" : "no") << "\n\n";
    o << "Reading:  p is a SUFFICIENT condition for q;  q is a NECESSARY condition for p.\n";
    o << "          'p only if q'  means  p -> q ;   'p if and only if q'  means  p <-> q.\n";
    if (variables(f).size() <= 6) {
        o << "\n" << formatTruthTable(truthTableOf({strs[0], strs[1], strs[2], strs[3]}));
    }
    return o.str();
}

std::string MathLogic::stepsReport(const std::string& formula, const std::map<std::string, bool>& assignment) const {
    LogicPtr f = parse(formula);
    auto vars = variables(f);
    std::string missing;
    for (auto& v : vars) if (!assignment.count(v)) missing += (missing.empty() ? "" : ", ") + v;
    if (!missing.empty()) throw std::invalid_argument("give a truth value for: " + missing + "   (e.g.  p=T, q=F)");
    std::vector<LogicPtr> subs; std::set<std::string> seen;
    collectSubs(f, subs, seen);
    std::ostringstream o;
    o << "Formula: " << print(f, Style::ASCII) << "\n\nGiven:\n";
    for (auto& v : vars) o << "  " << v << " = " << (assignment.at(v) ? "T" : "F") << "\n";
    o << "\nStep by step (inner parts first):\n";
    Env env; env.props = &assignment;
    int k = 1;
    for (auto& s : subs) o << "  " << k++ << ". " << print(s, Style::ASCII) << "  =  " << (eval(s, env) ? "T" : "F") << "\n";
    bool res = eval(f, env);
    o << "\nResult: " << (res ? "TRUE" : "FALSE") << "\n";
    return o.str();
}

std::string MathLogic::quantifierReport(const std::string& formula, const std::map<std::string, bool>& assignment) const {
    LogicPtr f = parse(formula);
    std::ostringstream o;
    o << "Statement: " << print(f, Style::ASCII) << "\n";
    Env env; env.props = &assignment;
    if (f->kind == Kind::FORALL || f->kind == Kind::EXISTS) {
        bool all = f->kind == Kind::FORALL;
        std::vector<double> uni = universe(f->universeText);
        char v = f->name[0];
        o << "Quantifier: " << (all ? "for ALL" : "there EXISTS") << " " << v << " in the universe of " << uni.size() << " element(s)\n\n";
        size_t trueCount = 0, shown = 0;
        int firstTrue = -1, firstFalse = -1;
        for (size_t i = 0; i < uni.size(); ++i) {
            env.nums[v] = uni[i];
            bool b = eval(f->a, env);
            if (b) { ++trueCount; if (firstTrue < 0) firstTrue = (int)i; }
            else if (firstFalse < 0) firstFalse = (int)i;
            if (shown < 60) { ++shown; char buf[64]; snprintf(buf, sizeof buf, "%.10g", uni[i]); o << "  " << v << " = " << buf << "  :  " << (b ? "T" : "F") << "\n"; }
        }
        if (uni.size() > shown) o << "  ... (" << uni.size() - shown << " more)\n";
        o << "\nTrue for " << trueCount << " of " << uni.size() << " elements.\n";
        bool value = all ? (trueCount == uni.size()) : (trueCount > 0);
        o << "Truth value of the statement: " << (value ? "TRUE" : "FALSE") << "\n";
        char buf[64];
        if (all && firstFalse >= 0) { snprintf(buf, sizeof buf, "%.10g", uni[firstFalse]); o << "Counterexample: " << v << " = " << buf << "\n"; }
        if (!all && firstTrue >= 0) { snprintf(buf, sizeof buf, "%.10g", uni[firstTrue]); o << "Witness: " << v << " = " << buf << "\n"; }
    } else {
        bool value = eval(f, env);
        o << "Truth value: " << (value ? "TRUE" : "FALSE") << "\n";
    }
    LogicPtr neg = nnfRec(f, true);
    o << "\nNegation (pushed inward):\n  " << print(neg, Style::ASCII) << "\n";
    o << "  = " << (eval(neg, env) ? "TRUE" : "FALSE") << "\n";
    o << "\nRules:  ~(forall x P(x))  <=>  exists x ~P(x)      ~(exists x P(x))  <=>  forall x ~P(x)\n";
    return o.str();
}

// =====================================================================
//  reference texts
// =====================================================================
std::string MathLogic::lawsText() {
    std::ostringstream o;
    o << "EQUIVALENCE LAWS  (each line:  name :  left  <=>  right)\n\n";
    for (const LawSpec& L : LAWS) o << "  " << L.name << ":\n      " << L.lhs << "   <=>   " << L.rhs << "\n";
    o << "\nQUANTIFIER LAWS (domain of discourse D)\n";
    o << "  ~(forall x P(x))   <=>   exists x ~P(x)\n";
    o << "  ~(exists x P(x))   <=>   forall x ~P(x)\n";
    o << "  forall x (P(x) & Q(x))  <=>  (forall x P(x)) & (forall x Q(x))\n";
    o << "  exists x (P(x) | Q(x))  <=>  (exists x P(x)) | (exists x Q(x))\n";
    o << "  (forall x P(x))  ->  (exists x P(x))      when D is not empty\n";
    return o.str();
}
std::string MathLogic::rulesText() {
    std::ostringstream o;
    o << "RULES OF INFERENCE  (premises  |-  conclusion)\n\n";
    for (const RuleSpec& R : RULES) {
        if (R.fallacy) continue;
        o << "  " << R.name << ":\n      ";
        for (size_t i = 0; i < R.premises.size(); ++i) o << (i ? ",   " : "") << R.premises[i];
        o << "   |-   " << R.conclusion << "\n";
    }
    o << "\nCOMMON FALLACIES (invalid forms)\n";
    for (const RuleSpec& R : RULES) {
        if (!R.fallacy) continue;
        o << "  " << R.name << ":\n      ";
        for (size_t i = 0; i < R.premises.size(); ++i) o << (i ? ",   " : "") << R.premises[i];
        o << "   |-/-   " << R.conclusion << "\n";
    }
    return o.str();
}
std::string MathLogic::connectivesText() {
    return
"LOGICAL CONNECTIVES AND TRUTH RULES\n"
"\n"
"  Name                 Write as                 True when ...\n"
"  Negation (NOT)       ~p   !p   not p          p is false\n"
"  Conjunction (AND)    p & q   p and q          both p and q are true\n"
"  Disjunction (OR)     p | q   p or q           at least one is true (inclusive or)\n"
"  Conditional          p -> q   p implies q     false only when p is true and q is false\n"
"  Biconditional        p <-> q  p iff q         p and q have the same truth value\n"
"  Exclusive or (XOR)   p xor q                  exactly one is true\n"
"  NAND                 p nand q                 not both are true\n"
"  NOR                  p nor q                  both are false\n"
"\n"
"  p q | ~p | p&q | p|q | p->q | p<->q | p xor q | p nand q | p nor q\n"
"  T T |  F |  T  |  T  |  T   |   T    |    F    |    F     |   F\n"
"  T F |  F |  F  |  T  |  F   |   F    |    T    |    T     |   F\n"
"  F T |  T |  F  |  T  |  T   |   F    |    T    |    T     |   F\n"
"  F F |  T |  F  |  F  |  T   |   T    |    F    |    T     |   T\n"
"\n"
"  Precedence (high to low): NOT, AND/NAND, XOR, OR/NOR, IMPLIES (right assoc.), IFF\n"
"  Constants: T (true), F (false).  Unicode symbols are accepted too (not and or -> <->).\n"
"  A proposition is a statement that is exactly one of true or false.\n"
"  Tautology: always true.  Contradiction: always false.  Contingency: neither.\n"
"  Two statements are equivalent when they have the same truth table.\n"
"  An argument is valid when the conclusion is true in every row where all premises are true.\n";
}
std::string MathLogic::overviewText() {
    return
"MATHEMATICAL LOGIC - OVERVIEW  (summary after Wikipedia: Mathematical logic)\n"
"\n"
"What it is\n"
"  The study of formal logic within mathematics. Also called symbolic logic, 'logistic',\n"
"  the algebra of logic or formal logic. It studies the expressive and deductive power of\n"
"  formal systems and gives tools for the foundations of mathematics.\n"
"\n"
"Main areas (Handbook of Mathematical Logic, 1977)\n"
"  1. Set theory            - sets, cardinals/ordinals, axioms (ZF, ZFC), axiom of choice,\n"
"                             continuum hypothesis.\n"
"  2. Model theory          - models of theories; structures that interpret formulas.\n"
"  3. Recursion theory      - computability: Turing machines, lambda calculus, undecidability.\n"
"  4. Proof theory and constructive mathematics - formal proofs, natural deduction, sequent\n"
"                             calculus, intuitionistic logic.\n"
"\n"
"Formal systems\n"
"  Propositional logic and first-order logic are the most studied. Others: second-order and\n"
"  higher-order logic, infinitary logic, and non-classical logics such as intuitionistic,\n"
"  modal and fuzzy logic. Algebraic logic uses Boolean algebras for classical propositional\n"
"  logic (truth values) and Heyting algebras for intuitionistic logic.\n"
"\n"
"Semantics and syntax\n"
"  Semantics: truth tables, models, logical consequence (premises |= conclusion).\n"
"  Syntax / proof theory: derivations by inference rules (premises |- conclusion).\n"
"  For first-order logic Godel's completeness theorem links the two: |= and |- coincide.\n"
"\n"
"Milestones\n"
"  1847-1854  Boole, De Morgan: algebra of logic.        1879  Frege, Begriffsschrift (quantifiers).\n"
"  1889  Peano axioms for arithmetic.                     1901  Russell's paradox.\n"
"  1908  Zermelo's set axioms (later ZF).                 1910-13  Principia Mathematica.\n"
"  1915/1920  Loewenheim-Skolem theorem.                  1929  Godel: completeness, compactness.\n"
"  1931  Godel's incompleteness theorems.                 1936  Church, Turing: Entscheidungsproblem\n"
"                                                               is unsolvable (halting problem).\n"
"  1936  Gentzen: consistency of arithmetic.              1963  Cohen: continuum hypothesis is\n"
"                                                               independent of ZF (forcing).\n"
"  1970  Matiyasevich: Hilbert's tenth problem unsolvable.\n"
"\n"
"Key theorems in one line each\n"
"  Completeness:    a first-order sentence true in every model of the axioms is provable from them.\n"
"  Compactness:     a set of sentences has a model iff every finite subset has a model.\n"
"  Loewenheim-Skolem: a countable first-order theory with an infinite model has models of every\n"
"                   infinite size, so first-order axioms cannot pin down the naturals up to isomorphism.\n"
"  Incompleteness:  a consistent, effective theory containing arithmetic has true but unprovable\n"
"                   sentences and cannot prove its own consistency.\n"
"\n"
"What this module implements\n"
"  Propositional semantics (truth tables, tautology / contradiction / contingency, equivalence),\n"
"  arguments and inference rules (a small natural-deduction prover), Boolean normal forms and\n"
"  Quine-McCluskey simplification, and a finite-domain fragment of first-order logic\n"
"  (comparisons and quantifiers over listed universes).\n";
}

// =====================================================================
//  ADVANCED LOGIC - first-order core
// =====================================================================
namespace fol {

struct Term;
struct Formula;
typedef std::shared_ptr<const Term> TermP;
typedef std::shared_ptr<const Formula> FormP;
struct Term { enum K { VAR, CONST, FUNC } k = CONST; std::string name; std::vector<TermP> args; };
struct Formula { enum K { PRED, EQ, TOP, BOT, NOT, AND, OR, IMP, IFF, ALL, EX } k = TOP; std::string name; std::vector<TermP> args; FormP a, b; };

TermP tVar(const std::string& n) { auto t = std::make_shared<Term>(); t->k = Term::VAR; t->name = n; return t; }
TermP tConst(const std::string& n) { auto t = std::make_shared<Term>(); t->k = Term::CONST; t->name = n; return t; }
TermP tFunc(const std::string& n, const std::vector<TermP>& a) { auto t = std::make_shared<Term>(); t->k = Term::FUNC; t->name = n; t->args = a; return t; }
FormP fPred(const std::string& n, const std::vector<TermP>& a) { auto f = std::make_shared<Formula>(); f->k = Formula::PRED; f->name = n; f->args = a; return f; }
FormP fEq(const TermP& x, const TermP& y) { auto f = std::make_shared<Formula>(); f->k = Formula::EQ; f->args = {x, y}; return f; }
FormP fConst(bool v) { auto f = std::make_shared<Formula>(); f->k = v ? Formula::TOP : Formula::BOT; return f; }
FormP fNot(const FormP& a) { auto f = std::make_shared<Formula>(); f->k = Formula::NOT; f->a = a; return f; }
FormP fBin(Formula::K k, const FormP& a, const FormP& b) { auto f = std::make_shared<Formula>(); f->k = k; f->a = a; f->b = b; return f; }
FormP fQ(Formula::K k, const std::string& v, const FormP& body) { auto f = std::make_shared<Formula>(); f->k = k; f->name = v; f->a = body; return f; }

bool isVarName(const std::string& s) {
    if (s.empty() || !strchr("uvwxyz", s[0])) return false;
    for (size_t i = 1; i < s.size(); ++i) if (!(isdigit((unsigned char)s[i]) || s[i] == '_' || s[i] == '\'')) return false;
    return true;
}

// ---------- printing ----------
std::string tstr(const TermP& t) {
    if (t->k != Term::FUNC) return t->name;
    std::string s = t->name + "(";
    for (size_t i = 0; i < t->args.size(); ++i) s += (i ? ", " : "") + tstr(t->args[i]);
    return s + ")";
}
int fprec(const FormP& f) {
    switch (f->k) {
        case Formula::IFF: return 1;
        case Formula::IMP: return 2;
        case Formula::OR: return 3;
        case Formula::AND: return 4;
        case Formula::NOT: case Formula::ALL: case Formula::EX: return 5;
        default: return 6;
    }
}
std::string fstr(const FormP& f) {
    switch (f->k) {
        case Formula::TOP: return "TOP";
        case Formula::BOT: return "BOT";
        case Formula::PRED: {
            if (f->args.empty()) return f->name;
            std::string s = f->name + "(";
            for (size_t i = 0; i < f->args.size(); ++i) s += (i ? ", " : "") + tstr(f->args[i]);
            return s + ")";
        }
        case Formula::EQ: return tstr(f->args[0]) + " = " + tstr(f->args[1]);
        case Formula::NOT: {
            if (f->a->k == Formula::EQ) return tstr(f->a->args[0]) + " != " + tstr(f->a->args[1]);
            std::string t = fstr(f->a);
            return "~" + (fprec(f->a) < 5 ? "(" + t + ")" : t);
        }
        case Formula::ALL: case Formula::EX: {
            std::string t = fstr(f->a);
            return std::string(f->k == Formula::ALL ? "forall " : "exists ") + f->name + " " + (fprec(f->a) < 5 ? "(" + t + ")" : t);
        }
        default: break;
    }
    const char* op = f->k == Formula::AND ? " & " : f->k == Formula::OR ? " | " : f->k == Formula::IMP ? " -> " : " <-> ";
    int p = fprec(f);
    auto wrapc = [&](const FormP& c, bool right) {
        std::string t = fstr(c);
        int cp = fprec(c);
        bool paren = cp < p;
        if (cp == p) paren = (f->k == Formula::IMP) ? !right : (right || c->k != f->k);
        return paren ? "(" + t + ")" : t;
    };
    return wrapc(f->a, false) + op + wrapc(f->b, true);
}

// ---------- parser ----------
class Parser {
public:
    explicit Parser(std::string text) : s(std::move(text)) {}
    FormP parseFormula() {
        FormP f = parseIff();
        ws();
        if (p < s.size()) fail(std::string("unexpected '") + s[p] + "'");
        return f;
    }
    TermP parseTermOnly() {
        TermP t = parseTerm();
        ws();
        if (p < s.size()) fail(std::string("unexpected '") + s[p] + "'");
        return t;
    }
    // atom (predicate) or term, used by unification
    bool parseAtomOrTerm(FormP& atom, TermP& term) {
        ws();
        if (p < s.size() && isupper((unsigned char)s[p])) { atom = parseAtom(); ws(); if (p < s.size()) fail("unexpected input"); return true; }
        term = parseTermOnly();
        return false;
    }
private:
    std::string s;
    size_t p = 0;
    [[noreturn]] void fail(const std::string& m) const { throw std::invalid_argument(m + " (at position " + std::to_string(p + 1) + ")"); }
    void ws() { while (p < s.size() && isspace((unsigned char)s[p])) ++p; }
    bool starts(const char* t) const { return s.compare(p, strlen(t), t) == 0; }
    std::string wordAt(size_t q) const {
        if (q >= s.size() || !(isalpha((unsigned char)s[q]) || isdigit((unsigned char)s[q]))) return "";
        size_t e = q;
        while (e < s.size() && (isalnum((unsigned char)s[e]) || s[e] == '_' || s[e] == '\'')) ++e;
        return s.substr(q, e - q);
    }
    bool eatWord(const char* w) {
        ws();
        std::string x = wordAt(p);
        if (!x.empty() && lower(x) == w) { p += strlen(w); return true; }
        return false;
    }
    FormP parseIff() {
        FormP l = parseImp();
        for (;;) {
            ws();
            if (starts("<->") || starts("<=>")) { p += 3; l = fBin(Formula::IFF, l, parseImp()); }
            else if (eatWord("iff")) l = fBin(Formula::IFF, l, parseImp());
            else break;
        }
        return l;
    }
    FormP parseImp() {
        FormP l = parseOr();
        ws();
        if (starts("->") || starts("=>")) { p += 2; return fBin(Formula::IMP, l, parseImp()); }
        if (eatWord("implies")) return fBin(Formula::IMP, l, parseImp());
        return l;
    }
    FormP parseOr() {
        FormP l = parseAnd();
        for (;;) {
            ws();
            if (starts("||")) { p += 2; l = fBin(Formula::OR, l, parseAnd()); }
            else if (p < s.size() && s[p] == '|') { ++p; l = fBin(Formula::OR, l, parseAnd()); }
            else if (eatWord("or")) l = fBin(Formula::OR, l, parseAnd());
            else break;
        }
        return l;
    }
    FormP parseAnd() {
        FormP l = parseUnary();
        for (;;) {
            ws();
            if (starts("&&")) { p += 2; l = fBin(Formula::AND, l, parseUnary()); }
            else if (p < s.size() && s[p] == '&') { ++p; l = fBin(Formula::AND, l, parseUnary()); }
            else if (eatWord("and")) l = fBin(Formula::AND, l, parseUnary());
            else break;
        }
        return l;
    }
    FormP parseUnary() {
        ws();
        if (p < s.size() && (s[p] == '~' || (s[p] == '!' && !starts("!=")))) { ++p; return fNot(parseUnary()); }
        if (eatWord("not")) return fNot(parseUnary());
        bool all = eatWord("forall"), ex = !all && eatWord("exists");
        if (all || ex) return parseQuantifier(all ? Formula::ALL : Formula::EX);
        return parseAtom();
    }
    FormP parseQuantifier(Formula::K k) {
        std::vector<std::string> vars;
        for (;;) {
            ws();
            std::string w = wordAt(p);
            if (w.empty() || !isVarName(w)) fail("expected a variable (u v w x y z) after the quantifier");
            p += w.size();
            vars.push_back(w);
            ws();
            if (p < s.size() && s[p] == ',') { ++p; continue; }
            break;
        }
        ws();
        FormP body;
        if (p < s.size() && (s[p] == '.' || s[p] == ':')) { ++p; body = parseIff(); }
        else body = parseUnary();
        for (size_t i = vars.size(); i-- > 0;) body = fQ(k, vars[i], body);
        return body;
    }
    TermP makeSimple(const std::string& w) const {
        return isVarName(w) ? tVar(w) : tConst(w);
    }
    TermP parseTerm() {
        ws();
        std::string w = wordAt(p);
        if (w.empty()) fail("expected a term");
        p += w.size();
        ws();
        if (p < s.size() && s[p] == '(') {
            if (isupper((unsigned char)w[0])) { p -= w.size(); fail("function symbols start with a lowercase letter"); }
            if (isVarName(w)) fail("'" + w + "' is a variable and cannot be applied to arguments");
            ++p;
            std::vector<TermP> args;
            for (;;) {
                args.push_back(parseTerm());
                ws();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                break;
            }
            if (p >= s.size() || s[p] != ')') fail("expected ')'");
            ++p;
            return tFunc(w, args);
        }
        return makeSimple(w);
    }
    FormP parseAtom() {
        ws();
        if (p >= s.size()) fail("unexpected end of formula");
        if (s[p] == '(') {
            ++p;
            FormP f = parseIff();
            ws();
            if (p >= s.size() || s[p] != ')') fail("expected ')'");
            ++p;
            return f;
        }
        std::string w = wordAt(p);
        if (w.empty()) fail(std::string("unexpected '") + s[p] + "'");
        if (isupper((unsigned char)w[0])) {                        // predicate
            if (w == "TOP") { p += 3; return fConst(true); }
            if (w == "BOT") { p += 3; return fConst(false); }
            size_t after = p + w.size(), q = after;
            while (q < s.size() && isspace((unsigned char)s[q])) ++q;
            p = after;
            if (q < s.size() && s[q] == '(') {
                p = q + 1;
                std::vector<TermP> args;
                for (;;) {
                    args.push_back(parseTerm());
                    ws();
                    if (p < s.size() && s[p] == ',') { ++p; continue; }
                    break;
                }
                if (p >= s.size() || s[p] != ')') fail("expected ')'");
                ++p;
                return fPred(w, args);
            }
            bool splittable = w.size() > 1 && w.size() <= 4;
            for (size_t i = 1; i < w.size() && splittable; ++i) if (!islower((unsigned char)w[i])) splittable = false;
            if (splittable) {                                      // Rxy  ==  R(x, y)
                std::vector<TermP> args;
                for (size_t i = 1; i < w.size(); ++i) args.push_back(makeSimple(std::string(1, w[i])));
                return fPred(std::string(1, w[0]), args);
            }
            return fPred(w, {});
        }
        TermP t1 = parseTerm();                                    // equality
        ws();
        bool neq = false;
        if (starts("!=")) { p += 2; neq = true; }
        else if (p < s.size() && s[p] == '=' && !starts("=>")) ++p;
        else fail("expected '=' after the term (a formula must be a predicate or an equation)");
        TermP t2 = parseTerm();
        FormP e = fEq(t1, t2);
        return neq ? fNot(e) : e;
    }
};

FormP parseF(const std::string& text) {
    std::string t = MathLogic::normalizeSymbols(text);
    // the Unicode table inserts words with spaces; nothing else to do
    return Parser(t).parseFormula();
}

// ---------- analysis ----------
void termVars(const TermP& t, std::set<std::string>& out) {
    if (t->k == Term::VAR) out.insert(t->name);
    for (auto& a : t->args) termVars(a, out);
}
void freeVars(const FormP& f, std::set<std::string>& out) {
    switch (f->k) {
        case Formula::PRED: case Formula::EQ: for (auto& t : f->args) termVars(t, out); break;
        case Formula::NOT: freeVars(f->a, out); break;
        case Formula::ALL: case Formula::EX: { std::set<std::string> in; freeVars(f->a, in); in.erase(f->name); out.insert(in.begin(), in.end()); break; }
        case Formula::TOP: case Formula::BOT: break;
        default: freeVars(f->a, out); freeVars(f->b, out);
    }
}
void allVars(const FormP& f, std::set<std::string>& out) {
    switch (f->k) {
        case Formula::PRED: case Formula::EQ: for (auto& t : f->args) termVars(t, out); break;
        case Formula::NOT: allVars(f->a, out); break;
        case Formula::ALL: case Formula::EX: out.insert(f->name); allVars(f->a, out); break;
        case Formula::TOP: case Formula::BOT: break;
        default: allVars(f->a, out); allVars(f->b, out);
    }
}
void boundVars(const FormP& f, std::set<std::string>& out) {
    switch (f->k) {
        case Formula::NOT: boundVars(f->a, out); break;
        case Formula::ALL: case Formula::EX: out.insert(f->name); boundVars(f->a, out); break;
        case Formula::AND: case Formula::OR: case Formula::IMP: case Formula::IFF: boundVars(f->a, out); boundVars(f->b, out); break;
        default: break;
    }
}
int complexity(const FormP& f) {
    switch (f->k) {
        case Formula::PRED: case Formula::EQ: case Formula::TOP: case Formula::BOT: return 0;
        case Formula::NOT: case Formula::ALL: case Formula::EX: return complexity(f->a) + 1;
        default: return std::max(complexity(f->a), complexity(f->b)) + 1;
    }
}
struct Signature {
    std::set<std::string> consts;
    std::map<std::string, int> funcs, preds;
    std::string error;
};
void sigTerm(const TermP& t, Signature& s) {
    if (t->k == Term::CONST) s.consts.insert(t->name);
    else if (t->k == Term::FUNC) {
        auto it = s.funcs.find(t->name);
        if (it != s.funcs.end() && it->second != (int)t->args.size()) s.error = "function symbol '" + t->name + "' is used with different arities";
        s.funcs[t->name] = (int)t->args.size();
        for (auto& a : t->args) sigTerm(a, s);
    }
}
void sigForm(const FormP& f, Signature& s) {
    switch (f->k) {
        case Formula::PRED: {
            auto it = s.preds.find(f->name);
            if (it != s.preds.end() && it->second != (int)f->args.size()) s.error = "predicate '" + f->name + "' is used with different arities";
            s.preds[f->name] = (int)f->args.size();
            for (auto& t : f->args) sigTerm(t, s);
            break;
        }
        case Formula::EQ: for (auto& t : f->args) sigTerm(t, s); break;
        case Formula::NOT: case Formula::ALL: case Formula::EX: sigForm(f->a, s); break;
        case Formula::TOP: case Formula::BOT: break;
        default: sigForm(f->a, s); sigForm(f->b, s);
    }
}
void subformulas(const FormP& f, std::vector<FormP>& out, std::set<std::string>& seen) {
    switch (f->k) {
        case Formula::NOT: case Formula::ALL: case Formula::EX: subformulas(f->a, out, seen); break;
        case Formula::AND: case Formula::OR: case Formula::IMP: case Formula::IFF: subformulas(f->a, out, seen); subformulas(f->b, out, seen); break;
        default: break;
    }
    std::string k = fstr(f);
    if (seen.insert(k).second) out.push_back(f);
}
void collectTerms(const TermP& t, std::vector<TermP>& out, std::set<std::string>& seen) {
    for (auto& a : t->args) collectTerms(a, out, seen);
    if (seen.insert(tstr(t)).second) out.push_back(t);
}
void formTerms(const FormP& f, std::vector<TermP>& out, std::set<std::string>& seen) {
    switch (f->k) {
        case Formula::PRED: case Formula::EQ: for (auto& t : f->args) collectTerms(t, out, seen); break;
        case Formula::NOT: case Formula::ALL: case Formula::EX: formTerms(f->a, out, seen); break;
        case Formula::TOP: case Formula::BOT: break;
        default: formTerms(f->a, out, seen); formTerms(f->b, out, seen);
    }
}
bool groundTerm(const TermP& t) {
    if (t->k == Term::VAR) return false;
    for (auto& a : t->args) if (!groundTerm(a)) return false;
    return true;
}

// ---------- substitution ----------
TermP substTerm(const TermP& t, const std::string& v, const TermP& r) {
    if (t->k == Term::VAR) return t->name == v ? r : t;
    if (t->k == Term::CONST) return t;
    std::vector<TermP> a;
    for (auto& x : t->args) a.push_back(substTerm(x, v, r));
    return tFunc(t->name, a);
}
std::string freshVar(const std::string& base, const std::set<std::string>& used) {
    std::string b;
    for (char c : base) if (isalpha((unsigned char)c)) { b = std::string(1, c); break; }
    if (b.empty()) b = "x";
    for (int i = 1;; ++i) { std::string n = b + std::to_string(i); if (!used.count(n)) return n; }
}
FormP substF(const FormP& f, const std::string& v, const TermP& r) {
    switch (f->k) {
        case Formula::PRED: case Formula::EQ: {
            std::vector<TermP> a;
            for (auto& t : f->args) a.push_back(substTerm(t, v, r));
            auto n = std::make_shared<Formula>(*f); n->args = a; return n;
        }
        case Formula::NOT: return fNot(substF(f->a, v, r));
        case Formula::ALL: case Formula::EX: {
            if (f->name == v) return f;
            std::set<std::string> rv; termVars(r, rv);
            if (rv.count(f->name)) {                              // avoid capture
                std::set<std::string> used; allVars(f, used); used.insert(rv.begin(), rv.end());
                std::string nv = freshVar(f->name, used);
                FormP body = substF(f->a, f->name, tVar(nv));
                return fQ(f->k, nv, substF(body, v, r));
            }
            return fQ(f->k, f->name, substF(f->a, v, r));
        }
        case Formula::TOP: case Formula::BOT: return f;
        default: return fBin(f->k, substF(f->a, v, r), substF(f->b, v, r));
    }
}

// ---------- normal forms ----------
FormP nnfF(const FormP& f, bool neg) {
    switch (f->k) {
        case Formula::TOP: return fConst(!neg);
        case Formula::BOT: return fConst(neg);
        case Formula::PRED: case Formula::EQ: return neg ? fNot(f) : f;
        case Formula::NOT: return nnfF(f->a, !neg);
        case Formula::AND: return neg ? fBin(Formula::OR, nnfF(f->a, true), nnfF(f->b, true)) : fBin(Formula::AND, nnfF(f->a, false), nnfF(f->b, false));
        case Formula::OR: return neg ? fBin(Formula::AND, nnfF(f->a, true), nnfF(f->b, true)) : fBin(Formula::OR, nnfF(f->a, false), nnfF(f->b, false));
        case Formula::IMP: return nnfF(fBin(Formula::OR, fNot(f->a), f->b), neg);
        case Formula::IFF: return nnfF(fBin(Formula::AND, fBin(Formula::IMP, f->a, f->b), fBin(Formula::IMP, f->b, f->a)), neg);
        case Formula::ALL: return fQ(neg ? Formula::EX : Formula::ALL, f->name, nnfF(f->a, neg));
        case Formula::EX: return fQ(neg ? Formula::ALL : Formula::EX, f->name, nnfF(f->a, neg));
    }
    return f;
}
FormP renameBound(const FormP& f, std::set<std::string>& used, std::map<std::string, std::string> env) {
    // rename every bound variable to a fresh one (standardize apart)
    switch (f->k) {
        case Formula::PRED: case Formula::EQ: {
            std::vector<TermP> a;
            for (auto& t : f->args) {
                std::set<std::string> vs; termVars(t, vs);
                TermP nt = t;
                for (auto& v : vs) { auto it = env.find(v); if (it != env.end()) nt = substTerm(nt, v, tVar(it->second)); }
                a.push_back(nt);
            }
            auto n = std::make_shared<Formula>(*f); n->args = a; return n;
        }
        case Formula::NOT: return fNot(renameBound(f->a, used, env));
        case Formula::ALL: case Formula::EX: {
            std::string nv = freshVar(f->name, used);
            used.insert(nv);
            env[f->name] = nv;
            return fQ(f->k, nv, renameBound(f->a, used, env));
        }
        case Formula::TOP: case Formula::BOT: return f;
        default: return fBin(f->k, renameBound(f->a, used, env), renameBound(f->b, used, env));
    }
}
struct QPrefix { bool all; std::string var; };
FormP pullQuantifiers(const FormP& f, std::vector<QPrefix>& prefix) {
    switch (f->k) {
        case Formula::ALL: case Formula::EX:
            prefix.push_back({f->k == Formula::ALL, f->name});
            return pullQuantifiers(f->a, prefix);
        case Formula::AND: case Formula::OR: {
            FormP l = pullQuantifiers(f->a, prefix), r = pullQuantifiers(f->b, prefix);
            return fBin(f->k, l, r);
        }
        default: return f;
    }
}
FormP prenex(const FormP& f, std::vector<QPrefix>& prefix, FormP* nnfOut = nullptr) {
    FormP n = nnfF(f, false);
    if (nnfOut) *nnfOut = n;
    std::set<std::string> used; allVars(n, used);
    FormP std_ = renameBound(n, used, {});
    return pullQuantifiers(std_, prefix);
}
std::string prefixStr(const std::vector<QPrefix>& pre) {
    std::string s;
    for (auto& q : pre) s += std::string(q.all ? "forall " : "exists ") + q.var + " ";
    return s;
}
FormP closeWith(const std::vector<QPrefix>& pre, FormP m) {
    for (size_t i = pre.size(); i-- > 0;) m = fQ(pre[i].all ? Formula::ALL : Formula::EX, pre[i].var, m);
    return m;
}
// Skolemisation: returns the universally quantified matrix (prefix keeps only forall)
FormP skolemize(const FormP& pnfMatrix, const std::vector<QPrefix>& pre, std::vector<std::string>& universals,
                const Signature& sig, std::vector<std::string>* skolems = nullptr) {
    FormP m = pnfMatrix;
    int k = 0;
    std::vector<std::string> us;
    for (auto& q : pre) {
        if (q.all) { us.push_back(q.var); continue; }
        std::string name;
        do { name = "sk" + std::to_string(++k); } while (sig.funcs.count(name) || sig.consts.count(name));
        std::vector<TermP> args;
        for (auto& u : us) args.push_back(tVar(u));
        TermP sk = args.empty() ? tConst(name) : tFunc(name, args);
        m = substF(m, q.var, sk);
        if (skolems) skolems->push_back(name + (args.empty() ? "" : "(" + [&] { std::string s; for (size_t i = 0; i < us.size(); ++i) s += (i ? ", " : "") + us[i]; return s; }() + ")") + "  replaces  " + q.var);
    }
    universals = us;
    return m;
}

// ---------- clauses ----------
struct Lit { bool neg; FormP atom; };      // atom: PRED or EQ
typedef std::vector<Lit> Clause;
std::string litStr(const Lit& l) { return (l.neg ? "~" : "") + fstr(l.atom); }
std::string clauseStr(const Clause& c) {
    if (c.empty()) return "[]";
    std::string s = "{";
    for (size_t i = 0; i < c.size(); ++i) s += (i ? ", " : "") + litStr(c[i]);
    return s + "}";
}
bool cnfOf(const FormP& f, std::vector<Clause>& out, size_t cap, std::string& err) {
    switch (f->k) {
        case Formula::TOP: return true;                               // no clause
        case Formula::BOT: out.push_back({}); return true;
        case Formula::PRED: case Formula::EQ: out.push_back({{false, f}}); return true;
        case Formula::NOT: out.push_back({{true, f->a}}); return true;
        case Formula::AND: return cnfOf(f->a, out, cap, err) && cnfOf(f->b, out, cap, err);
        case Formula::OR: {
            std::vector<Clause> a, b;
            if (!cnfOf(f->a, a, cap, err) || !cnfOf(f->b, b, cap, err)) return false;
            if (a.empty() || b.empty()) return true;                  // TOP | x = TOP
            if (a.size() * b.size() > cap) { err = "the clause form is too large (more than " + std::to_string(cap) + " clauses)"; return false; }
            for (auto& x : a) for (auto& y : b) { Clause c = x; c.insert(c.end(), y.begin(), y.end()); out.push_back(c); }
            return true;
        }
        default: err = "unexpected connective in the matrix"; return false;
    }
}
Clause cleanClause(const Clause& c, bool& taut) {
    Clause out; std::set<std::string> seen;
    taut = false;
    for (auto& l : c) {
        std::string k = litStr(l);
        if (seen.count(k)) continue;
        seen.insert(k); out.push_back(l);
    }
    for (auto& l : out) if (seen.count(litStr({!l.neg, l.atom}))) taut = true;
    return out;
}

}  // namespace fol

// =====================================================================
//  ADVANCED LOGIC - models, evaluation, analysis
// =====================================================================
namespace fol {

std::vector<std::string> splitTop(const std::string& s, const std::string& seps) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '(' || c == '{' || c == '[') ++depth;
        else if (c == ')' || c == '}' || c == ']') --depth;
        if (depth == 0 && seps.find(c) != std::string::npos) { if (!trim(cur).empty()) out.push_back(trim(cur)); cur.clear(); }
        else cur += c;
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

// ---------- compiled formulas ----------
struct CTerm { int kind = 0; int id = 0; std::vector<CTerm> args; };          // 0 variable slot, 1 constant, 2 function
struct CForm;
typedef std::shared_ptr<CForm> CFormP;
struct CForm { Formula::K k = Formula::TOP; int pred = -1, slot = -1; std::vector<CTerm> args; CFormP a, b; std::string text, var; };
struct Comp {
    std::map<std::string, int> constId, funcId, predId, slotId;
    std::vector<std::string> constNames, funcNames, predNames, slotNames;
    std::vector<int> funcAr, predAr;
};
Comp makeComp(const Signature& sig, const std::vector<FormP>& fs) {
    Comp c;
    for (auto& s : sig.consts) { c.constId[s] = (int)c.constNames.size(); c.constNames.push_back(s); }
    for (auto& kv : sig.funcs) { c.funcId[kv.first] = (int)c.funcNames.size(); c.funcNames.push_back(kv.first); c.funcAr.push_back(kv.second); }
    for (auto& kv : sig.preds) { c.predId[kv.first] = (int)c.predNames.size(); c.predNames.push_back(kv.first); c.predAr.push_back(kv.second); }
    std::set<std::string> vs;
    for (auto& f : fs) allVars(f, vs);
    for (auto& v : vs) { c.slotId[v] = (int)c.slotNames.size(); c.slotNames.push_back(v); }
    return c;
}
CTerm compileTerm(const TermP& t, const Comp& c) {
    CTerm r;
    if (t->k == Term::VAR) { r.kind = 0; r.id = c.slotId.at(t->name); }
    else if (t->k == Term::CONST) { r.kind = 1; r.id = c.constId.at(t->name); }
    else { r.kind = 2; r.id = c.funcId.at(t->name); for (auto& a : t->args) r.args.push_back(compileTerm(a, c)); }
    return r;
}
CFormP compileForm(const FormP& f, const Comp& c) {
    auto r = std::make_shared<CForm>();
    r->k = f->k; r->text = fstr(f);
    switch (f->k) {
        case Formula::PRED: r->pred = c.predId.at(f->name); for (auto& t : f->args) r->args.push_back(compileTerm(t, c)); break;
        case Formula::EQ: for (auto& t : f->args) r->args.push_back(compileTerm(t, c)); break;
        case Formula::NOT: r->a = compileForm(f->a, c); break;
        case Formula::ALL: case Formula::EX: r->slot = c.slotId.at(f->name); r->var = f->name; r->a = compileForm(f->a, c); break;
        case Formula::TOP: case Formula::BOT: break;
        default: r->a = compileForm(f->a, c); r->b = compileForm(f->b, c);
    }
    return r;
}

struct CModel {
    int n = 0;
    std::vector<int> consts;                       // element index
    std::vector<std::vector<int>> funcs;           // table over n^arity (index = mixed radix), -1 = undefined
    std::vector<std::vector<char>> preds;
};
struct Tracer {
    std::ostringstream os;
    const std::vector<std::string>* names = nullptr;
    int lines = 0, maxLines = 160, maxDepth = 2;
    bool truncated = false;
    void line(int depth, const std::string& s) {
        if (lines >= maxLines) { truncated = true; return; }
        ++lines;
        os << std::string(depth * 2, ' ') << s << "\n";
    }
};
size_t tupleIndex(const std::vector<int>& v, int n) { size_t idx = 0; for (int x : v) idx = idx * n + x; return idx; }

int evalT(const CTerm& t, const CModel& m, const Comp& c, std::vector<int>& asg) {
    if (t.kind == 0) {
        int v = asg[t.id];
        if (v < 0) throw std::runtime_error("variable '" + c.slotNames[t.id] + "' is free and has no value (give it in the assignment box, e.g.  " + c.slotNames[t.id] + "=m1)");
        return v;
    }
    if (t.kind == 1) {
        int v = m.consts[t.id];
        if (v < 0) throw std::runtime_error("constant '" + c.constNames[t.id] + "' has no interpretation in the model");
        return v;
    }
    std::vector<int> av;
    for (auto& a : t.args) av.push_back(evalT(a, m, c, asg));
    int v = m.funcs[t.id][tupleIndex(av, m.n)];
    if (v < 0) throw std::runtime_error("function '" + c.funcNames[t.id] + "' is not defined for these arguments");
    return v;
}
std::string asgText(const std::vector<int>& asg, const Comp& c, const std::vector<std::string>* names) {
    std::string s;
    for (size_t i = 0; i < asg.size(); ++i)
        if (asg[i] >= 0) s += (s.empty() ? "" : ", ") + c.slotNames[i] + "=" + (names ? (*names)[asg[i]] : std::to_string(asg[i]));
    return s.empty() ? "" : "   [" + s + "]";
}
bool evalF(const CForm& f, const CModel& m, const Comp& c, std::vector<int>& asg, Tracer* tr, int depth) {
    switch (f.k) {
        case Formula::TOP: return true;
        case Formula::BOT: return false;
        case Formula::PRED: {
            std::vector<int> av;
            for (auto& a : f.args) av.push_back(evalT(a, m, c, asg));
            return m.preds[f.pred][tupleIndex(av, m.n)] != 0;
        }
        case Formula::EQ: return evalT(f.args[0], m, c, asg) == evalT(f.args[1], m, c, asg);
        case Formula::NOT: return !evalF(*f.a, m, c, asg, tr, depth);
        case Formula::AND: { bool x = evalF(*f.a, m, c, asg, tr, depth); bool y = evalF(*f.b, m, c, asg, tr, depth); return x && y; }
        case Formula::OR: { bool x = evalF(*f.a, m, c, asg, tr, depth); bool y = evalF(*f.b, m, c, asg, tr, depth); return x || y; }
        case Formula::IMP: { bool x = evalF(*f.a, m, c, asg, tr, depth); bool y = evalF(*f.b, m, c, asg, tr, depth); return !x || y; }
        case Formula::IFF: return evalF(*f.a, m, c, asg, tr, depth) == evalF(*f.b, m, c, asg, tr, depth);
        case Formula::ALL: case Formula::EX: {
            bool all = f.k == Formula::ALL, result = all;
            bool show = tr && depth <= tr->maxDepth;
            if (show) tr->line(depth, f.text + asgText(asg, c, tr->names));
            int saved = asg[f.slot];
            for (int e = 0; e < m.n; ++e) {
                asg[f.slot] = e;
                bool b = evalF(*f.a, m, c, asg, show ? tr : nullptr, depth + 1);
                if (show) tr->line(depth + 1, f.var + " = " + (*tr->names)[e] + "  :  " + (b ? "T" : "F"));
                if (all && !b) { result = false; if (!tr) break; }
                if (!all && b) { result = true; if (!tr) break; }
            }
            asg[f.slot] = saved;
            if (show) tr->line(depth, std::string("=> ") + (result ? "T" : "F"));
            return result;
        }
    }
    return false;
}

// ---------- model text parsing ----------
struct ParsedModel {
    std::vector<std::string> names;
    CModel m;
    std::map<std::string, int> idx;
    std::string summary;
};
int elemIndex(const ParsedModel& pm, const std::string& e, const char* what) {
    auto it = pm.idx.find(trim(e));
    if (it == pm.idx.end()) throw std::invalid_argument(std::string(what) + ": '" + trim(e) + "' is not an element of the domain");
    return it->second;
}
std::vector<std::string> parseDomain(const std::string& text) {
    std::string t = trim(text);
    if (!t.empty() && t.front() == '{' && t.back() == '}') t = t.substr(1, t.size() - 2);
    std::vector<std::string> out;
    for (auto& tok : splitTop(t, ",; \t\n")) {
        size_t dd = tok.find("..");
        if (dd != std::string::npos) {
            int a = atoi(tok.substr(0, dd).c_str()), b = atoi(tok.substr(dd + 2).c_str());
            if (b - a > 200) throw std::invalid_argument("domain too large");
            for (int v = a; v <= b; ++v) out.push_back(std::to_string(v));
        } else out.push_back(tok);
    }
    if (out.empty()) throw std::invalid_argument("the domain is empty: list its elements, e.g.  m1, m2, m3   or   1..4");
    std::set<std::string> seen;
    for (auto& e : out) if (!seen.insert(e).second) throw std::invalid_argument("domain element '" + e + "' is listed twice");
    return out;
}
void setPrintNames(ParsedModel& pm, const std::vector<std::string>& names) {
    pm.names = names;
    for (size_t i = 0; i < names.size(); ++i) pm.idx[names[i]] = (int)i;
}
// raw (symbol-level) data of a parsed model text
struct RawModel {
    std::vector<std::string> names;
    std::map<std::string, int> consts;
    std::map<std::string, std::set<std::vector<int>>> rels;
    std::map<std::string, int> relArity;                 // -1 unknown (empty set)
    std::map<std::string, std::map<std::vector<int>, int>> funcs;
    std::map<std::string, int> funcArity;
};
RawModel parseRaw(const ModelText& mt) {
    RawModel r;
    r.names = parseDomain(mt.domain);
    ParsedModel tmp; setPrintNames(tmp, r.names);
    for (auto& e : splitTop(mt.constants, ",;\n")) {
        size_t eq = e.find('=');
        if (eq == std::string::npos) throw std::invalid_argument("constants: use  a=m1, b=m2  (got '" + e + "')");
        r.consts[trim(e.substr(0, eq))] = elemIndex(tmp, e.substr(eq + 1), "constants");
    }
    for (auto& e : splitTop(mt.relations, ";\n")) {
        size_t eq = e.find('=');
        if (eq == std::string::npos) throw std::invalid_argument("relations: use  P={m1}; R={(m1,m2)}  (got '" + e + "')");
        std::string name = trim(e.substr(0, eq)), body = trim(e.substr(eq + 1));
        if (body.empty() || body.front() != '{' || body.back() != '}') throw std::invalid_argument("relation " + name + ": write the tuples inside { }");
        body = body.substr(1, body.size() - 2);
        int arity = -1;
        std::set<std::vector<int>> tuples;
        for (auto& tok : splitTop(body, ",")) {
            std::vector<int> tup;
            if (tok.front() == '(' && tok.back() == ')') for (auto& el : splitTop(tok.substr(1, tok.size() - 2), ",")) tup.push_back(elemIndex(tmp, el, ("relation " + name).c_str()));
            else tup.push_back(elemIndex(tmp, tok, ("relation " + name).c_str()));
            if (arity >= 0 && arity != (int)tup.size()) throw std::invalid_argument("relation " + name + " mixes tuples of different sizes");
            arity = (int)tup.size();
            tuples.insert(tup);
        }
        r.rels[name] = tuples; r.relArity[name] = arity;
    }
    for (auto& e : splitTop(mt.functions, ",;\n")) {
        size_t eq = e.rfind('=');
        size_t lp = e.find('(');
        if (eq == std::string::npos || lp == std::string::npos || e.back() == ')' ) throw std::invalid_argument("functions: use  f(m1)=m2, g(m1,m2)=m1  (got '" + e + "')");
        size_t rp = e.rfind(')', eq);
        if (rp == std::string::npos) throw std::invalid_argument("functions: missing ')' in '" + e + "'");
        std::string name = trim(e.substr(0, lp));
        std::vector<int> args;
        for (auto& el : splitTop(e.substr(lp + 1, rp - lp - 1), ",")) args.push_back(elemIndex(tmp, el, ("function " + name).c_str()));
        int val = elemIndex(tmp, e.substr(eq + 1), ("function " + name).c_str());
        auto fit = r.funcArity.find(name);
        if (fit != r.funcArity.end() && fit->second != (int)args.size()) throw std::invalid_argument("function " + name + " is used with different numbers of arguments");
        r.funcArity[name] = (int)args.size();
        r.funcs[name][args] = val;
    }
    return r;
}
std::string tupStr(const std::vector<int>& t, const std::vector<std::string>& names) {
    if (t.size() == 1) return names[t[0]];
    std::string s = "(";
    for (size_t i = 0; i < t.size(); ++i) s += (i ? "," : "") + names[t[i]];
    return s + ")";
}
std::string rawSummary(const RawModel& r, const char* label = "M") {
    std::ostringstream o;
    o << "  domain  { ";
    for (size_t i = 0; i < r.names.size(); ++i) o << (i ? ", " : "") << r.names[i];
    o << " }\n";
    for (auto& kv : r.consts) o << "  " << kv.first << "^" << label << " = " << r.names[kv.second] << "\n";
    for (auto& kv : r.rels) {
        o << "  " << kv.first << "^" << label << " = { ";
        bool first = true;
        for (auto& t : kv.second) { o << (first ? "" : ", ") << tupStr(t, r.names); first = false; }
        o << " }\n";
    }
    for (auto& kv : r.funcs) {
        o << "  " << kv.first << "^" << label << " : ";
        bool first = true;
        for (auto& e : kv.second) {
            o << (first ? "" : ", ") << kv.first << "(";
            for (size_t i = 0; i < e.first.size(); ++i) o << (i ? "," : "") << r.names[e.first[i]];
            o << ")=" << r.names[e.second];
            first = false;
        }
        o << "\n";
    }
    return o.str();
}
// build the compiled model for a signature
CModel buildModel(const RawModel& r, const Signature& sig, const Comp& comp) {
    CModel m;
    m.n = (int)r.names.size();
    for (auto& cn : comp.constNames) {
        auto it = r.consts.find(cn);
        if (it == r.consts.end()) throw std::invalid_argument("the model gives no value for the constant '" + cn + "' (write  " + cn + "=<element>  in Constants)");
        m.consts.push_back(it->second);
    }
    for (size_t p = 0; p < comp.predNames.size(); ++p) {
        auto it = r.rels.find(comp.predNames[p]);
        if (it == r.rels.end()) throw std::invalid_argument("the model gives no relation for '" + comp.predNames[p] + "' (write  " + comp.predNames[p] + "={}  for the empty relation)");
        int ar = comp.predAr[p], given = r.relArity.at(comp.predNames[p]);
        if (given >= 0 && given != ar) throw std::invalid_argument("relation " + comp.predNames[p] + " has " + std::to_string(ar) + " argument(s) in the formula but tuples of size " + std::to_string(given) + " in the model");
        size_t sz = 1; for (int i = 0; i < ar; ++i) sz *= m.n;
        std::vector<char> tab(sz, 0);
        for (auto& t : it->second) tab[tupleIndex(t, m.n)] = 1;
        m.preds.push_back(tab);
    }
    for (size_t f = 0; f < comp.funcNames.size(); ++f) {
        auto it = r.funcs.find(comp.funcNames[f]);
        if (it == r.funcs.end()) throw std::invalid_argument("the model gives no values for the function '" + comp.funcNames[f] + "'");
        int ar = comp.funcAr[f], given = r.funcArity.at(comp.funcNames[f]);
        if (given != ar) throw std::invalid_argument("function " + comp.funcNames[f] + " has " + std::to_string(ar) + " argument(s) in the formula but " + std::to_string(given) + " in the model");
        size_t sz = 1; for (int i = 0; i < ar; ++i) sz *= m.n;
        std::vector<int> tab(sz, -1);
        for (auto& e : it->second) tab[tupleIndex(e.first, m.n)] = e.second;
        for (size_t i = 0; i < sz; ++i) if (tab[i] < 0) {
            std::vector<int> t(ar); size_t x = i;
            for (int k = ar - 1; k >= 0; --k) { t[k] = (int)(x % m.n); x /= m.n; }
            throw std::invalid_argument("function " + comp.funcNames[f] + " must be total: no value for " + comp.funcNames[f] + "(" + [&] { std::string s; for (int k = 0; k < ar; ++k) s += (k ? "," : "") + r.names[t[k]]; return s; }() + ")");
        }
        m.funcs.push_back(tab);
    }
    (void)sig;
    return m;
}
std::string signatureText(const Signature& s) {
    std::string o;
    o += "  constants: ";
    { bool f = true; for (auto& c : s.consts) { o += (f ? "" : ", ") + c; f = false; } if (f) o += "(none)"; }
    o += "\n  functions: ";
    { bool f = true; for (auto& kv : s.funcs) { o += (f ? "" : ", ") + kv.first + "/" + std::to_string(kv.second); f = false; } if (f) o += "(none)"; }
    o += "\n  relations: ";
    { bool f = true; for (auto& kv : s.preds) { o += (f ? "" : ", ") + kv.first + "/" + std::to_string(kv.second); f = false; } if (f) o += "(none)"; }
    return o + "\n";
}
std::string setStr(const std::set<std::string>& s) {
    std::string o;
    for (auto& x : s) o += (o.empty() ? "" : ", ") + x;
    return o.empty() ? "(none)" : o;
}
const char* mainConnective(const FormP& f) {
    switch (f->k) {
        case Formula::PRED: case Formula::EQ: return "atomic formula";
        case Formula::TOP: case Formula::BOT: return "constant";
        case Formula::NOT: return "negation";
        case Formula::AND: return "conjunction";
        case Formula::OR: return "disjunction";
        case Formula::IMP: return "conditional";
        case Formula::IFF: return "biconditional";
        case Formula::ALL: return "universal quantifier";
        default: return "existential quantifier";
    }
}
int quantRank(const FormP& f) {
    switch (f->k) {
        case Formula::NOT: return quantRank(f->a);
        case Formula::ALL: case Formula::EX: return quantRank(f->a) + 1;
        case Formula::AND: case Formula::OR: case Formula::IMP: case Formula::IFF: return std::max(quantRank(f->a), quantRank(f->b));
        default: return 0;
    }
}
void checkSig(const Signature& s) { if (!s.error.empty()) throw std::invalid_argument(s.error); }
Signature sigOf(const std::vector<FormP>& fs) { Signature s; for (auto& f : fs) sigForm(f, s); return s; }

}  // namespace fol

// =====================================================================
//  AdvancedLogic: formula analysis and model checking
// =====================================================================
std::string AdvancedLogic::analyzeFormula(const std::string& formula) {
    using namespace fol;
    FormP f = parseF(formula);
    Signature sig = sigOf({f});
    checkSig(sig);
    std::set<std::string> fv, bv, av;
    freeVars(f, fv); boundVars(f, bv); allVars(f, av);
    std::vector<FormP> subs; std::set<std::string> seen;
    subformulas(f, subs, seen);
    std::vector<TermP> terms; std::set<std::string> tseen;
    formTerms(f, terms, tseen);
    std::vector<QPrefix> pre;
    prenex(f, pre);
    bool allEx = !pre.empty(), allAll = !pre.empty();
    for (auto& q : pre) { if (q.all) allEx = false; else allAll = false; }
    std::ostringstream o;
    o << "Formula:   " << fstr(f) << "\n";
    o << "Well-formed formula of a first-order language: YES\n\n";
    o << "Language L (symbols used):\n" << signatureText(sig);
    bool usesEq = false;
    for (auto& s : subs) if (s->k == Formula::EQ) usesEq = true;
    o << "  equality '=': " << (usesEq ? "used" : "not used") << "\n\n";
    o << "Variables:\n";
    o << "  free:  " << setStr(fv) << "\n";
    o << "  bound: " << setStr(bv) << "\n";
    std::string both;
    for (auto& v : fv) if (bv.count(v)) both += (both.empty() ? "" : ", ") + v;
    if (!both.empty()) o << "  note: " << both << " occurs both free and bound (in different places)\n";
    o << "Sentence (no free variables): " << (fv.empty() ? "YES" : "NO  (an open formula; its truth depends on a variable assignment)") << "\n\n";
    o << "Main connective: " << mainConnective(f) << "\n";
    o << "Complexity comp(phi) = " << complexity(f) << "   (atoms 0; negation / quantifier +1; binary connective max+1)\n";
    o << "Quantifier rank = " << quantRank(f) << "\n";
    if (fv.empty() && !pre.empty()) {
        if (allEx) o << "Prenex prefix is all-existential: an EXISTENTIAL (Sigma_1) sentence (preserved when the model grows).\n";
        else if (allAll) o << "Prenex prefix is all-universal: a UNIVERSAL (Pi_1) sentence (preserved by submodels).\n";
        else o << "Prenex prefix mixes quantifiers (" << prefixStr(pre) << ")\n";
    } else if (fv.empty() && pre.empty()) o << "Quantifier-free sentence.\n";
    o << "\nSubformulas (" << subs.size() << ", with complexity):\n";
    for (size_t i = 0; i < subs.size(); ++i) {
        std::set<std::string> sf; freeVars(subs[i], sf);
        o << "  " << i + 1 << ". " << fstr(subs[i]) << "     comp=" << complexity(subs[i]) << "   free: " << setStr(sf) << "\n";
    }
    o << "\nTerms (" << terms.size() << "):\n";
    for (auto& t : terms) o << "  " << tstr(t) << "   [" << (t->k == Term::VAR ? "variable" : t->k == Term::CONST ? "constant" : "function term") << (groundTerm(t) ? ", closed" : "") << "]\n";
    return o.str();
}

std::string AdvancedLogic::modelCheckReport(const std::string& formula, const ModelText& mt, const std::string& assignment) {
    using namespace fol;
    FormP f = parseF(formula);
    Signature sig = sigOf({f});
    checkSig(sig);
    RawModel raw = parseRaw(mt);
    Comp comp = makeComp(sig, {f});
    CModel model = buildModel(raw, sig, comp);
    CFormP cf = compileForm(f, comp);
    std::vector<int> asg(comp.slotNames.size(), -1);
    ParsedModel pm; setPrintNames(pm, raw.names);
    for (auto& e : splitTop(assignment, ",;\n")) {
        size_t eq = e.find('=');
        if (eq == std::string::npos) throw std::invalid_argument("assignment: use  x=m1, y=m2  (got '" + e + "')");
        std::string v = trim(e.substr(0, eq));
        auto it = comp.slotId.find(v);
        int el = elemIndex(pm, e.substr(eq + 1), "assignment");
        if (it != comp.slotId.end()) asg[it->second] = el;
    }
    std::set<std::string> fv; freeVars(f, fv);
    Tracer tr; tr.names = &raw.names;
    std::vector<int> work = asg;
    bool value = evalF(*cf, model, comp, work, &tr, 0);
    std::ostringstream o;
    o << "Model M:\n" << rawSummary(raw) << "\n";
    o << "Formula: " << fstr(f) << "\n";
    if (!fv.empty()) {
        o << "Assignment s:";
        for (auto& v : fv) { int i = comp.slotId[v]; o << " " << v << "->" << raw.names[asg[i]]; }
        o << "\n";
    }
    o << "\nEvaluation (Tarski satisfaction, outer quantifiers unrolled over the domain):\n" << tr.os.str();
    if (tr.truncated) o << "  ... (trace shortened)\n";
    o << "\nRESULT: M " << (value ? "|=" : "does NOT satisfy (|=/)") << (fv.empty() ? " " : " [s] ") << fstr(f) << "   ->   " << (value ? "TRUE" : "FALSE") << "\n";
    if (fv.empty()) o << "M is a " << (value ? "model" : "counter-model") << " of this sentence.\n";
    return o.str();
}

// =====================================================================
//  AdvancedLogic: finite model finder, normal forms
// =====================================================================
namespace fol {
std::string modelFromDigits(const Comp& comp, const CModel& m) {
    std::ostringstream o;
    auto nm = [](int i) { return "m" + std::to_string(i + 1); };
    o << "    domain { ";
    for (int i = 0; i < m.n; ++i) o << (i ? ", " : "") << nm(i);
    o << " }\n";
    for (size_t c = 0; c < comp.constNames.size(); ++c) o << "    " << comp.constNames[c] << " = " << nm(m.consts[c]) << "\n";
    for (size_t p = 0; p < comp.predNames.size(); ++p) {
        o << "    " << comp.predNames[p] << " = { ";
        bool first = true;
        int ar = comp.predAr[p];
        for (size_t i = 0; i < m.preds[p].size(); ++i) if (m.preds[p][i]) {
            std::vector<int> t(ar); size_t x = i;
            for (int k = ar - 1; k >= 0; --k) { t[k] = (int)(x % m.n); x /= m.n; }
            std::string s;
            if (ar == 0) s = "T";
            else if (ar == 1) s = nm(t[0]);
            else { s = "("; for (int k = 0; k < ar; ++k) s += (k ? "," : "") + nm(t[k]); s += ")"; }
            o << (first ? "" : ", ") << s; first = false;
        }
        o << " }\n";
    }
    for (size_t f = 0; f < comp.funcNames.size(); ++f) {
        o << "    " << comp.funcNames[f] << ": ";
        int ar = comp.funcAr[f];
        for (size_t i = 0; i < m.funcs[f].size(); ++i) {
            std::vector<int> t(ar); size_t x = i;
            for (int k = ar - 1; k >= 0; --k) { t[k] = (int)(x % m.n); x /= m.n; }
            o << (i ? ", " : "") << comp.funcNames[f] << "(";
            for (int k = 0; k < ar; ++k) o << (k ? "," : "") << nm(t[k]);
            o << ")=" << nm(m.funcs[f][i]);
        }
        o << "\n";
    }
    return o.str();
}
}  // namespace fol

std::string AdvancedLogic::findModelsReport(FinderMode mode, const std::string& statement, const std::vector<std::string>& premisesIn, int maxSize) {
    using namespace fol;
    if (maxSize < 1) maxSize = 1;
    if (maxSize > 8) maxSize = 8;
    FormP st = parseF(statement);
    std::vector<FormP> prem;
    if (mode != FinderMode::Validity) for (auto& p : premisesIn) if (!trim(p).empty()) prem.push_back(parseF(p));
    std::vector<FormP> need = prem;
    std::string goalText;
    switch (mode) {
        case FinderMode::Satisfiable: case FinderMode::CountModels: need.push_back(st); break;
        default: need.push_back(fNot(st)); break;
    }
    for (auto& f : need) {
        std::set<std::string> fv; freeVars(f, fv);
        if (!fv.empty()) throw std::invalid_argument("only sentences can be tested (free variable '" + *fv.begin() + "'): close it with forall / exists");
    }
    Signature sig = sigOf(need);
    checkSig(sig);
    Comp comp = makeComp(sig, need);
    std::vector<CFormP> cf;
    for (auto& f : need) cf.push_back(compileForm(f, comp));

    std::ostringstream o;
    switch (mode) {
        case FinderMode::Satisfiable: o << "Question: is the set of sentences satisfiable? (search for a model)\n"; break;
        case FinderMode::Validity: o << "Question: is the sentence valid? (search for a counter-model, i.e. a model of its negation)\n"; break;
        case FinderMode::Consequence: o << "Question: do the premises entail the conclusion? (search for a model of premises + negated conclusion)\n"; break;
        default: o << "Count the models of the sentences on each finite domain size\n"; break;
    }
    for (auto& p : prem) o << "  premise:    " << fstr(p) << "\n";
    o << "  " << (mode == FinderMode::Validity || mode == FinderMode::Consequence ? "conclusion: " : "sentence:   ") << fstr(st) << "\n";
    o << "Language:\n" << signatureText(sig) << "\n";

    bool found = false;
    CModel firstModel; int firstSize = 0;
    for (int n = 1; n <= maxSize; ++n) {
        // digit layout
        std::vector<int> bases;
        for (size_t c = 0; c < comp.constNames.size(); ++c) bases.push_back(n);
        std::vector<size_t> predLen, funcLen;
        for (int ar : comp.predAr) { size_t L = 1; for (int i = 0; i < ar; ++i) L *= n; predLen.push_back(L); for (size_t i = 0; i < L; ++i) bases.push_back(2); }
        for (int ar : comp.funcAr) { size_t L = 1; for (int i = 0; i < ar; ++i) L *= n; funcLen.push_back(L); for (size_t i = 0; i < L; ++i) bases.push_back(n); }
        double logTotal = 0;
        for (int b : bases) logTotal += std::log2((double)b);
        o << "domain size " << n << ": 2^" << (int)std::ceil(logTotal) << " interpretations";
        if (logTotal > 21.0) { o << "  -> too many to enumerate (limit 2^21); stopping here\n"; break; }
        std::vector<int> digits(bases.size(), 0);
        CModel m; m.n = n;
        m.consts.assign(comp.constNames.size(), 0);
        m.preds.clear(); m.funcs.clear();
        for (size_t L : predLen) m.preds.push_back(std::vector<char>(L, 0));
        for (size_t L : funcLen) m.funcs.push_back(std::vector<int>(L, 0));
        unsigned long long count = 0;
        bool keepFirst = true;
        CModel firstHere;
        for (;;) {
            size_t d = 0;
            for (size_t c = 0; c < comp.constNames.size(); ++c) m.consts[c] = digits[d++];
            for (size_t p = 0; p < predLen.size(); ++p) for (size_t i = 0; i < predLen[p]; ++i) m.preds[p][i] = (char)digits[d++];
            for (size_t f = 0; f < funcLen.size(); ++f) for (size_t i = 0; i < funcLen[f]; ++i) m.funcs[f][i] = digits[d++];
            std::vector<int> asg(comp.slotNames.size(), -1);
            bool ok = true;
            for (auto& c : cf) if (!evalF(*c, m, comp, asg, nullptr, 0)) { ok = false; break; }
            if (ok) {
                ++count;
                if (keepFirst) { firstHere = m; keepFirst = false; }
                if (mode != FinderMode::CountModels) break;
            }
            size_t i = 0;                                          // odometer
            for (; i < digits.size(); ++i) { if (++digits[i] < bases[i]) break; digits[i] = 0; }
            if (i == digits.size()) break;
        }
        o << "  ->  " << (mode == FinderMode::CountModels ? std::to_string(count) + " model(s)" : count ? "model found" : "none") << "\n";
        if (count && !found) { found = true; firstModel = firstHere; firstSize = n; }
        if (found && mode != FinderMode::CountModels) break;
    }
    o << "\n";
    switch (mode) {
        case FinderMode::Satisfiable:
            if (found) o << "SATISFIABLE: a model with " << firstSize << " element(s):\n" << modelFromDigits(comp, firstModel);
            else o << "No model with at most " << maxSize << " elements found. (Not a proof of unsatisfiability: it may only have larger or infinite models. Use the Tableau or Resolution prover.)\n";
            break;
        case FinderMode::Validity:
            if (found) o << "NOT VALID. Counter-model with " << firstSize << " element(s) (the sentence is false in it):\n" << modelFromDigits(comp, firstModel);
            else o << "No counter-model with at most " << maxSize << " elements. The sentence is true in all those finite models. (Validity needs ALL models: try the Tableau prover.)\n";
            break;
        case FinderMode::Consequence:
            if (found) o << "NOT a consequence. Counter-model with " << firstSize << " element(s) (premises true, conclusion false):\n" << modelFromDigits(comp, firstModel);
            else o << "No counter-model with at most " << maxSize << " elements. No finite counter-model of that size exists (try the Tableau or Resolution prover for a proof).\n";
            break;
        default:
            if (found) o << "First model found, size " << firstSize << ":\n" << modelFromDigits(comp, firstModel);
            o << "(Counts include isomorphic copies: relabelling the domain elements gives different interpretations.)\n";
            o << "For a relation of arity k on n elements there are 2^(n^k) possible interpretations.\n";
    }
    return o.str();
}

std::string AdvancedLogic::normalFormReport(const std::string& formula) {
    using namespace fol;
    FormP f = parseF(formula);
    Signature sig = sigOf({f});
    checkSig(sig);
    std::set<std::string> fv; freeVars(f, fv);
    std::vector<QPrefix> pre;
    FormP nn;
    FormP matrix = prenex(f, pre, &nn);
    std::ostringstream o;
    o << "Formula:                 " << fstr(f) << "\n";
    o << "Negation normal form:    " << fstr(nn) << "\n\n";
    FormP pnfF = closeWith(pre, matrix);
    o << "Prenex normal form (bound variables renamed apart):\n  prefix: " << (pre.empty() ? "(none)" : prefixStr(pre)) << "\n  matrix: " << fstr(matrix) << "\n  PNF:    " << fstr(pnfF) << "\n\n";
    std::vector<QPrefix> all;
    for (auto& v : fv) all.push_back({true, v});                  // free variables = universally quantified
    all.insert(all.end(), pre.begin(), pre.end());
    std::vector<std::string> universals, skolems;
    FormP sk = skolemize(matrix, all, universals, sig, &skolems);
    std::vector<QPrefix> ups;
    for (auto& u : universals) ups.push_back({true, u});
    o << "Skolem normal form (equisatisfiable, not equivalent):\n";
    if (!fv.empty()) o << "  (free variables are read as universally quantified)\n";
    for (auto& s : skolems) o << "  Skolem term: " << s << "\n";
    o << "  " << fstr(closeWith(ups, sk)) << "\n\n";
    std::vector<Clause> cl; std::string err;
    if (!cnfOf(sk, cl, 2000, err)) { o << "Clause form: " << err << "\n"; return o.str(); }
    std::vector<std::string> shown;
    for (auto& c : cl) { bool taut; Clause cc = cleanClause(c, taut); if (!taut) shown.push_back(clauseStr(cc)); }
    o << "Clause form (conjunction of clauses, all variables universal; " << shown.size() << " clause(s)):\n";
    for (auto& s : shown) o << "  " << s << "\n";
    if (shown.empty()) o << "  (empty set: the sentence is trivially satisfiable)\n";
    return o.str();
}

// =====================================================================
//  AdvancedLogic: unification, tableau prover, resolution prover
// =====================================================================
namespace fol {
typedef std::map<std::string, TermP> Subst;

TermP walkT(const TermP& t, const Subst& s) {
    TermP x = t;
    while (x->k == Term::VAR) { auto it = s.find(x->name); if (it == s.end()) break; x = it->second; }
    return x;
}
bool occursIn(const std::string& v, const TermP& t, const Subst& s) {
    TermP x = walkT(t, s);
    if (x->k == Term::VAR) return x->name == v;
    for (auto& a : x->args) if (occursIn(v, a, s)) return true;
    return false;
}
TermP applyS(const TermP& t, const Subst& s) {
    TermP x = walkT(t, s);
    if (x->k != Term::FUNC) return x;
    std::vector<TermP> a;
    for (auto& y : x->args) a.push_back(applyS(y, s));
    return tFunc(x->name, a);
}
bool unifyT(const TermP& a0, const TermP& b0, Subst& s, std::string& why) {
    TermP a = walkT(a0, s), b = walkT(b0, s);
    if (a->k == Term::VAR) {
        if (b->k == Term::VAR && a->name == b->name) return true;
        if (occursIn(a->name, b, s)) { why = "occurs check fails: " + a->name + " occurs in " + tstr(applyS(b, s)); return false; }
        s[a->name] = b; return true;
    }
    if (b->k == Term::VAR) return unifyT(b, a, s, why);
    if (a->k == Term::CONST && b->k == Term::CONST) {
        if (a->name == b->name) return true;
        why = "clash: the constants " + a->name + " and " + b->name + " are different"; return false;
    }
    if (a->k == Term::FUNC && b->k == Term::FUNC) {
        if (a->name != b->name || a->args.size() != b->args.size()) {
            why = "clash: " + a->name + "/" + std::to_string(a->args.size()) + " and " + b->name + "/" + std::to_string(b->args.size()) + " are different function symbols"; return false;
        }
        for (size_t i = 0; i < a->args.size(); ++i) if (!unifyT(a->args[i], b->args[i], s, why)) return false;
        return true;
    }
    why = "clash: " + tstr(applyS(a, s)) + " and " + tstr(applyS(b, s)) + " have different forms (a constant cannot match a function term)";
    return false;
}
}  // namespace fol

std::string AdvancedLogic::unifyReport(const std::string& a, const std::string& b) {
    using namespace fol;
    FormP fa, fb; TermP ta, tb;
    bool atomA = fol::Parser(MathLogic::normalizeSymbols(a)).parseAtomOrTerm(fa, ta);
    bool atomB = fol::Parser(MathLogic::normalizeSymbols(b)).parseAtomOrTerm(fb, tb);
    if (atomA != atomB) throw std::invalid_argument("unify two terms (f(x), a, ...) or two atoms (P(x, a), ...), not one of each");
    Subst s; std::string why;
    bool ok = true;
    std::set<std::string> vs;
    std::ostringstream o;
    if (atomA) {
        o << "Atom 1: " << fstr(fa) << "\nAtom 2: " << fstr(fb) << "\n\n";
        for (auto& t : fa->args) termVars(t, vs);
        for (auto& t : fb->args) termVars(t, vs);
        if (fa->k != Formula::PRED || fb->k != Formula::PRED || fa->name != fb->name || fa->args.size() != fb->args.size()) {
            o << "NOT unifiable: different predicate symbols or arities.\n";
            return o.str();
        }
        for (size_t i = 0; i < fa->args.size() && ok; ++i) ok = unifyT(fa->args[i], fb->args[i], s, why);
    } else {
        o << "Term 1: " << tstr(ta) << "\nTerm 2: " << tstr(tb) << "\n\n";
        termVars(ta, vs); termVars(tb, vs);
        ok = unifyT(ta, tb, s, why);
    }
    if (!ok) { o << "NOT unifiable: " << why << "\n"; return o.str(); }
    o << "UNIFIABLE. Most general unifier (mgu) theta:\n  { ";
    bool first = true;
    for (auto& v : vs) {
        TermP r = applyS(tVar(v), s);
        if (r->k == Term::VAR && r->name == v) continue;
        o << (first ? "" : ", ") << v << " -> " << tstr(r); first = false;
    }
    if (first) o << "(identity)";
    o << " }\n";
    if (atomA) {
        std::vector<TermP> ar; for (auto& t : fa->args) ar.push_back(applyS(t, s));
        o << "Common instance: " << fstr(fPred(fa->name, ar)) << "\n";
    } else o << "Common instance: " << tstr(applyS(ta, s)) << "\n";
    o << "\n(Variables with the same name are the same variable. Robinson's algorithm with occurs check.)\n";
    return o.str();
}

// ---------------------------------------------------------------------
//  Tableau prover
// ---------------------------------------------------------------------
namespace fol {
struct TBranch {
    std::vector<FormP> forms;
    std::vector<int> lineNo;
    std::vector<char> done;
    std::map<std::string, int> keyLine;
    std::map<size_t, std::set<std::string>> applied;
    std::vector<TermP> terms;
    std::set<std::string> termKeys;
    int newConsts = 0;
    bool truncated = false;
};
enum TStatus { T_CLOSED, T_OPEN, T_LIMIT };
enum AddRes { ADD_DUP, ADD_OK, ADD_CLOSED };
struct TabCtx {
    std::ostringstream out;
    int lineCounter = 0, maxLines = 400;
    bool limit = false;
    std::set<std::string> usedConsts;
    int constCounter = 0, maxNewConsts = 6;
    std::vector<TBranch> openBranches;
};
FormP negF(const FormP& f) { return f->k == Formula::NOT ? f->a : fNot(f); }

void addGroundTerms(TBranch& b, const TermP& t) {
    for (auto& a : t->args) addGroundTerms(b, a);
    if (t->k != Term::VAR && groundTerm(t) && b.termKeys.insert(tstr(t)).second) b.terms.push_back(t);
}
void addFormTerms(TBranch& b, const FormP& f) {
    if (f->k == Formula::PRED || f->k == Formula::EQ) for (auto& t : f->args) addGroundTerms(b, t);
    else if (f->k == Formula::NOT) addFormTerms(b, f->a);
    else if (f->k == Formula::AND || f->k == Formula::OR || f->k == Formula::IMP || f->k == Formula::IFF) { addFormTerms(b, f->a); addFormTerms(b, f->b); }
    else if (f->k == Formula::ALL || f->k == Formula::EX) addFormTerms(b, f->a);
}
AddRes addF(TBranch& b, TabCtx& c, const FormP& f, const std::string& why, const std::string& prefix) {
    std::string k = fstr(f);
    if (b.keyLine.count(k)) return ADD_DUP;
    if (c.lineCounter >= c.maxLines) { c.limit = true; return ADD_OK; }
    int n = ++c.lineCounter;
    b.forms.push_back(f); b.lineNo.push_back(n); b.done.push_back(0); b.keyLine[k] = n;
    c.out << prefix << (n < 10 ? " " : "") << n << ". " << k << "      [" << why << "]\n";
    addFormTerms(b, f);
    if (f->k == Formula::BOT || (f->k == Formula::NOT && f->a->k == Formula::TOP)) {
        c.out << prefix << "    x  closed (line " << n << " is a contradiction)\n";
        return ADD_CLOSED;
    }
    auto it = b.keyLine.find(fstr(negF(f)));
    if (it != b.keyLine.end()) {
        c.out << prefix << "    x  closed (lines " << it->second << " and " << n << " are contradictory)\n";
        return ADD_CLOSED;
    }
    return ADD_OK;
}
int ruleType(const FormP& f) {                       // 0 literal, 1 alpha, 2 delta, 3 beta, 4 gamma
    switch (f->k) {
        case Formula::AND: return 1;
        case Formula::OR: case Formula::IMP: case Formula::IFF: return 3;
        case Formula::EX: return 2;
        case Formula::ALL: return 4;
        case Formula::NOT: {
            const FormP& g = f->a;
            switch (g->k) {
                case Formula::OR: case Formula::IMP: case Formula::NOT: return 1;
                case Formula::AND: case Formula::IFF: return 3;
                case Formula::ALL: return 2;
                case Formula::EX: return 4;
                default: return 0;
            }
        }
        default: return 0;
    }
}
std::string newConstant(TabCtx& c) {
    std::string n;
    do { n = "c" + std::to_string(++c.constCounter); } while (c.usedConsts.count(n));
    c.usedConsts.insert(n);
    return n;
}
typedef std::vector<std::pair<FormP, std::string>> Alt;

TStatus expandBranch(TBranch b, TabCtx& c, const std::string& prefix) {
    for (;;) {
        if (c.limit) return T_LIMIT;
        bool acted = false;
        // alpha rules
        for (size_t i = 0; i < b.forms.size() && !acted; ++i) {
            if (b.done[i] || ruleType(b.forms[i]) != 1) continue;
            FormP f = b.forms[i]; b.done[i] = 1; acted = true;
            std::string ref = std::to_string(b.lineNo[i]);
            std::vector<std::pair<FormP, std::string>> adds;
            if (f->k == Formula::AND) adds = {{f->a, "& " + ref}, {f->b, "& " + ref}};
            else if (f->a->k == Formula::OR) adds = {{negF(f->a->a), "~| " + ref}, {negF(f->a->b), "~| " + ref}};
            else if (f->a->k == Formula::IMP) adds = {{f->a->a, "~-> " + ref}, {negF(f->a->b), "~-> " + ref}};
            else adds = {{f->a->a, "~~ " + ref}};
            for (auto& ad : adds) if (addF(b, c, ad.first, ad.second, prefix) == ADD_CLOSED) return T_CLOSED;
        }
        if (acted) continue;
        // delta rules (new constants)
        for (size_t i = 0; i < b.forms.size() && !acted; ++i) {
            if (b.done[i] || ruleType(b.forms[i]) != 2) continue;
            b.done[i] = 1;
            if (b.newConsts >= c.maxNewConsts) { b.truncated = true; continue; }
            FormP f = b.forms[i];
            std::string cn = newConstant(c);
            ++b.newConsts;
            TermP ct = tConst(cn);
            if (b.termKeys.insert(cn).second) b.terms.push_back(ct);
            acted = true;
            std::string ref = std::to_string(b.lineNo[i]);
            FormP inst = f->k == Formula::EX ? substF(f->a, f->name, ct) : fNot(substF(f->a->a, f->a->name, ct));
            if (addF(b, c, inst, std::string(f->k == Formula::EX ? "exists " : "~forall ") + ref + ", " + cn + " is new", prefix) == ADD_CLOSED) return T_CLOSED;
        }
        if (acted) continue;
        // beta rules (branching)
        for (size_t i = 0; i < b.forms.size(); ++i) {
            if (b.done[i] || ruleType(b.forms[i]) != 3) continue;
            FormP f = b.forms[i]; b.done[i] = 1;
            std::string ref = std::to_string(b.lineNo[i]);
            std::vector<Alt> alts;
            std::string name;
            if (f->k == Formula::OR) { name = "|"; alts = {{{f->a, "| " + ref}}, {{f->b, "| " + ref}}}; }
            else if (f->k == Formula::IMP) { name = "->"; alts = {{{negF(f->a), "-> " + ref}}, {{f->b, "-> " + ref}}}; }
            else if (f->k == Formula::IFF) { name = "<->"; alts = {{{f->a, "<-> " + ref}, {f->b, "<-> " + ref}}, {{negF(f->a), "<-> " + ref}, {negF(f->b), "<-> " + ref}}}; }
            else if (f->a->k == Formula::AND) { name = "~&"; alts = {{{negF(f->a->a), "~& " + ref}}, {{negF(f->a->b), "~& " + ref}}}; }
            else { name = "~<->"; alts = {{{f->a->a, "~<-> " + ref}, {negF(f->a->b), "~<-> " + ref}}, {{negF(f->a->a), "~<-> " + ref}, {f->a->b, "~<-> " + ref}}}; }
            c.out << prefix << "    split on line " << ref << " (" << name << ")\n";
            bool anyLimit = false;
            for (size_t k = 0; k < alts.size(); ++k) {
                c.out << prefix << "    +-- branch " << k + 1 << "\n";
                std::string cp = prefix + (k + 1 < alts.size() ? "    |   " : "        ");
                TBranch child = b;
                bool closed = false;
                for (auto& ad : alts[k]) if (addF(child, c, ad.first, ad.second, cp) == ADD_CLOSED) { closed = true; break; }
                TStatus st = closed ? T_CLOSED : expandBranch(child, c, cp);
                if (st == T_OPEN) { c.out << prefix << "    (an open branch was found; the other branches are not needed)\n"; return T_OPEN; }
                if (st == T_LIMIT) anyLimit = true;
            }
            return anyLimit ? T_LIMIT : T_CLOSED;
        }
        // gamma rules (instantiation)
        bool any = false;
        for (size_t i = 0; i < b.forms.size(); ++i) {
            if (ruleType(b.forms[i]) != 4) continue;
            FormP f = b.forms[i];
            if (b.terms.empty()) {
                if (b.newConsts >= c.maxNewConsts) { b.truncated = true; continue; }
                std::string cn = newConstant(c); ++b.newConsts;
                b.terms.push_back(tConst(cn)); b.termKeys.insert(cn);
            }
            std::vector<TermP> ts = b.terms;
            for (auto& t : ts) {
                std::string tk = tstr(t);
                if (!b.applied[i].insert(tk).second) continue;
                FormP inst = f->k == Formula::ALL ? substF(f->a, f->name, t) : fNot(substF(f->a->a, f->a->name, t));
                std::string ref = std::to_string(b.lineNo[i]);
                AddRes r = addF(b, c, inst, std::string(f->k == Formula::ALL ? "forall " : "~exists ") + ref + ", " + tk, prefix);
                if (r == ADD_CLOSED) return T_CLOSED;
                if (r == ADD_OK) any = true;
                if (c.limit) return T_LIMIT;
            }
        }
        if (any) continue;
        break;
    }
    if (b.truncated) {
        c.out << prefix << "    ...  stopped: this branch would need more than " << c.maxNewConsts << " new constants\n";
        return T_LIMIT;
    }
    c.out << prefix << "    o  open (every rule has been applied; the branch is complete)\n";
    c.openBranches.push_back(b);
    return T_OPEN;
}
}  // namespace fol

std::string AdvancedLogic::tableauReport(const std::vector<std::string>& premisesIn, const std::string& conclusion, int maxLines) {
    using namespace fol;
    std::vector<FormP> prem;
    for (auto& p : premisesIn) if (!trim(p).empty()) prem.push_back(parseF(p));
    bool hasGoal = !trim(conclusion).empty();
    FormP goal = hasGoal ? parseF(conclusion) : nullptr;
    std::vector<FormP> start = prem;
    if (hasGoal) start.push_back(fNot(goal));
    if (start.empty()) throw std::invalid_argument("enter at least a conclusion or a premise");
    for (auto& f : start) {
        std::set<std::string> fv; freeVars(f, fv);
        if (!fv.empty()) throw std::invalid_argument("tableaux work on sentences (free variable '" + *fv.begin() + "'): close it with forall / exists");
    }
    Signature sig = sigOf(start);
    checkSig(sig);
    TabCtx c;
    c.maxLines = std::max(30, std::min(maxLines, 2000));
    c.usedConsts = sig.consts;
    TBranch b;
    for (auto& s : sig.consts) { b.terms.push_back(tConst(s)); b.termKeys.insert(s); }
    std::ostringstream head;
    for (auto& p : prem) head << "Premise:    " << fstr(p) << "\n";
    if (hasGoal) head << "Conclusion: " << fstr(goal) << "\n";
    head << (hasGoal ? "\nWe test whether  premises + ~(conclusion)  has a model. If every branch closes, there is none, so the argument is valid.\n\n"
                     : "\nWe test whether the premises have a model (all branches closing means they are unsatisfiable).\n\n");
    int idx = 0;
    TStatus status = T_OPEN;
    bool closed = false;
    for (auto& f : start) {
        bool isNeg = hasGoal && idx == (int)start.size() - 1;
        if (addF(b, c, f, isNeg ? "negated conclusion" : "premise", "") == ADD_CLOSED) { closed = true; break; }
        ++idx;
    }
    status = closed ? T_CLOSED : expandBranch(b, c, "");
    std::ostringstream o;
    o << head.str() << c.out.str() << "\n";
    bool funcs = !sig.funcs.empty();
    bool eq = false;
    for (auto& f : start) { std::vector<FormP> subs; std::set<std::string> sn; subformulas(f, subs, sn); for (auto& s : subs) if (s->k == Formula::EQ) eq = true; }
    if (status == T_CLOSED) {
        o << "RESULT: every branch is closed.\n";
        o << (hasGoal ? "The set {premises, ~conclusion} is unsatisfiable, so premises |= conclusion.\nBy completeness the argument is also derivable (premises |- conclusion).\n"
                      : "The premises are unsatisfiable (inconsistent).\n");
        if (eq) o << "Note: equality was treated as an ordinary predicate (no equality rules), so this is a valid proof but other equality-based proofs need more rules.\n";
    } else if (status == T_OPEN) {
        o << "RESULT: an open, complete branch exists.\n";
        o << (hasGoal ? "So the argument is NOT valid: premises |/= conclusion. Counter-model read off the open branch:\n"
                      : "So the premises are satisfiable. A model read off the open branch:\n");
        const TBranch& ob = c.openBranches.back();
        std::vector<std::string> dom;
        for (auto& t : ob.terms) if (t->k == Term::CONST) dom.push_back(t->name);
        if (dom.empty()) dom.push_back("c0");
        ModelText mt;
        for (size_t i = 0; i < dom.size(); ++i) mt.domain += (i ? ", " : "") + dom[i];
        for (auto& cn : sig.consts) mt.constants += (mt.constants.empty() ? "" : ", ") + cn + "=" + cn;
        std::map<std::string, std::set<std::string>> tup;
        for (auto& kv : sig.preds) tup[kv.first];
        for (auto& f : ob.forms) if (f->k == Formula::PRED) {
            bool ground = true; std::string t;
            for (size_t i = 0; i < f->args.size(); ++i) { if (f->args[i]->k != Term::CONST) ground = false; t += (i ? "," : "") + f->args[i]->name; }
            if (ground) tup[f->name].insert(f->args.size() == 1 ? t : "(" + t + ")");
        }
        for (auto& kv : tup) { std::string s; for (auto& x : kv.second) s += (s.empty() ? "" : ",") + x; mt.relations += (mt.relations.empty() ? "" : "; ") + kv.first + "={" + s + "}"; }
        if (funcs) o << "  (the language has function symbols; the model below ignores them)\n";
        try {
            RawModel raw = parseRaw(mt);
            o << rawSummary(raw);
            if (!funcs && !eq) {
                Comp comp = makeComp(sig, start);
                CModel m = buildModel(raw, sig, comp);
                bool all = true;
                for (auto& f : start) { std::vector<int> asg(comp.slotNames.size(), -1); CFormP cf = compileForm(f, comp); if (!evalF(*cf, m, comp, asg, nullptr, 0)) all = false; }
                o << (all ? "  Verified: every starting formula is true in this model" + std::string(hasGoal ? " (so the conclusion is false in it)." : ".") + "\n" : "  (could not verify this model automatically)\n");
            }
        } catch (const std::exception& e) { o << "  (model could not be printed: " << e.what() << ")\n"; }
    } else {
        o << "RESULT: undetermined (search limit reached).\n";
        o << "First-order validity is only semi-decidable: if the argument is valid a proof exists, but an invalid argument may give an endless tableau\n(for instance forall x exists y R(x,y) forces ever new constants).\n";
        o << "Try the Finite Model Finder for a counter-model, or the Resolution prover.\n";
    }
    return o.str();
}

// ---------------------------------------------------------------------
//  Resolution prover (clausal first-order resolution with factoring)
// ---------------------------------------------------------------------
namespace fol {
std::string anonT(const TermP& t) {
    if (t->k == Term::VAR) return "_";
    if (t->k == Term::CONST) return t->name;
    std::string s = t->name + "(";
    for (size_t i = 0; i < t->args.size(); ++i) s += (i ? "," : "") + anonT(t->args[i]);
    return s + ")";
}
std::string anonLit(const Lit& l) {
    std::string s = l.neg ? "~" : "";
    s += l.atom->k == Formula::EQ ? "=" : l.atom->name;
    s += "(";
    for (size_t i = 0; i < l.atom->args.size(); ++i) s += (i ? "," : "") + anonT(l.atom->args[i]);
    return s + ")";
}
TermP renameTermMap(const TermP& t, std::map<std::string, std::string>& m, int& counter, const std::string& prefix) {
    if (t->k == Term::VAR) {
        auto it = m.find(t->name);
        if (it == m.end()) it = m.insert({t->name, prefix + std::to_string(++counter)}).first;
        return tVar(it->second);
    }
    if (t->k == Term::CONST) return t;
    std::vector<TermP> a;
    for (auto& x : t->args) a.push_back(renameTermMap(x, m, counter, prefix));
    return tFunc(t->name, a);
}
Lit renameLit(const Lit& l, std::map<std::string, std::string>& m, int& counter, const std::string& prefix) {
    std::vector<TermP> a;
    for (auto& t : l.atom->args) a.push_back(renameTermMap(t, m, counter, prefix));
    auto n = std::make_shared<Formula>(*l.atom); n->args = a;
    return {l.neg, n};
}
Clause renameApart(const Clause& c, int& gcounter) {           // globally fresh variables
    std::map<std::string, std::string> m; Clause out;
    for (auto& l : c) out.push_back(renameLit(l, m, gcounter, "r"));
    return out;
}
Clause normalizeClause(const Clause& c) {                        // canonical variable names x1, x2, ... (order-independent)
    std::vector<size_t> idx(c.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return anonLit(c[a]) < anonLit(c[b]); });
    std::map<std::string, std::string> m; int counter = 0; Clause out;
    for (size_t i : idx) out.push_back(renameLit(c[i], m, counter, "x"));
    return out;
}
Lit applyLit(const Lit& l, const Subst& s) {
    std::vector<TermP> a;
    for (auto& t : l.atom->args) a.push_back(applyS(t, s));
    auto n = std::make_shared<Formula>(*l.atom); n->args = a;
    return {l.neg, n};
}
std::string substStr(const Subst& s, const std::set<std::string>& vars) {
    std::string o;
    for (auto& v : vars) {
        TermP r = applyS(tVar(v), s);
        if (r->k == Term::VAR && r->name == v) continue;
        o += (o.empty() ? "" : ", ") + v + "->" + tstr(r);
    }
    return o;
}
bool sameAtomShape(const Lit& a, const Lit& b) {
    if (a.atom->k != b.atom->k) return false;
    if (a.atom->k == Formula::PRED && a.atom->name != b.atom->name) return false;
    return a.atom->args.size() == b.atom->args.size();
}
bool unifyAtoms(const Lit& a, const Lit& b, Subst& s) {
    std::string why;
    for (size_t i = 0; i < a.atom->args.size(); ++i) if (!unifyT(a.atom->args[i], b.atom->args[i], s, why)) return false;
    return true;
}
FormP skolemizeG(const FormP& matrix, const std::vector<QPrefix>& pre, const Signature& sig, int& counter) {
    FormP m = matrix;
    std::vector<std::string> us;
    for (auto& q : pre) {
        if (q.all) { us.push_back(q.var); continue; }
        std::string name;
        do { name = "sk" + std::to_string(++counter); } while (sig.funcs.count(name) || sig.consts.count(name));
        std::vector<TermP> args;
        for (auto& u : us) args.push_back(tVar(u));
        m = substF(m, q.var, args.empty() ? tConst(name) : tFunc(name, args));
    }
    return m;
}
struct RClause { Clause lits; std::vector<int> parents; std::string how; };
}  // namespace fol

std::string AdvancedLogic::resolutionReport(const std::vector<std::string>& premisesIn, const std::string& conclusion, int maxClauses) {
    using namespace fol;
    std::vector<FormP> prem;
    for (auto& p : premisesIn) if (!trim(p).empty()) prem.push_back(parseF(p));
    bool hasGoal = !trim(conclusion).empty();
    FormP goal = hasGoal ? parseF(conclusion) : nullptr;
    std::vector<FormP> start = prem;
    std::vector<std::string> label;
    for (size_t i = 0; i < prem.size(); ++i) label.push_back("premise " + std::to_string(i + 1));
    if (hasGoal) { start.push_back(fNot(goal)); label.push_back("negated conclusion"); }
    if (start.empty()) throw std::invalid_argument("enter at least a conclusion or a premise");
    for (auto& f : start) {
        std::set<std::string> fv; freeVars(f, fv);
        if (!fv.empty()) throw std::invalid_argument("resolution works on sentences (free variable '" + *fv.begin() + "'): close it with forall / exists");
    }
    Signature sig = sigOf(start);
    checkSig(sig);
    bool usesEq = false;
    for (auto& f : start) { std::vector<FormP> subs; std::set<std::string> sn; subformulas(f, subs, sn); for (auto& s : subs) if (s->k == Formula::EQ) usesEq = true; }
    std::ostringstream o;
    for (auto& p : prem) o << "Premise:    " << fstr(p) << "\n";
    if (hasGoal) o << "Conclusion: " << fstr(goal) << "\n";
    o << "\nStep 1: put premises and the NEGATED conclusion into clause form (NNF, prenex, Skolem, CNF).\n";
    std::vector<RClause> all;
    std::map<std::string, int> seen;
    int skCounter = 0, gcounter = 0;
    auto addClause = [&](const Clause& c, const std::vector<int>& parents, const std::string& how, bool& isEmpty) -> int {
        bool taut; Clause cc = cleanClause(c, taut);
        if (taut) return -1;
        cc = normalizeClause(cc);
        std::string key = clauseStr(cc);
        auto it = seen.find(key);
        if (it != seen.end()) return -1;
        all.push_back({cc, parents, how});
        seen[key] = (int)all.size() - 1;
        if (cc.empty()) isEmpty = true;
        return (int)all.size() - 1;
    };
    bool empty = false;
    for (size_t i = 0; i < start.size(); ++i) {
        std::vector<QPrefix> pre;
        FormP matrix = prenex(start[i], pre);
        FormP sk = skolemizeG(matrix, pre, sig, skCounter);
        std::vector<Clause> cl; std::string err;
        if (!cnfOf(sk, cl, 2000, err)) throw std::invalid_argument(err);
        for (auto& c : cl) addClause(c, {}, "from " + label[i], empty);
    }
    size_t initial = all.size();
    for (size_t i = 0; i < all.size(); ++i) o << "  " << i + 1 << ". " << clauseStr(all[i].lits) << "      [" << all[i].how << "]\n";
    if (usesEq) o << "  (note: '=' is treated as an ordinary predicate; no paramodulation)\n";
    o << "\nStep 2: resolution (binary resolution + factoring, smallest clause first).\n";
    std::vector<size_t> processed, queue;
    for (size_t i = 0; i < all.size(); ++i) queue.push_back(i);
    int cycles = 0;
    bool limit = false;
    int emptyIdx = -1;
    for (size_t i = 0; i < all.size(); ++i) if (all[i].lits.empty()) emptyIdx = (int)i;
    while (emptyIdx < 0 && !queue.empty()) {
        if ((int)all.size() >= maxClauses || ++cycles > 6000) { limit = true; break; }
        size_t best = 0;
        for (size_t i = 1; i < queue.size(); ++i) if (all[queue[i]].lits.size() < all[queue[best]].lits.size()) best = i;
        size_t gi = queue[best];
        queue.erase(queue.begin() + best);
        std::vector<size_t> partners = processed;
        partners.push_back(gi);
        for (size_t pj : partners) {
            Clause c1 = renameApart(all[gi].lits, gcounter), c2 = renameApart(all[pj].lits, gcounter);
            for (size_t a = 0; a < c1.size() && emptyIdx < 0; ++a)
                for (size_t b = 0; b < c2.size() && emptyIdx < 0; ++b) {
                    if (c1[a].neg == c2[b].neg || !sameAtomShape(c1[a], c2[b])) continue;
                    Subst s;
                    if (!unifyAtoms(c1[a], c2[b], s)) continue;
                    Clause r;
                    for (size_t k = 0; k < c1.size(); ++k) if (k != a) r.push_back(applyLit(c1[k], s));
                    for (size_t k = 0; k < c2.size(); ++k) if (k != b) r.push_back(applyLit(c2[k], s));
                    std::set<std::string> vs;
                    for (auto& l : c1) for (auto& t : l.atom->args) termVars(t, vs);
                    for (auto& l : c2) for (auto& t : l.atom->args) termVars(t, vs);
                    std::string how = "resolve " + std::to_string(gi + 1) + ", " + std::to_string(pj + 1) + " on " + (c1[a].atom->k == Formula::PRED ? c1[a].atom->name : std::string("=")) + "   theta={" + substStr(s, vs) + "}";
                    bool isE = false;
                    int id = addClause(r, {(int)gi, (int)pj}, how, isE);
                    if (id >= 0) { queue.push_back(id); if (isE) emptyIdx = id; }
                }
        }
        if (emptyIdx < 0) {                                          // factoring of the given clause
            Clause c1 = renameApart(all[gi].lits, gcounter);
            for (size_t a = 0; a < c1.size() && emptyIdx < 0; ++a)
                for (size_t b = a + 1; b < c1.size() && emptyIdx < 0; ++b) {
                    if (c1[a].neg != c1[b].neg || !sameAtomShape(c1[a], c1[b])) continue;
                    Subst s;
                    if (!unifyAtoms(c1[a], c1[b], s)) continue;
                    Clause r;
                    for (size_t k = 0; k < c1.size(); ++k) if (k != b) r.push_back(applyLit(c1[k], s));
                    bool isE = false;
                    int id = addClause(r, {(int)gi}, "factor " + std::to_string(gi + 1), isE);
                    if (id >= 0) { queue.push_back(id); if (isE) emptyIdx = id; }
                }
        }
        processed.push_back(gi);
    }
    if (emptyIdx >= 0) {
        std::set<int> need; std::vector<int> stack = {emptyIdx};
        while (!stack.empty()) { int x = stack.back(); stack.pop_back(); if (!need.insert(x).second) continue; for (int p : all[x].parents) stack.push_back(p); }
        o << "\nDerivation of the empty clause (only the clauses that are needed):\n";
        for (int id : need) o << (id + 1 < 10 ? "  " : " ") << id + 1 << ". " << clauseStr(all[id].lits) << "      [" << all[id].how << "]\n";
        o << "\nRESULT: the empty clause [] was derived. The clause set is unsatisfiable.\n";
        o << (hasGoal ? "Hence premises + ~conclusion is unsatisfiable, i.e. premises |= conclusion (refutation proof).\n" : "The premises are inconsistent.\n");
    } else if (limit) {
        o << "\nRESULT: undetermined (limit of " << maxClauses << " clauses / " << 6000 << " steps reached). Resolution is only a semi-decision procedure for first-order logic.\n";
        o << "Generated " << all.size() << " clauses (" << initial << " initial). Try the Finite Model Finder for a counter-model.\n";
    } else {
        o << "\nRESULT: saturated without deriving []. The clause set is satisfiable, so the conclusion does NOT follow from the premises.\n";
        o << "(" << all.size() << " clauses generated in total.)\n";
    }
    return o.str();
}

// ---------------------------------------------------------------------
//  models: isomorphism, submodels, preservation
// ---------------------------------------------------------------------
namespace fol {
struct SymSet { std::set<std::string> consts, rels, funcs; };
SymSet symsOf(const RawModel& r) {
    SymSet s;
    for (auto& kv : r.consts) s.consts.insert(kv.first);
    for (auto& kv : r.rels) s.rels.insert(kv.first);
    for (auto& kv : r.funcs) s.funcs.insert(kv.first);
    return s;
}
std::string symDiff(const std::set<std::string>& a, const std::set<std::string>& b) {
    std::string o;
    for (auto& x : a) if (!b.count(x)) o += (o.empty() ? "" : ", ") + x;
    return o;
}
bool isSubset(const RawModel& n, const RawModel& m, std::vector<std::string>& why) {
    std::map<std::string, int> inM;
    for (size_t i = 0; i < m.names.size(); ++i) inM[m.names[i]] = (int)i;
    std::vector<int> toM(n.names.size(), -1);
    for (size_t i = 0; i < n.names.size(); ++i) {
        auto it = inM.find(n.names[i]);
        if (it == inM.end()) why.push_back("element " + n.names[i] + " of N is not an element of M");
        else toM[i] = it->second;
    }
    if (!why.empty()) return false;
    for (auto& kv : n.consts) {
        auto it = m.consts.find(kv.first);
        if (it != m.consts.end() && it->second != toM[kv.second])
            why.push_back("constant " + kv.first + ": " + kv.first + "^N = " + n.names[kv.second] + " but " + kv.first + "^M = " + m.names[it->second]);
    }
    for (auto& kv : n.rels) {
        auto it = m.rels.find(kv.first);
        if (it == m.rels.end()) continue;
        std::set<std::vector<int>> restricted;
        for (auto& t : it->second) {
            bool inside = true; std::vector<int> back;
            for (int x : t) { int idx = -1; for (size_t j = 0; j < toM.size(); ++j) if (toM[j] == x) idx = (int)j; if (idx < 0) inside = false; back.push_back(idx); }
            if (inside) restricted.insert(back);
        }
        if (restricted != kv.second) why.push_back("relation " + kv.first + ": " + kv.first + "^N is not the restriction of " + kv.first + "^M to the domain of N");
    }
    for (auto& kv : n.funcs) {
        auto it = m.funcs.find(kv.first);
        if (it == m.funcs.end()) continue;
        for (auto& e : kv.second) {
            std::vector<int> t;
            for (int x : e.first) t.push_back(toM[x]);
            auto f = it->second.find(t);
            if (f == it->second.end() || f->second != toM[e.second]) why.push_back("function " + kv.first + " disagrees with M on some argument");
        }
    }
    return why.empty();
}
}  // namespace fol

std::string AdvancedLogic::modelRelationReport(int mode, const ModelText& mt, const ModelText& nt, const std::string& sentence) {
    using namespace fol;
    RawModel M = parseRaw(mt), N = parseRaw(nt);
    SymSet sm = symsOf(M), sn = symsOf(N);
    std::string diff = symDiff(sm.consts, sn.consts) + symDiff(sn.consts, sm.consts) + symDiff(sm.rels, sn.rels) + symDiff(sn.rels, sm.rels) + symDiff(sm.funcs, sn.funcs) + symDiff(sn.funcs, sm.funcs);
    std::ostringstream o;
    o << "Model M:\n" << rawSummary(M, "M") << "Model N:\n" << rawSummary(N, "N") << "\n";
    if (!diff.empty() && mode != 2) throw std::invalid_argument("M and N must interpret the same symbols (constants, relations, functions); they differ in: " + diff);
    auto isoSearch = [&](std::vector<std::vector<int>>& found, size_t cap) {
        int n = (int)M.names.size();
        if ((int)N.names.size() != n) return;
        std::vector<int> sigma(n);
        for (int i = 0; i < n; ++i) sigma[i] = i;
        do {
            bool ok = true;
            for (auto& kv : M.consts) { auto it = N.consts.find(kv.first); if (it == N.consts.end() || it->second != sigma[kv.second]) { ok = false; break; } }
            for (auto& kv : M.rels) {
                if (!ok) break;
                auto it = N.rels.find(kv.first);
                if (it == N.rels.end() || it->second.size() != kv.second.size()) { ok = false; break; }
                for (auto& t : kv.second) { std::vector<int> u; for (int x : t) u.push_back(sigma[x]); if (!it->second.count(u)) { ok = false; break; } }
            }
            for (auto& kv : M.funcs) {
                if (!ok) break;
                auto it = N.funcs.find(kv.first);
                if (it == N.funcs.end() || it->second.size() != kv.second.size()) { ok = false; break; }
                for (auto& e : kv.second) { std::vector<int> u; for (int x : e.first) u.push_back(sigma[x]); auto f = it->second.find(u); if (f == it->second.end() || f->second != sigma[e.second]) { ok = false; break; } }
            }
            if (ok) { found.push_back(sigma); if (found.size() >= cap) return; }
        } while (std::next_permutation(sigma.begin(), sigma.end()));
    };
    if (mode == 0) {
        if (M.names.size() > 9) throw std::invalid_argument("the isomorphism search is limited to models with at most 9 elements");
        std::vector<std::vector<int>> found;
        if (M.names.size() == N.names.size()) isoSearch(found, 1000000);
        if (!found.empty()) {
            o << "RESULT: M and N are ISOMORPHIC  (" << found.size() << " isomorphism" << (found.size() == 1 ? "" : "s") << ").\n";
            o << "One isomorphism sigma:\n";
            for (size_t i = 0; i < M.names.size(); ++i) o << "  " << M.names[i] << "  ->  " << N.names[found[0][i]] << "\n";
            o << "\nIsomorphic models satisfy exactly the same first-order sentences (they are elementarily equivalent).\n";
        } else {
            o << "RESULT: M and N are NOT isomorphic.\n";
            if (M.names.size() != N.names.size()) o << "Reason: |M| = " << M.names.size() << " but |N| = " << N.names.size() << " (a sentence such as 'there are at least k elements' tells them apart).\n";
            else {
                for (auto& kv : M.rels) { auto it = N.rels.find(kv.first); if (it != N.rels.end() && it->second.size() != kv.second.size()) o << "Reason: |" << kv.first << "^M| = " << kv.second.size() << " but |" << kv.first << "^N| = " << it->second.size() << ".\n"; }
                o << "No bijection of the " << M.names.size() << " elements preserves all constants, relations and functions (all " << M.names.size() << "! bijections were checked).\n";
            }
            o << "For FINITE models, elementary equivalence coincides with isomorphism, so some first-order sentence is true in one model and false in the other.\n";
        }
    } else if (mode == 1) {
        std::vector<std::string> why;
        bool sub = isSubset(N, M, why);
        o << "Is N a submodel (substructure) of M?  (domain subset, same constants, relations restricted, functions closed)\n";
        if (sub) o << "RESULT: YES, N is a submodel of M (N <= M).\n";
        else { o << "RESULT: NO.\n"; for (auto& w : why) o << "  - " << w << "\n"; }
    } else {
        FormP f = parseF(sentence);
        std::set<std::string> fv; freeVars(f, fv);
        if (!fv.empty()) throw std::invalid_argument("enter a sentence (no free variables)");
        Signature sig = sigOf({f});
        checkSig(sig);
        Comp comp = makeComp(sig, {f});
        CFormP cf = compileForm(f, comp);
        CModel cm = buildModel(M, sig, comp), cn = buildModel(N, sig, comp);
        std::vector<int> asg(comp.slotNames.size(), -1);
        bool vm = evalF(*cf, cm, comp, asg, nullptr, 0), vn = evalF(*cf, cn, comp, asg, nullptr, 0);
        o << "Sentence: " << fstr(f) << "\n  true in M?  " << (vm ? "YES" : "no") << "\n  true in N?  " << (vn ? "YES" : "no") << "\n\n";
        std::vector<QPrefix> pre;
        prenex(f, pre);
        bool allEx = !pre.empty(), allAll = !pre.empty();
        for (auto& q : pre) { if (q.all) allEx = false; else allAll = false; }
        std::vector<std::string> why;
        bool sub = diff.empty() && isSubset(N, M, why);
        o << "N is " << (sub ? "" : "NOT ") << "a submodel of M.\n";
        if (M.names.size() == N.names.size() && M.names.size() <= 8 && diff.empty()) {
            std::vector<std::vector<int>> found; isoSearch(found, 1);
            o << "M and N are " << (found.empty() ? "NOT " : "") << "isomorphic" << (found.empty() ? "." : ", so they must agree on every sentence.") << "\n";
        }
        o << "\nPreservation theorems (N submodel of M):\n  Sigma_1 (existential) sentences go UP:  true in N  =>  true in M.\n  Pi_1 (universal) sentences go DOWN:     true in M  =>  true in N.\n";
        if (allEx) o << "This sentence is Sigma_1 (existential prefix)" << (sub ? (vn ? ": true in N, so it must be true in M" + std::string(vm ? " (it is)." : " -- CONTRADICTION, check inputs.") : ": false in N, nothing is forced.") : ".") << "\n";
        else if (allAll) o << "This sentence is Pi_1 (universal prefix)" << (sub ? (vm ? ": true in M, so it must be true in N" + std::string(vn ? " (it is)." : " -- CONTRADICTION, check inputs.") : ": false in M, nothing is forced.") : ".") << "\n";
        else o << "This sentence is neither purely existential nor purely universal in prenex form, so submodels need not preserve it.\n";
    }
    return o.str();
}

// ---------------------------------------------------------------------
//  Cantor's diagonal argument
// ---------------------------------------------------------------------
std::string AdvancedLogic::cantorReport(const std::string& rowsText, const std::string& setText) {
    using namespace fol;
    std::ostringstream o;
    std::vector<std::string> rows = splitTop(rowsText, ",;\n");
    if (!rows.empty()) {
        size_t n = rows.size();
        std::vector<std::string> r;
        for (auto& x : rows) { std::string t; for (char c : x) if (!isspace((unsigned char)c)) t += (char)toupper((unsigned char)c); r.push_back(t); }
        bool hasHT = false;
        for (auto& x : r) for (char c : x) { if (c == 'H' || c == 'T') hasHT = true; else if (c != '0' && c != '1') throw std::invalid_argument("use only H/T or 0/1 in the rows"); }
        for (size_t i = 0; i < n; ++i) if (r[i].size() <= i) throw std::invalid_argument("row " + std::to_string(i + 1) + " needs at least " + std::to_string(i + 1) + " symbols (the diagonal entry is symbol " + std::to_string(i + 1) + ")");
        size_t width = n;
        for (auto& x : r) width = std::max(width, x.size());
        o << "Cantor's diagonal argument\nEnumerated list of infinite binary sequences (first " << width << " symbols shown):\n\n";
        for (size_t i = 0; i < n; ++i) {
            o << "  s" << i + 1 << " : ";
            for (size_t j = 0; j < r[i].size(); ++j) o << (j == i ? "[" : " ") << r[i][j] << (j == i ? "]" : " ");
            o << "...\n";
        }
        std::string d;
        for (size_t i = 0; i < n; ++i) {
            char c = r[i][i], f;
            if (hasHT) f = c == 'H' ? 'T' : 'H'; else f = c == '0' ? '1' : '0';
            d += f;
        }
        o << "\n  d  : ";
        for (size_t i = 0; i < n; ++i) o << " " << d[i] << " ";
        o << "...   (flip the diagonal entry in every position)\n\n";
        for (size_t i = 0; i < n; ++i) o << "  d differs from s" << i + 1 << " at position " << i + 1 << ":  " << d[i] << " != " << r[i][i] << "\n";
        o << "\nSo d is different from every sequence in the list: the list cannot contain all infinite binary sequences.\n";
        o << "Hence the set of infinite binary sequences (equivalently P(N), or the reals) is UNCOUNTABLE:  |N| < |P(N)|.\n";
    }
    std::string st = trim(setText);
    if (!st.empty()) {
        std::string elems = st, mapText;
        size_t bar = st.find('|');
        if (bar != std::string::npos) { elems = trim(st.substr(0, bar)); mapText = trim(st.substr(bar + 1)); }
        std::vector<std::string> S;
        {
            std::string t = elems;
            if (!t.empty() && t.front() == '{' && t.back() == '}') t = t.substr(1, t.size() - 2);
            std::set<std::string> seen;
            for (auto& e : splitTop(t, ", ")) if (seen.insert(e).second) S.push_back(e);
        }
        if (S.size() > 20) throw std::invalid_argument("the set is limited to 20 elements");
        if (!rows.empty()) o << "\n----------------------------------------------------------\n\n";
        o << "Cantor's theorem for a set S = { ";
        for (size_t i = 0; i < S.size(); ++i) o << (i ? ", " : "") << S[i];
        o << " }   (|S| = " << S.size() << ")\n";
        unsigned long long cnt = 1ULL << S.size();
        o << "The power set P(S) has 2^" << S.size() << " = " << cnt << " elements, and " << S.size() << " < " << cnt << ".\n";
        if (S.size() <= 4) {
            o << "P(S) = {\n";
            for (unsigned long long m = 0; m < cnt; ++m) {
                o << "   { ";
                bool first = true;
                for (size_t i = 0; i < S.size(); ++i) if ((m >> i) & 1ULL) { o << (first ? "" : ", ") << S[i]; first = false; }
                o << " }\n";
            }
            o << "}\n";
        }
        if (!mapText.empty()) {
            std::map<std::string, std::set<std::string>> f;
            for (auto& e : splitTop(mapText, ",;\n")) {
                size_t col = e.find(':');
                if (col == std::string::npos) throw std::invalid_argument("map: use  a:{a,b}, b:{}, c:{a,c}");
                std::string key = trim(e.substr(0, col)), val = trim(e.substr(col + 1));
                if (!val.empty() && val.front() == '{' && val.back() == '}') val = val.substr(1, val.size() - 2);
                std::set<std::string> vs;
                for (auto& x : splitTop(val, ", ")) vs.insert(x);
                f[key] = vs;
            }
            o << "\nA function f : S -> P(S):\n";
            for (auto& x : S) { o << "  f(" << x << ") = { "; bool first = true; for (auto& y : f[x]) { o << (first ? "" : ", ") << y; first = false; } o << " }\n"; }
            std::set<std::string> D;
            for (auto& x : S) if (!f[x].count(x)) D.insert(x);
            o << "\nDiagonal set  D = { x in S : x not in f(x) } = { ";
            { bool first = true; for (auto& y : D) { o << (first ? "" : ", ") << y; first = false; } }
            o << " }\n";
            for (auto& x : S) o << "  " << x << (f[x].count(x) ? " in f(" : " not in f(") << x << ")  so  " << x << (D.count(x) ? " in D" : " not in D") << "  :  D != f(" << x << ")\n";
            o << "\nD differs from f(x) for every x, so D is not in the range of f: NO function S -> P(S) is onto. (Cantor's theorem; the same proof works for infinite S.)\n";
        }
    }
    if (o.str().empty()) throw std::invalid_argument("enter rows of H/T (or 0/1) and/or a set");
    return o.str();
}

// =====================================================================
//  Part 2: Turing machines
// =====================================================================
std::string AdvancedLogic::turingReport(const std::string& rulesText, const std::string& tapeText, const std::string& startState,
                                        const std::string& blankText, int maxSteps) {
    using namespace fol;
    char blank = trim(blankText).empty() ? '_' : trim(blankText)[0];
    maxSteps = std::max(1, std::min(maxSteps, 200000));
    struct Rule { std::string next; char write; char move; };
    std::map<std::pair<std::string, char>, Rule> rules;
    std::set<std::string> states;
    for (auto& e : splitTop(rulesText, ";\n")) {
        std::string t = e;
        size_t arrow = t.find("->");
        if (arrow != std::string::npos) t.replace(arrow, 2, " ");
        for (char& c : t) if (c == ',') c = ' ';
        std::istringstream is(t);
        std::string q, a, q2, b, mv;
        if (!(is >> q >> a >> q2 >> b >> mv) || a.size() != 1 || b.size() != 1 || mv.size() != 1)
            throw std::invalid_argument("rule '" + e + "': use   state read -> newstate write move   (one-character symbols, move = L, R or N), e.g.  q0 1 -> q0 1 R");
        char m = (char)toupper((unsigned char)mv[0]);
        if (m != 'L' && m != 'R' && m != 'N' && m != 'S') throw std::invalid_argument("rule '" + e + "': the move must be L, R or N");
        if (rules.count({q, a[0]})) throw std::invalid_argument("rule '" + e + "': two rules for the same state and symbol (the machine must be deterministic)");
        rules[{q, a[0]}] = {q2, b[0], m == 'S' ? 'N' : m};
        states.insert(q); states.insert(q2);
    }
    if (rules.empty()) throw std::invalid_argument("enter at least one rule");
    std::string state = trim(startState);
    if (state.empty()) throw std::invalid_argument("enter the start state");
    std::map<long, char> tape;
    std::string in = trim(tapeText);
    for (size_t i = 0; i < in.size(); ++i) if (in[i] != blank) tape[(long)i] = in[i];
    long head = 0, minPos = 0, maxPos = std::max<long>(0, (long)in.size() - 1);
    auto cell = [&](long p) { auto it = tape.find(p); return it == tape.end() ? blank : it->second; };
    auto snapshot = [&](int step) {
        std::string s = "step " + std::to_string(step) + "  state " + state + "   ";
        long lo = std::min(minPos, head), hi = std::max(maxPos, head);
        if (hi - lo > 70) { lo = std::max(lo, head - 35); hi = std::min(hi, head + 35); }
        for (long p = lo; p <= hi; ++p) s += (p == head ? "[" : " ") + std::string(1, cell(p)) + (p == head ? "]" : " ");
        return s;
    };
    std::vector<std::string> trace;
    trace.push_back(snapshot(0));
    int steps = 0;
    std::string stopReason;
    for (;; ) {
        char c = cell(head);
        auto it = rules.find({state, c});
        if (it == rules.end()) it = rules.find({state, '*'});
        if (it == rules.end()) { stopReason = "HALTED: no rule for state '" + state + "' reading '" + std::string(1, c) + "'"; break; }
        if (steps >= maxSteps) { stopReason = "STOPPED: step limit (" + std::to_string(maxSteps) + ") reached - the machine may run forever (halting problem!)"; break; }
        const Rule& r = it->second;
        char w = r.write == '*' ? c : r.write;
        if (w == blank) tape.erase(head); else tape[head] = w;
        if (r.move == 'L') --head; else if (r.move == 'R') ++head;
        state = r.next;
        minPos = std::min(minPos, head); maxPos = std::max(maxPos, head);
        ++steps;
        trace.push_back(snapshot(steps));
    }
    std::ostringstream o;
    o << "Turing machine with " << rules.size() << " rule(s); start state " << trim(startState) << "; blank = '" << blank << "'\n";
    o << "Input tape: " << (in.empty() ? "(blank)" : in) << "\n\n";
    size_t n = trace.size();
    for (size_t i = 0; i < n; ++i) {
        if (n > 60 && i == 40) { o << "   ... (" << n - 50 << " configurations omitted) ...\n"; i = n - 11; continue; }
        o << trace[i] << "\n";
    }
    std::string out;
    if (!tape.empty()) for (long p = tape.begin()->first; p <= tape.rbegin()->first; ++p) out += cell(p);
    o << "\n" << stopReason << "\nSteps executed: " << steps << "\nFinal state: " << state << "\nTape contents (blanks trimmed): " << (out.empty() ? "(blank)" : out) << "\n";
    return o.str();
}

// =====================================================================
//  Part 2: recursive functions (Goedel's schema)
// =====================================================================
namespace rf {
struct Node;
typedef std::shared_ptr<const Node> NP;
struct Node { enum K { CONST, Z, S, P, C, R, M, NAME } k = CONST; long long n = 0; int i = 0, kk = 0; std::vector<NP> sub; std::string name; };

class Parser {
public:
    explicit Parser(std::string t) : s(std::move(t)) {}
    NP parse() { NP n = expr(); ws(); if (p < s.size()) fail("unexpected '" + std::string(1, s[p]) + "'"); return n; }
private:
    std::string s; size_t p = 0;
    [[noreturn]] void fail(const std::string& m) const { throw std::invalid_argument(m + " (at position " + std::to_string(p + 1) + ")"); }
    void ws() { while (p < s.size() && isspace((unsigned char)s[p])) ++p; }
    void expect(char c) { ws(); if (p >= s.size() || s[p] != c) fail(std::string("expected '") + c + "'"); ++p; }
    long long number() { ws(); size_t q = p; while (q < s.size() && isdigit((unsigned char)s[q])) ++q; if (q == p) fail("expected a number"); long long v = atoll(s.substr(p, q - p).c_str()); p = q; return v; }
    NP mk(Node::K k) { auto n = std::make_shared<Node>(); n->k = k; return n; }
    NP expr() {
        ws();
        if (p >= s.size()) fail("unexpected end");
        char c = s[p];
        if (isdigit((unsigned char)c)) { auto n = std::make_shared<Node>(); n->k = Node::CONST; n->n = number(); return n; }
        if (c == 'Z' && (p + 1 >= s.size() || !isalpha((unsigned char)s[p + 1]))) { ++p; return mk(Node::Z); }
        if (c == 'S' && (p + 1 >= s.size() || !isalpha((unsigned char)s[p + 1]))) { ++p; return mk(Node::S); }
        if ((c == 'P' || c == 'C' || c == 'R' || c == 'M') && p + 1 < s.size() && (s[p + 1] == '(' || isspace((unsigned char)s[p + 1]))) {
            ++p; expect('(');
            auto n = std::make_shared<Node>();
            if (c == 'P') {
                n->k = Node::P; n->i = (int)number(); expect(','); n->kk = (int)number(); expect(')');
                if (n->i < 1 || n->i > n->kk) fail("P(i,k) needs 1 <= i <= k");
                return n;
            }
            n->k = c == 'C' ? Node::C : c == 'R' ? Node::R : Node::M;
            n->sub.push_back(expr());
            if (c == 'M') { expect(')'); return n; }
            expect(';');
            n->sub.push_back(expr());
            if (c == 'C') { for (;;) { ws(); if (p < s.size() && s[p] == ',') { ++p; n->sub.push_back(expr()); } else break; } }
            expect(')');
            return n;
        }
        if (isalpha((unsigned char)c)) {
            size_t q = p; while (q < s.size() && (isalnum((unsigned char)s[q]) || s[q] == '_')) ++q;
            auto n = std::make_shared<Node>(); n->k = Node::NAME; n->name = s.substr(p, q - p); p = q; return n;
        }
        fail(std::string("unexpected '") + c + "'");
    }
};
struct LibEntry { const char* name; const char* def; const char* meaning; };
const LibEntry LIB[] = {
    {"add",   "R(P(1,1); C(S; P(3,3)))",                          "add(x,0)=x ; add(x,S(y)) = S(add(x,y))"},
    {"mul",   "R(Z; C(add; P(1,3), P(3,3)))",                      "mul(x,0)=0 ; mul(x,S(y)) = add(x, mul(x,y))"},
    {"pred",  "R(0; P(1,2))",                                      "pred(0)=0 ; pred(S(y)) = y"},
    {"monus", "R(P(1,1); C(pred; P(3,3)))",                        "x - y cut off at 0:  monus(x,0)=x ; monus(x,S(y)) = pred(monus(x,y))"},
    {"exp",   "R(C(S; Z); C(mul; P(1,3), P(3,3)))",                "exp(x,0)=1 ; exp(x,S(y)) = mul(x, exp(x,y))   (x to the power y)"},
    {"fact",  "R(1; C(mul; C(S; P(1,2)), P(2,2)))",                "fact(0)=1 ; fact(S(y)) = S(y) * fact(y)"},
    {"sg",    "R(0; C(S; C(Z; P(1,2))))",                          "sg(0)=0 ; sg(S(y)) = 1   (sign)"},
    {"nsg",   "C(monus; C(S; Z), sg)",                             "nsg(x) = 1 - sg(x)   (1 if x = 0, else 0)"},
    {"dist",  "C(add; C(monus; P(1,2), P(2,2)), C(monus; P(2,2), P(1,2)))", "|x - y|"},
};
std::map<std::string, NP>& library() {
    static std::map<std::string, NP> lib;
    if (lib.empty()) for (auto& e : LIB) lib[e.name] = Parser(e.def).parse();
    return lib;
}
int arity(const NP& n, int depth = 0) {
    if (depth > 60) throw std::invalid_argument("expression nested too deeply");
    switch (n->k) {
        case Node::CONST: return 0;
        case Node::Z: case Node::S: return 1;
        case Node::P: return n->kk;
        case Node::NAME: {
            auto& lib = library();
            auto it = lib.find(n->name);
            if (it == lib.end()) throw std::invalid_argument("unknown function name '" + n->name + "' (see the library, or use Z, S, P(i,k), C, R, M)");
            return arity(it->second, depth + 1);
        }
        case Node::C: {
            int m = (int)n->sub.size() - 1;
            int f = arity(n->sub[0], depth + 1);
            if (f != m) throw std::invalid_argument("C(f; g1..gm): f must take exactly " + std::to_string(m) + " argument(s) but takes " + std::to_string(f));
            int a = arity(n->sub[1], depth + 1);
            for (int i = 2; i <= m; ++i) if (arity(n->sub[i], depth + 1) != a) throw std::invalid_argument("C(f; g1..gm): all g_i must have the same number of arguments");
            return a;
        }
        case Node::R: {
            int g = arity(n->sub[0], depth + 1), h = arity(n->sub[1], depth + 1);
            if (h != g + 2) throw std::invalid_argument("R(g; h): if g has " + std::to_string(g) + " argument(s), h must have " + std::to_string(g + 2) + " (found " + std::to_string(h) + ")");
            return g + 1;
        }
        case Node::M: {
            int f = arity(n->sub[0], depth + 1);
            if (f < 1) throw std::invalid_argument("M(f): f needs at least one argument");
            return f - 1;
        }
    }
    return 0;
}
struct Fuel { long long calls = 0, limit = 8000000; };
struct OutOfFuel {};
struct Overflow {};
typedef long long ll;
// library names are evaluated with fast native code (their schema definitions are shown in the library)
bool nativeEval(const std::string& name, const std::vector<long long>& x, long long& out) {
    const long long CAP = 4000000000000000000LL;
    auto mulc = [&](long long a, long long b) -> long long { __int128 v = (__int128)a * b; if (v > CAP) throw Overflow(); return (long long)v; };
    if (name == "add") { out = x[0] + x[1]; if (out > CAP) throw Overflow(); return true; }
    if (name == "mul") { out = mulc(x[0], x[1]); return true; }
    if (name == "pred") { out = x[0] > 0 ? x[0] - 1 : 0; return true; }
    if (name == "monus") { out = x[0] > x[1] ? x[0] - x[1] : 0; return true; }
    if (name == "exp") { long long v = 1; for (long long i = 0; i < x[1]; ++i) { v = mulc(v, x[0]); if (v == 0 || v == 1) break; } out = x[1] == 0 ? 1 : v; if (x[0] == 1) out = 1; if (x[0] == 0) out = x[1] == 0 ? 1 : 0; return true; }
    if (name == "fact") { long long v = 1; for (long long i = 2; i <= x[0]; ++i) v = mulc(v, i); out = v; return true; }
    if (name == "sg") { out = x[0] > 0 ? 1 : 0; return true; }
    if (name == "nsg") { out = x[0] > 0 ? 0 : 1; return true; }
    if (name == "dist") { out = x[0] > x[1] ? x[0] - x[1] : x[1] - x[0]; return true; }
    return false;
}
ll evalN(const NP& n, const std::vector<ll>& x, Fuel& fu) {
    if (++fu.calls > fu.limit) throw OutOfFuel();
    switch (n->k) {
        case Node::CONST: return n->n;
        case Node::Z: return 0;
        case Node::S: return x[0] + 1;
        case Node::P: return x[n->i - 1];
        case Node::NAME: { ll v; if (nativeEval(n->name, x, v)) return v; return evalN(library().at(n->name), x, fu); }
        case Node::C: {
            std::vector<ll> g;
            for (size_t i = 1; i < n->sub.size(); ++i) g.push_back(evalN(n->sub[i], x, fu));
            return evalN(n->sub[0], g, fu);
        }
        case Node::R: {
            std::vector<ll> base(x.begin(), x.end() - 1);
            ll y = x.back();
            ll v = evalN(n->sub[0], base, fu);
            for (ll t = 0; t < y; ++t) {
                std::vector<ll> a = base; a.push_back(t); a.push_back(v);
                v = evalN(n->sub[1], a, fu);
            }
            return v;
        }
        case Node::M: {
            std::vector<ll> a = x; a.push_back(0);
            for (ll y = 0;; ++y) {
                a.back() = y;
                if (evalN(n->sub[0], a, fu) == 0) return y;
            }
        }
    }
    return 0;
}
std::string show(const NP& n) {
    switch (n->k) {
        case Node::CONST: return std::to_string(n->n);
        case Node::Z: return "Z";
        case Node::S: return "S";
        case Node::P: return "P(" + std::to_string(n->i) + "," + std::to_string(n->kk) + ")";
        case Node::NAME: return n->name;
        case Node::M: return "M(" + show(n->sub[0]) + ")";
        case Node::R: return "R(" + show(n->sub[0]) + "; " + show(n->sub[1]) + ")";
        case Node::C: { std::string s = "C(" + show(n->sub[0]) + "; "; for (size_t i = 1; i < n->sub.size(); ++i) s += (i > 1 ? ", " : "") + show(n->sub[i]); return s + ")"; }
    }
    return "";
}
}  // namespace rf

std::string AdvancedLogic::recursiveReport(int mode, const std::string& exprText, const std::string& argText) {
    using namespace fol;
    std::ostringstream o;
    auto parseArgs = [&](const std::string& t) {
        std::vector<long long> v;
        for (auto& a : splitTop(t, ", ;")) {
            char* end = nullptr; long long x = strtoll(a.c_str(), &end, 10);
            if (*end || x < 0) throw std::invalid_argument("arguments must be natural numbers 0, 1, 2, ... (got '" + a + "')");
            v.push_back(x);
        }
        return v;
    };
    if (mode == 1) {
        o << "GOEDEL'S SCHEMA - basic functions and operations (over the natural numbers)\n\n";
        o << "  Z            zero function            Z(x) = 0\n";
        o << "  S            successor                S(x) = x + 1\n";
        o << "  P(i,k)       projection               P(i,k)(x1,...,xk) = xi\n";
        o << "  n            constant (0 arguments)   e.g. 0, 1, 7\n";
        o << "  C(f; g1,...,gm)   composition         h(x) = f(g1(x), ..., gm(x))\n";
        o << "  R(g; h)      primitive recursion      f(x,0) = g(x) ;  f(x,S(y)) = h(x, y, f(x,y))     (recursion on the LAST argument)\n";
        o << "  M(f)         minimisation (mu)        M(f)(x) = least y with f(x,y) = 0 (f defined for all smaller y); may be undefined\n\n";
        o << "Primitive recursive functions use Z, S, P, C, R only; adding M gives exactly the partial recursive (= Turing computable) functions.\n\n";
        o << "Library (all defined from the schema; when you use these NAMES the app computes them with fast native code):\n";
        static const char* samples[][3] = {{"add", "3,4", "7"}, {"mul", "3,4", "12"}, {"pred", "5", "4"}, {"monus", "7,3", "4"}, {"exp", "2,5", "32"}, {"fact", "5", "120"}, {"sg", "7", "1"}, {"nsg", "0", "1"}, {"dist", "3,8", "5"}};
        for (size_t i = 0; i < sizeof(rf::LIB) / sizeof(rf::LIB[0]); ++i) {
            rf::NP n = rf::library().at(rf::LIB[i].name);
            o << "  " << rf::LIB[i].name << "  (" << rf::arity(n) << " argument" << (rf::arity(n) == 1 ? "" : "s") << ")  =  " << rf::LIB[i].def << "\n      " << rf::LIB[i].meaning << "\n";
            rf::Fuel fu;
            std::vector<long long> a;
            for (auto& t : splitTop(samples[i][1], ",")) a.push_back(atoll(t.c_str()));
            o << "      e.g. " << rf::LIB[i].name << "(" << samples[i][1] << ") = " << rf::evalN(n, a, fu) << "\n";
        }
        return o.str();
    }
    if (mode == 2) {
        std::vector<long long> a = parseArgs(argText);
        if (a.size() != 2) throw std::invalid_argument("Ackermann needs two arguments  m, n");
        long long m = a[0], n = a[1];
        if (m > 4) throw std::invalid_argument("m is limited to 4 (A(5,n) is astronomically large)");
        std::vector<long long> st; st.push_back(m);
        long long steps = 0; const long long LIMIT = 60000000;
        while (!st.empty()) {
            long long mm = st.back(); st.pop_back();
            if (++steps > LIMIT || st.size() > 20000000) { o << "A(" << a[0] << "," << a[1] << ") is too large to compute here (more than " << LIMIT << " steps).\n"; return o.str(); }
            if (mm == 0) n = n + 1;
            else if (n == 0) { st.push_back(mm - 1); n = 1; }
            else { st.push_back(mm - 1); st.push_back(mm); n = n - 1; }
        }
        o << "Ackermann function:  A(0,n) = n+1 ;  A(m+1,0) = A(m,1) ;  A(m+1,n+1) = A(m, A(m+1,n))\n\n";
        o << "A(" << a[0] << "," << a[1] << ") = " << n << "      (" << steps << " recursive steps)\n\n";
        o << "A is total and computable (by a Turing machine / mu-recursive definition) but NOT primitive recursive:\n";
        o << "it grows faster than every primitive recursive function. So primitive recursion is strictly weaker than computability.\n";
        o << "Values: A(1,n)=n+2, A(2,n)=2n+3, A(3,n)=2^(n+3)-3, A(4,0)=13, A(4,1)=65533.\n";
        return o.str();
    }
    rf::NP e = rf::Parser(exprText).parse();
    int ar = rf::arity(e);
    std::vector<long long> args = trim(argText).empty() ? std::vector<long long>() : parseArgs(argText);
    if ((int)args.size() != ar) throw std::invalid_argument("this function takes " + std::to_string(ar) + " argument(s) but " + std::to_string(args.size()) + " were given");
    rf::Fuel fu;
    o << "Function:  " << rf::show(e) << "     arity " << ar << "\n";
    o << "Arguments: (";
    for (size_t i = 0; i < args.size(); ++i) o << (i ? ", " : "") << args[i];
    o << ")\n\n";
    try {
        long long v = rf::evalN(e, args, fu);
        o << "VALUE = " << v << "      (" << fu.calls << " function applications)\n";
    } catch (const rf::Overflow&) {
        o << "The value is too large for 64-bit integers (more than 4 * 10^18).\n";
    } catch (const rf::OutOfFuel&) {
        o << "No value found within " << fu.limit << " steps.\nEither the computation is just very long (primitive recursive functions can grow extremely fast)\nor the minimisation M(f) never finds a zero: M defines PARTIAL functions, which may be undefined (like a non-halting program).\n";
    }
    return o.str();
}

// =====================================================================
//  Part 2: Goedel numbering
// =====================================================================
namespace godel {
struct Big {
    std::vector<unsigned> d;                       // base 1e9, little endian
    Big() { d.push_back(1); }
    void mulSmall(unsigned m) {
        unsigned long long carry = 0;
        for (auto& x : d) { unsigned long long v = (unsigned long long)x * m + carry; x = (unsigned)(v % 1000000000ULL); carry = v / 1000000000ULL; }
        while (carry) { d.push_back((unsigned)(carry % 1000000000ULL)); carry /= 1000000000ULL; }
    }
    unsigned remSmall(unsigned m) const {
        unsigned long long r = 0;
        for (size_t i = d.size(); i-- > 0;) r = (r * 1000000000ULL + d[i]) % m;
        return (unsigned)r;
    }
    void divSmall(unsigned m) {
        unsigned long long r = 0;
        for (size_t i = d.size(); i-- > 0;) { unsigned long long cur = r * 1000000000ULL + d[i]; d[i] = (unsigned)(cur / m); r = cur % m; }
        while (d.size() > 1 && d.back() == 0) d.pop_back();
    }
    bool isOne() const { return d.size() == 1 && d[0] == 1; }
    bool isZero() const { return d.size() == 1 && d[0] == 0; }
    std::string str() const {
        std::string s = std::to_string(d.back());
        for (size_t i = d.size() - 1; i-- > 0;) { std::string t = std::to_string(d[i]); s += std::string(9 - t.size(), '0') + t; }
        return s;
    }
    static bool parse(const std::string& t, Big& out) {
        if (t.empty()) return false;
        for (char c : t) if (!isdigit((unsigned char)c)) return false;
        out.d.assign(1, 0);
        for (char c : t) { out.mulSmall(10); unsigned long long carry = c - '0'; for (auto& x : out.d) { unsigned long long v = x + carry; x = (unsigned)(v % 1000000000ULL); carry = v / 1000000000ULL; if (!carry) break; } if (carry) out.d.push_back((unsigned)carry); }
        while (out.d.size() > 1 && out.d.back() == 0) out.d.pop_back();
        return true;
    }
};
const std::vector<unsigned>& primes() {
    static std::vector<unsigned> p;
    if (p.empty()) for (unsigned n = 2; p.size() < 400; ++n) { bool ok = true; for (unsigned q : p) { if (q * q > n) break; if (n % q == 0) { ok = false; break; } } if (ok) p.push_back(n); }
    return p;
}
int codeOf(const std::string& t) {
    static const std::map<std::string, int> tbl = {{"~", 1}, {"&", 2}, {"|", 3}, {"->", 4}, {"<->", 5}, {"forall", 6}, {"exists", 7}, {"(", 8}, {")", 9}, {",", 10}, {"=", 11}, {"!=", 12}, {"+", 13}, {"*", 14}};
    auto it = tbl.find(t);
    if (it != tbl.end()) return it->second;
    if (t.size() == 1 && islower((unsigned char)t[0])) return 20 + (t[0] - 'a' + 1);
    if (t.size() == 1 && isupper((unsigned char)t[0])) return 50 + (t[0] - 'A' + 1);
    if (t.size() == 1 && isdigit((unsigned char)t[0])) return 100 + (t[0] - '0');
    return -1;
}
std::string symOf(int c) {
    static const char* names[] = {"", "~", "&", "|", "->", "<->", "forall", "exists", "(", ")", ",", "=", "!=", "+", "*"};
    if (c >= 1 && c <= 14) return names[c];
    if (c >= 21 && c <= 46) return std::string(1, (char)('a' + c - 21));
    if (c >= 51 && c <= 76) return std::string(1, (char)('A' + c - 51));
    if (c >= 100 && c <= 109) return std::string(1, (char)('0' + c - 100));
    return "";
}
std::vector<std::string> tokenize(const std::string& s) {
    std::string t = MathLogic::normalizeSymbols(s);
    std::vector<std::string> out;
    for (size_t i = 0; i < t.size();) {
        if (isspace((unsigned char)t[i])) { ++i; continue; }
        static const char* multi[] = {"<->", "->", "!=", "forall", "exists"};
        bool done = false;
        for (const char* m : multi) if (t.compare(i, strlen(m), m) == 0) { out.push_back(m); i += strlen(m); done = true; break; }
        if (done) continue;
        out.push_back(std::string(1, t[i++]));
    }
    return out;
}
}  // namespace godel

std::string AdvancedLogic::godelReport(int mode, const std::string& text) {
    using namespace godel;
    std::ostringstream o;
    const auto& pr = primes();
    if (mode == 0) {
        auto toks = tokenize(text);
        if (toks.empty()) throw std::invalid_argument("enter a formula, e.g.  forall x (P(x) -> Q(x))");
        if (toks.size() > 80) throw std::invalid_argument("the formula is too long (max 80 symbols)");
        Big g;
        o << "Goedel numbering of the string:  " << trim(text) << "\n\nSymbol codes: ~1 &2 |3 ->4 <->5 forall6 exists7 (8 )9 ,10 =11 !=12 +13 *14 ; letters a..z = 21..46, A..Z = 51..76, digits 0..9 = 100..109\n";
        o << "Goedel number  =  p1^c1 * p2^c2 * ... * pn^cn   (p_i = i-th prime, c_i = code of the i-th symbol)\n\n";
        o << "  i   symbol    code   prime power\n";
        for (size_t i = 0; i < toks.size(); ++i) {
            int c = codeOf(toks[i]);
            if (c < 0) throw std::invalid_argument("the symbol '" + toks[i] + "' is not in the coding table");
            for (int k = 0; k < c; ++k) g.mulSmall(pr[i]);
            char line[128];
            snprintf(line, sizeof line, "  %-3zu %-8s  %-5d  %u^%d\n", i + 1, toks[i].c_str(), c, pr[i], c);
            o << line;
        }
        std::string s = g.str();
        o << "\nGOEDEL NUMBER (" << s.size() << " digit" << (s.size() == 1 ? "" : "s") << "):\n" << s << "\n\n";
        o << "By the fundamental theorem of arithmetic the number determines the string uniquely: factorise it (use 'Decode a number') to read the formula back.\n";
        o << "This is the basis of arithmetisation of syntax: statements ABOUT formulas and proofs become statements about natural numbers.\n";
        return o.str();
    }
    if (mode == 1) {
        Big g;
        if (!Big::parse(trim(text), g) || g.isZero()) throw std::invalid_argument("enter a positive whole number (digits only)");
        std::vector<int> ex;
        size_t i = 0;
        for (; i < pr.size() && !g.isOne(); ++i) {
            int e = 0;
            while (g.remSmall(pr[i]) == 0) { g.divSmall(pr[i]); ++e; }
            if (e == 0) break;
            ex.push_back(e);
        }
        o << "Decoding " << trim(text) << "\n\n";
        if (!g.isOne()) {
            o << "The number is NOT of the form p1^a1 * p2^a2 * ... * pn^an with every ai >= 1\n(either some prime is skipped or it has a prime factor beyond the first " << ex.size() + 1 << " primes), so it is not a Goedel number of a string.\n";
            if (!ex.empty()) { o << "Exponents found for the first primes: "; for (size_t k = 0; k < ex.size(); ++k) o << (k ? ", " : "") << ex[k]; o << "\n"; }
            return o.str();
        }
        o << "Prime factorisation:  ";
        for (size_t k = 0; k < ex.size(); ++k) o << (k ? " * " : "") << pr[k] << "^" << ex[k];
        o << "\nExponents: ";
        for (size_t k = 0; k < ex.size(); ++k) o << (k ? ", " : "") << ex[k];
        o << "\n\nAs a string (exponent = symbol code): ";
        std::string str; bool ok = true;
        for (int e : ex) { std::string s = symOf(e); if (s.empty()) ok = false; str += s + (s.size() > 1 ? " " : ""); }
        if (ok) o << str << "\n"; else o << "(some exponent is not a symbol code)\n";
        o << "As a sequence  <a1,...,an>  coded by exponent = ai + 1:  <";
        for (size_t k = 0; k < ex.size(); ++k) o << (k ? "," : "") << ex[k] - 1;
        o << ">\n";
        return o.str();
    }
    if (mode == 2) {
        std::vector<unsigned long long> v;
        for (auto& t : fol::splitTop(text, ", ;")) { char* end = nullptr; unsigned long long x = strtoull(t.c_str(), &end, 10); if (*end) throw std::invalid_argument("enter natural numbers"); v.push_back(x); }
        if (v.size() == 2) {
            if (v[0] > 1000000000ULL || v[1] > 1000000000ULL) throw std::invalid_argument("numbers are limited to 10^9 here");
            unsigned long long x = v[0], y = v[1], s = x + y;
            unsigned long long z = s * (s + 1) / 2 + y;
            o << "Cantor pairing function  pi(x,y) = (x+y)(x+y+1)/2 + y\n\n  pi(" << x << ", " << y << ") = " << z << "\n\n";
            o << "pi is a bijection N x N -> N (it walks through the plane along the diagonals), so N x N is countable.\n";
            o << "Check by unpairing: " << z << " -> (" << x << ", " << y << ")\n";
            return o.str();
        }
        if (v.size() == 1) {
            unsigned long long n = v[0];
            unsigned long long w = (unsigned long long)((std::sqrt(8.0 * (double)n + 1.0) - 1.0) / 2.0);
            while (w * (w + 1) / 2 > n) --w;
            while ((w + 1) * (w + 2) / 2 <= n) ++w;
            unsigned long long t = w * (w + 1) / 2, y = n - t, x = w - y;
            o << "Inverse of the Cantor pairing function\n\n  pi^-1(" << n << ") = (" << x << ", " << y << ")\n\n  w = floor((sqrt(8n+1)-1)/2) = " << w << ",  y = n - w(w+1)/2 = " << y << ",  x = w - y = " << x << "\n";
            return o.str();
        }
        throw std::invalid_argument("enter two numbers  x,y  to pair, or one number n to unpair");
    }
    // mode 3: sequence coding
    std::vector<unsigned> seq;
    for (auto& t : fol::splitTop(text, ", ;")) { char* end = nullptr; unsigned long x = strtoul(t.c_str(), &end, 10); if (*end) throw std::invalid_argument("enter natural numbers separated by commas"); seq.push_back((unsigned)x); }
    if (seq.empty() || seq.size() > 40) throw std::invalid_argument("enter 1 to 40 natural numbers");
    Big g;
    o << "Sequence coding  <a1,...,an>  =  p1^(a1+1) * p2^(a2+1) * ... * pn^(an+1)\n\n";
    for (size_t i = 0; i < seq.size(); ++i) {
        if (seq[i] > 200) throw std::invalid_argument("entries are limited to 200");
        for (unsigned k = 0; k <= seq[i]; ++k) g.mulSmall(pr[i]);
        o << "  a" << i + 1 << " = " << seq[i] << "   ->  " << pr[i] << "^" << seq[i] + 1 << "\n";
    }
    o << "\nCode = " << g.str() << "\n\nThe code determines the sequence (length = number of primes dividing it); decoding is primitive recursive. This is how finite sequences,\nformulas and proofs are represented by single numbers in the proof of the incompleteness theorems.\n";
    return o.str();
}

// =====================================================================
//  Overview of the advanced notes
// =====================================================================
std::string AdvancedLogic::advancedOverview() {
    return
"ADVANCED LOGIC - OVERVIEW  (after the LMU / MCMP lecture notes 'Advanced Logic', Part I and II)\n"
"\n"
"PART I - MODELS AND PROOFS\n"
"\n"
"First-order languages\n"
"  A language L has constants, function symbols, relation (predicate) symbols with arities, variables,\n"
"  connectives ~ & | -> <->, quantifiers forall / exists and (optionally) equality.\n"
"  Terms: variables, constants, f(t1..tn).  Atomic formulas: R(t1..tn), t1 = t2.\n"
"  comp(phi): atoms 0; ~phi, forall x phi, exists x phi: comp+1; binary: max+1 (induction on complexity).\n"
"  Free / bound variables; a SENTENCE has no free variables. Substitution phi(t/x) must avoid variable capture.\n"
"\n"
"Models and satisfaction (Tarski)\n"
"  A model M = (D, I): non-empty domain D, constants -> elements, f -> functions D^n -> D, R -> subsets of D^n.\n"
"  Satisfaction relative to an assignment s:  M,s |= forall x phi  iff  M,s[x:=d] |= phi for ALL d in D;\n"
"  exists: for SOME d.  For a sentence, truth does not depend on s.\n"
"  Valid: true in all models.  Satisfiable: true in some model.  Gamma |= phi: every model of Gamma satisfies phi.\n"
"  Counting: a relation of arity k on n elements has 2^(n^k) interpretations.\n"
"\n"
"Proof systems\n"
"  Tableaux (this app): start with premises and the negated conclusion; apply alpha rules (& , ~|, ~->, ~~),\n"
"  beta rules (| , -> , ~& , <->: branching), delta rules (exists / ~forall: introduce a NEW constant) and\n"
"  gamma rules (forall / ~exists: instantiate with ANY constant). A branch closes on phi and ~phi.\n"
"  All branches closed  =>  Gamma |- phi.  A complete open branch gives a counter-model (its constants = the domain).\n"
"  Natural deduction: introduction / elimination rules for each connective and quantifier (forall-intro needs an\n"
"  arbitrary constant, exists-elim a fresh one).  Deduction theorem: Gamma, phi |- psi  iff  Gamma |- phi -> psi.\n"
"\n"
"Soundness, completeness, compactness\n"
"  Soundness: Gamma |- phi  implies  Gamma |= phi.   Completeness (Goedel 1929): Gamma |= phi  implies  Gamma |- phi.\n"
"  Compactness: Gamma has a model iff every finite subset has one.  Consequence: infinite (non-standard) models of\n"
"  arithmetic exist, and 'finite' is not first-order definable.\n"
"  Loewenheim-Skolem: a satisfiable countable theory has a countable model (and infinite models of every infinite size).\n"
"  First-order validity is undecidable (Church, Turing 1936) but semi-decidable (tableaux / resolution find proofs).\n"
"\n"
"Model theory\n"
"  Isomorphism: a bijection preserving constants, relations and functions. Isomorphic models are elementarily equivalent\n"
"  (satisfy the same sentences); finite models are elementarily equivalent iff isomorphic.\n"
"  Submodel N <= M: N's domain is a subset, relations are restrictions, functions are closed.\n"
"  Sigma_1 (exists* ...) sentences go UP from N to M; Pi_1 (forall* ...) sentences go DOWN from M to N.\n"
"\n"
"Cardinality\n"
"  A ~ B iff there is a bijection.  N ~ Z ~ Q (countable); Cantor's diagonal argument shows P(N) and the reals are\n"
"  uncountable; Cantor's theorem: |S| < |P(S)| for every set S (diagonal set D = {x : x not in f(x)}).\n"
"\n"
"PART II - RECURSION THEORY, ARITHMETIC AND INCOMPLETENESS\n"
"\n"
"Computability\n"
"  Turing machines: finite control, tape, head. Goedel's schema: Z, S, P(i,k), composition C, primitive recursion R\n"
"  (total functions: addition, multiplication, exponentiation, factorial, ...), minimisation M (partial functions).\n"
"  Ackermann's function is total and computable but not primitive recursive.\n"
"  Church-Turing thesis: the effectively computable functions are exactly the Turing computable = partial recursive ones.\n"
"  Halting problem (diagonal argument): no machine decides whether machine e halts on input e. Recursive sets are\n"
"  decidable; recursively enumerable sets are semi-decidable (the halting set K is r.e. but not recursive).\n"
"\n"
"Arithmetic and incompleteness\n"
"  Arithmetisation of syntax: Goedel numbers code formulas, sequences and proofs by numbers; syntactic notions\n"
"  (formula, proof, provable) are recursive / r.e. relations, representable in arithmetic.\n"
"  Diagonal lemma: for every formula psi(x) there is a sentence G with  PA |- G <-> psi(code of G).\n"
"  Goedel's first incompleteness theorem: a consistent, effectively axiomatised theory containing enough arithmetic\n"
"  (Robinson's Q) has a true sentence it cannot prove (G says 'I am not provable').\n"
"  Second theorem: such a theory cannot prove its own consistency.  Tarski: truth in the standard model is not definable.\n"
"\n"
"Features in this app\n"
"  Formula analyzer, model checker, finite model finder, tableau prover, normal forms (NNF / prenex / Skolem / clauses),\n"
"  resolution prover, unification, isomorphism / submodel checks, Cantor diagonal, Turing machine simulator,\n"
"  recursive functions (Z S P C R M), Ackermann, Goedel numbering and Cantor pairing.\n";
}