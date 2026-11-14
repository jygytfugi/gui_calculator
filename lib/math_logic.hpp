#ifndef MATH_LOGIC_HPP
#define MATH_LOGIC_HPP

// ============================================================================
//  math_logic  -  Mathematical logic module (ตรรกศาสตร์คณิตศาสตร์)
//
//  Topics follow:
//   * Wikipedia "Mathematical logic": propositional logic and first-order logic as the
//     most studied formal systems; semantics (truth, models, logical consequence),
//     proof theory (natural deduction / inference rules), Boolean (algebraic) logic,
//     quantifiers over a domain of discourse, non-classical logic overview, key theorems.
//   * Thai logic course material (propositions, connectives, truth tables, equivalent
//     propositions, tautologies, arguments, open sentences and quantifiers).
//   * Truth-table method (2^n rows, tautology / contradiction / contingency, equivalence,
//     argument validity by "critical rows").
//
//  Formula syntax (ASCII or Unicode symbols are accepted):
//     NOT        ~  !  not  ¬
//     AND        &  &&  /\  and  ∧
//     OR         |  ||  \/  or  ∨
//     XOR        xor  ⊕
//     NAND, NOR  nand  nor
//     IMPLIES    ->  =>  implies  →  ⇒
//     IFF        <->  <=>  iff  ↔  ⇔
//     constants  T  F  true  false  (1 and 0 are numbers, not constants)
//     variables  p, q, r, P1, rain, ...   (identifiers; T and F are reserved)
//     precedence NOT > AND/NAND > XOR > OR/NOR > IMPLIES (right assoc.) > IFF
//
//  First-order style (finite domain of discourse, needs a Calculator for arithmetic):
//     comparisons   x > 2     x^2 = 4     1 < x <= 5     (=, !=, <, <=, >, >=)
//     quantifiers   forall x in {1,2,3}: x^2 > 1
//                   exists x in 1..10: x^2 = 49        (ranges a..b or a..b step s)
// ============================================================================

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

class Calculator;   // forward declaration (only used for comparisons / universes)

struct LogicNode;
using LogicPtr = std::shared_ptr<const LogicNode>;

struct LogicNode {
    enum Kind { CONST, VAR, NOT, AND, OR, XOR, NAND, NOR, IMPLIES, IFF, CMP, FORALL, EXISTS };
    Kind kind = CONST;
    bool value = false;                 // CONST
    std::string name;                   // VAR: variable name, CMP: operator, quantifier: bound variable
    std::string lhs, rhs;               // CMP: arithmetic expressions
    std::string universeText;           // quantifier: "{1,2,3}" or "1..10"
    LogicPtr a, b;                      // children (body of a quantifier is in 'a')
};

class MathLogic {
public:
    explicit MathLogic(Calculator* calc = nullptr);
    void setCalculator(Calculator* calc);

    enum class Style { ASCII, Symbols };

    // ---------------- parsing / printing ----------------
    // throws std::invalid_argument (message includes the position)
    LogicPtr parse(const std::string& text) const;
    static std::string toString(const LogicPtr& n, Style style = Style::ASCII);
    static std::string normalizeSymbols(const std::string& text);   // Unicode logic symbols -> ASCII

    // ---------------- semantics ----------------
    std::vector<std::string> variables(const LogicPtr& n) const;            // propositional variables, sorted
    bool evaluate(const LogicPtr& n, const std::map<std::string, bool>& assignment = {}) const;
    bool evaluate(const std::string& formula, const std::map<std::string, bool>& assignment = {}) const;
    static std::map<std::string, bool> parseAssignment(const std::string& text);   // "p=T, q=0"

    // ---------------- truth tables ----------------
    struct TruthTable {
        std::vector<std::string> vars;                 // input columns
        std::vector<std::string> columns;              // sub-formula columns (last = the formula)
        std::vector<std::vector<bool>> inputs;         // [row][var]   (first row = all true)
        std::vector<std::vector<bool>> values;         // [row][column]
    };
    TruthTable truthTable(const std::string& formula, bool subformulas = true) const;
    TruthTable truthTableOf(const std::vector<std::string>& formulas) const;   // one column per formula
    static std::string formatTruthTable(const TruthTable& t, bool useTF = true, size_t maxRows = 4096,
                                        const std::vector<bool>* mark = nullptr);

    // ---------------- classification / consequence ----------------
    enum class Verdict { Tautology, Contradiction, Contingency };
    struct Classification {
        Verdict kind = Verdict::Contingency;
        std::vector<std::string> vars;
        unsigned long long rows = 0, trueRows = 0;
        std::vector<unsigned long long> minterms;      // numbering: first variable = most significant bit, true = 1
    };
    Classification classify(const std::string& formula) const;
    std::string classifyReport(const std::string& formula) const;
    bool equivalent(const std::string& a, const std::string& b) const;
    bool entails(const std::string& a, const std::string& b) const;            // a |= b
    std::string equivalenceReport(const std::string& a, const std::string& b) const;
    static std::string identifyLaw(const LogicPtr& a, const LogicPtr& b);       // name of a standard law, "" if none

