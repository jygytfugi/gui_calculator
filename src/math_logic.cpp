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