    // ---------------- arguments / proofs ----------------
    struct ArgumentResult {
        bool valid = false;
        std::vector<std::string> vars;
        std::vector<std::map<std::string, bool>> counterexamples;
        std::string rule;                              // e.g. "Modus Ponens (MP)"
        std::string fallacy;                           // e.g. "Affirming the consequent (fallacy)"
    };
    ArgumentResult checkArgument(const std::vector<std::string>& premises, const std::string& conclusion) const;
    std::string argumentReport(const std::vector<std::string>& premises, const std::string& conclusion) const;
    std::string proveReport(const std::vector<std::string>& premises, const std::string& goal) const;

    // ---------------- normal forms / Boolean algebra ----------------
    std::string nnf(const std::string& formula) const;               // negation normal form
    std::string canonicalDNF(const std::string& formula) const;      // sum of minterms
    std::string canonicalCNF(const std::string& formula) const;      // product of maxterms
    std::string minimalDNF(const std::string& formula) const;        // Quine-McCluskey
    std::string minimalCNF(const std::string& formula) const;
    std::string normalFormsReport(const std::string& formula) const;

    // ---------------- other reports ----------------
    std::string conditionalReport(const std::string& formula) const;               // converse / inverse / contrapositive
    std::string stepsReport(const std::string& formula, const std::map<std::string, bool>& assignment) const;
    std::string quantifierReport(const std::string& formula, const std::map<std::string, bool>& assignment = {}) const;

    // ---------------- reference texts ----------------
    static std::string lawsText();
    static std::string rulesText();
    static std::string connectivesText();
    static std::string overviewText();

private:
    Calculator* calc_ = nullptr;
    struct Env;
    bool eval(const LogicPtr& n, Env& env) const;
    std::vector<double> universe(const std::string& text) const;
    double arith(const std::string& expr, const Env& env) const;
    std::vector<std::string> unionVars(const std::vector<LogicPtr>& fs) const;
    std::vector<bool> column(const LogicPtr& f, const std::vector<std::string>& vars) const;   // truth column, first row = all true
};


// ============================================================================
//  AdvancedLogic  -  university-level logic (after Meadows, "Advanced Logic", LMU lecture notes)
//
//  Part 1  Models & Proofs: first-order languages (terms, formulae, free/bound variables, sentences,
//          complexity), models and satisfaction, validity / satisfiability / consequence, tableau proofs
//          with counter-model extraction, soundness & completeness, model theory (isomorphism,
//          elementary equivalence, submodels, Sigma1 / Pi1 preservation, compactness), cardinality and
//          Cantor's diagonal argument, Loewenheim-Skolem.
//  Part 2  Recursion & Incompleteness: Turing machines, Goedel's schema (Z, S, P, C, R, M), Ackermann,
//          Church-Turing thesis, Goedel numbering and pairing, diagonal lemma, incompleteness.
//  Plus standard university material: prenex / Skolem normal forms, clause form, unification and
//  resolution.
//
//  First-order syntax:   P(x), R(x,y), Rxy, f(x,a), x = y, x != y, ~ & | -> <-> ,
//                        forall x ..., exists x ...   (binds tightly; use  forall x. body  for a long body)
//  Conventions:          variables u v w x y z (optionally with digits), constants = other lowercase
//                        names or numbers (a, b, c, 0, 1, john), function symbols = lowercase name followed
//                        by '(', predicate symbols = names starting with an uppercase letter.
// ============================================================================
struct ModelText {
    std::string domain;        // "m1, m2"  or  "1..4"
    std::string constants;     // "a=m1, b=m2"
    std::string relations;     // "P={m1}; R={(m1,m1),(m1,m2)}"
    std::string functions;     // "f(m1)=m2, f(m2)=m1"
};

class AdvancedLogic {
public:
    enum class FinderMode { Satisfiable, Validity, Consequence, CountModels };

    // ---- Part 1 ----
    static std::string analyzeFormula(const std::string& formula);
    static std::string modelCheckReport(const std::string& formula, const ModelText& model, const std::string& assignment = "");
    static std::string findModelsReport(FinderMode mode, const std::string& statement,
                                        const std::vector<std::string>& premises, int maxSize);
    static std::string tableauReport(const std::vector<std::string>& premises, const std::string& conclusion, int maxLines = 400);
    static std::string normalFormReport(const std::string& formula);
    static std::string resolutionReport(const std::vector<std::string>& premises, const std::string& conclusion, int maxClauses = 1500);
    static std::string unifyReport(const std::string& a, const std::string& b);
    // mode: 0 isomorphic?  1 submodel?  2 compare a sentence in both (Sigma1 / Pi1 preservation)
    static std::string modelRelationReport(int mode, const ModelText& m, const ModelText& n, const std::string& sentence);
    static std::string cantorReport(const std::string& rows, const std::string& setElements);

    // ---- Part 2 ----
    static std::string turingReport(const std::string& rules, const std::string& tape, const std::string& startState,
                                    const std::string& blank, int maxSteps);
    // mode: 0 evaluate   1 show library   2 Ackermann A(m,n)
    static std::string recursiveReport(int mode, const std::string& expr, const std::string& args);
    // mode: 0 encode formula  1 decode number  2 pairing / unpairing  3 sequence coding
    static std::string godelReport(int mode, const std::string& text);

    static std::string advancedOverview();
};

#endif // MATH_LOGIC_HPP