#include "../lib/menu.hpp"
#include <iostream>
#include <string>

void clearScreen() {
#if defined(_WIN32) || defined(_WIN64)
    std::system("cls");
#else
    std::cout << "\033[2J\033[1;1H";
#endif
}

// ---------------------------------------------------------------------------
// อ่านปุ่มกดทีละตัวโดยไม่ต้องกด Enter (raw mode) รองรับทั้ง Windows / Linux / macOS
// รู้จักปุ่มลูกศรขึ้น-ลงด้วย (แปลงเป็น 'w' / 's' ให้อัตโนมัติ)
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
#include <conio.h>
static int readKey() {
    int ch = _getch();
    if (ch == 0 || ch == 224) { // prefix ของปุ่มลูกศร / ปุ่มพิเศษบน Windows
        int ch2 = _getch();
        if (ch2 == 72) return 'w'; // Arrow Up
        if (ch2 == 80) return 's'; // Arrow Down
        return 0;
    }
    return ch;
}
#else
#include <termios.h>
#include <unistd.h>
static int readKey() {
    termios oldt{}, newt{};
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    int ch = getchar();
    if (ch == 27) { // ESC — อาจเป็นจุดเริ่มต้นของปุ่มลูกศร (ESC [ A/B)
        int ch2 = getchar();
        if (ch2 == '[') {
            int ch3 = getchar();
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            if (ch3 == 'A') return 'w'; // Arrow Up
            if (ch3 == 'B') return 's'; // Arrow Down
            return 27;
        }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}
#endif

// วาดสัญลักษณ์ > (สีเขียว) นำหน้าตัวเลือกที่กำลังไฮไลต์อยู่ ตัวอื่นเว้นช่องว่างเท่ากัน
static std::string cursor(int idx, int selected) {
    return (idx == selected) ? "\033[1;32m> \033[0m" : "  ";
}

// ---------------------------------------------------------------------------
// ลูปควบคุมการเลือกเมนูแบบ Interactive: w = ขึ้น, s = ลง, Enter = ยืนยัน
// พิมพ์เลข 1-9 = เลือกตัวนั้นทันที (เผื่อผู้ใช้ถนัดพิมพ์แบบเดิม)
// 0 หรือ q = กลับ/ออก, c = เคลียร์หน้าจอ
// ---------------------------------------------------------------------------
int selectMenu(void (*printFn)(int), int numOptions) {
    int selected = 1;

    while (true) {
        clearScreen();
        printFn(selected);
        int key = readKey();

        if (key == 'w' || key == 'W') {
            selected = (selected <= 1) ? numOptions : selected - 1;
        } else if (key == 's' || key == 'S') {
            selected = (selected >= numOptions) ? 1 : selected + 1;
        } else if (key == '\r' || key == '\n') {
            return selected;
        } else if (key == '0' || key == 'q' || key == 'Q') {
            return 0;
        } else if (key == 'c' || key == 'C') {
            return -1;
        } else if (key >= '1' && key <= '9') {
            int n = key - '0';
            if (n <= numOptions) return n;
        }
        // ปุ่มอื่นที่ไม่รู้จัก -> วนซ้ำแสดงเมนูใหม่เฉย ๆ
    }
}

void printMainMenu(int selected) {
    std::cout << "\033[1;36m============================================================\033[0m\n"
              << "\033[1;33m                   ★ MATHS SOLVER SYSTEM ★                  \033[0m\n"
              << "\033[1;36m============================================================\033[0m\n"
              << "  " << cursor(1, selected) << "\033[1;32m[1]\033[0m Algebra Calculator (Continuous Calc & Memory)\n"
              << "  " << cursor(2, selected) << "\033[1;32m[2]\033[0m Calculus Solver (Derivative, Integral, Limit)\n"
              << "  " << cursor(3, selected) << "\033[1;32m[3]\033[0m Alpha Equation Solver (Polynomial, Factorization, General)\n"
              << "  " << cursor(4, selected) << "\033[1;32m[4]\033[0m 2D Text Graph Plotter\n"
              << "  " << cursor(5, selected) << "\033[1;32m[5]\033[0m Multi-Matrix Calculator (N Matrices & Calculus)\n"
              << "  " << cursor(6, selected) << "\033[1;32m[6]\033[0m Table & Probability Calculator (Statistics & Dist)\n"
              << "  " << cursor(7, selected) << "\033[1;32m[7]\033[0m Relations & Functions (Domain, Range, Inverse, Composition)\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  \033[1;33m[c]\033[0m Clear Screen\n"
              << "  \033[1;31m[0]\033[0m Exit Program\n"
              << "\033[1;36m============================================================\033[0m\n"
                << "\033[1;90m(Use w/s to scroll the menu, Enter to confirm, or type the number)\033[0m\n"
                << "\033[1;35mChoose category > \033[0m";
}

void printAlgebraMenu() {
    std::cout << "\033[1;36m============================================================\033[0m\n"
              << "\033[1;33m                ★ [1] ALGEBRA CALCULATOR ★                  \033[0m\n"
              << "\033[1;36m============================================================\033[0m\n"
              << "\033[1;37mAvailable Functions & Operations:\033[0m\n"
              << "  \033[1;32m1. Basic Arithmetic\033[0m : +, -, *, /, ^ (Power), % (Percentage)\n"
              << "  \033[1;32m2. Absolute Value  \033[0m : |x| or abs(x)\n"
              << "  \033[1;32m3. Factorial       \033[0m : fact(n)  [e.g., fact(5)]\n"
              << "  \033[1;32m4. Roots           \033[0m : sqrt(x) or sqrt(x, n), root(x, n)\n"
              << "  \033[1;32m5. Logarithms      \033[0m : log(x) [base 10], log(x, base), ln(x)\n"
              << "  \033[1;32m6. Exponential     \033[0m : exp(x)\n"
              << "  \033[1;32m7. Trigonometry    \033[0m : sin(x), cos(x), tan(x)\n"
              << "  \033[1;32m8. Inverse Trigo   \033[0m : asin(x), acos(x), atan(x)\n"
              << "  \033[1;32m9. Constants       \033[0m : pi, e, inf\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  \033[1;37mCommands:\033[0m\n"
              << "  - Use \033[1;33mans\033[0m to reference previous result (e.g., 'ans + 10', 'sqrt(ans)')\n"
              << "  - Type \033[1;33m'clear'\033[0m or \033[1;33m'c'\033[0m to clear screen\n"
              << "  - Type \033[1;31m'exit'\033[0m or \033[1;31m'menu'\033[0m to return to Main Menu\n"
              << "\033[1;36m============================================================\033[0m\n";
}

void printCalculusMenu(int selected) {
    std::cout << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "\033[1;33m                   --- Calculus Menu ---                    \033[0m\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  " << cursor(1, selected) << "\033[1;32m1)\033[0m Derivative (d/dx f(x) at x0)\n"
              << "  " << cursor(2, selected) << "\033[1;32m2)\033[0m Definite / Improper Integral (f(x) over [a, b])\n"
              << "  " << cursor(3, selected) << "\033[1;32m3)\033[0m Indefinite Integral Function (F(x) from x_ref to x0)\n"
              << "  " << cursor(4, selected) << "\033[1;32m4)\033[0m Limit of Function (lim x->a f(x), or one-sided x->a+ / x->a-)\n"
              << "  " << cursor(5, selected) << "\033[1;32m5)\033[0m Symbolic Derivative (f'(x) as a formula, no x0 substituted)\n"
              << "  " << cursor(6, selected) << "\033[1;32m6)\033[0m Symbolic Indefinite Integral (F(x) as a formula, no x0 substituted)\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  \033[1;33m[c]\033[0m Clear Screen  |  \033[1;31m[0]\033[0m Back to Main Menu\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
                << "\033[1;90m(Use w/s to scroll the menu, Enter to confirm, or type the number)\033[0m\n"
                << "\033[1;35mChoose category > \033[0m";
}

void printEquationMenu(int selected) {
    std::cout << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "\033[1;33m               --- Alpha Equation Solver ---                \033[0m\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  " << cursor(1, selected) << "\033[1;32m1)\033[0m Polynomial Solver (Degree 2, 3, 4, N)\n"
              << "  " << cursor(2, selected) << "\033[1;32m2)\033[0m Polynomial Factorization\n"
              << "  " << cursor(3, selected) << "\033[1;32m3)\033[0m General Equation Solver f(x) = 0\n"
              << "  " << cursor(4, selected) << "\033[1;32m4)\033[0m Solve Derivative Equation f'(x) = targetValue\n"
              << "  " << cursor(5, selected) << "\033[1;32m5)\033[0m Solve Integral Bound Integral[a to b] f(x) dx = targetValue\n"
              << "  " << cursor(6, selected) << "\033[1;32m6)\033[0m Solve Limit Equation lim(x->a) f(x) = targetValue\n"
              << "  " << cursor(7, selected) << "\033[1;32m7)\033[0m Solve Indefinite Integral Equation F(x) = targetValue\n"
              << "  " << cursor(8, selected) << "\033[1;32m8)\033[0m Solve Two-Sided Equation (lhs = rhs)\n"
              << "  " << cursor(9, selected) << "\033[1;32m9)\033[0m Solve Linear System (choose # of equations; any # of variables, e.g. x,y,z,...)\n"
              << "  " << cursor(10, selected) << "\033[1;32m10)\033[0m Solve Nonlinear System (choose # of equations; any # of variables)\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  \033[1;90mTip: type \033[1;33mans\033[1;90m anywhere in an equation to reuse your last result, e.g. \033[1;33mx + 2 = ans\033[0m\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  \033[1;33m[c]\033[0m Clear Screen  |  \033[1;31m[0]\033[0m Back to Main Menu\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
                << "\033[1;90m(Use w/s to scroll the menu, Enter to confirm, or type the number)\033[0m\n"
                << "\033[1;35mChoose category > \033[0m";
}

void printGraphMenu(int selected) {
    std::cout << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "\033[1;33m                --- 2D Text Graph Plotter ---               \033[0m\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  " << cursor(1, selected) << "\033[1;32m1)\033[0m Plot f(x) (e.g., sin(x), x^2 - 4)\n"
              << "  " << cursor(2, selected) << "\033[1;32m2)\033[0m Plot Polynomial Degree N with Roots\n"
              << "  " << cursor(3, selected) << "\033[1;32m3)\033[0m Plot Derivative & Tangent Line at x0\n"
              << "  " << cursor(4, selected) << "\033[1;32m4)\033[0m Plot Integral Area over [a, b]\n"
              << "  " << cursor(5, selected) << "\033[1;32m5)\033[0m Plot Limit approaching x -> a\n"
              << "  " << cursor(6, selected) << "\033[1;32m6)\033[0m Plot Indefinite Integral F(x)\n"
              << "  " << cursor(7, selected) << "\033[1;32m7)\033[0m Plot Scatter Plot from DataTable\n"
              << "  " << cursor(8, selected) << "\033[1;32m8)\033[0m Plot Area Between Two Curves f1(x) & f2(x)\n"
              << "  " << cursor(9, selected) << "\033[1;32m9)\033[0m Plot Inverse Function f^-1(x) (reflection over y = x)\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  \033[1;33m[c]\033[0m Clear Screen  |  \033[1;31m[0]\033[0m Back to Main Menu\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
                << "\033[1;90m(Use w/s to scroll the menu, Enter to confirm, or type the number)\033[0m\n"
                << "\033[1;35mChoose category > \033[0m";
}

void printMatrixMenu(int selected) {
    std::cout << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "\033[1;33m              --- Multi-Matrix Calculator ---               \033[0m\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  " << cursor(1, selected) << "\033[1;32m1)\033[0m Add N Matrices\n"
              << "  " << cursor(2, selected) << "\033[1;32m2)\033[0m Subtract N Matrices\n"
              << "  " << cursor(3, selected) << "\033[1;32m3)\033[0m Multiply N Matrices\n"
              << "  " << cursor(4, selected) << "\033[1;32m4)\033[0m Transpose Matrix (A^T)\n"
              << "  " << cursor(5, selected) << "\033[1;32m5)\033[0m Determinant (det(A))\n"
              << "  " << cursor(6, selected) << "\033[1;32m6)\033[0m Inverse Matrix (A^-1)\n"
              << "  " << cursor(7, selected) << "\033[1;32m7)\033[0m Matrix Derivative\n"
              << "  " << cursor(8, selected) << "\033[1;32m8)\033[0m Matrix Integral\n"
              << "  " << cursor(9, selected) << "\033[1;32m9)\033[0m Matrix Indefinite Integral\n"
              << "  " << cursor(10, selected) << "\033[1;32m10)\033[0m Plot Scatter Plot from DataTable\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  \033[1;33m[c]\033[0m Clear Screen  |  \033[1;31m[0]\033[0m Back to Main Menu\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
                << "\033[1;90m(Use w/s to scroll the menu, Enter to confirm, or type the number)\033[0m\n"
                << "\033[1;35mChoose category > \033[0m";
}

void printTableMenu(int selected) {
    std::cout << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "\033[1;33m          --- Table & Probability Calculator ---            \033[0m\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  " << cursor(1, selected) << "\033[1;32m1)\033[0m Input Data Table & Column Statistics\n"
              << "  " << cursor(2, selected) << "\033[1;32m2)\033[0m Generate Binomial Distribution Table\n"
              << "  " << cursor(3, selected) << "\033[1;32m3)\033[0m Generate Poisson Distribution Table\n"
              << "  " << cursor(4, selected) << "\033[1;32m4)\033[0m Generate Standard Normal Z-Table\n"
              << "  " << cursor(5, selected) << "\033[1;32m5)\033[0m Probability Distributions Calculator\n"
              << "  " << cursor(6, selected) << "\033[1;32m6)\033[0m Combinatorics Calculator (nCr, nPr)\n"
              << "  " << cursor(7, selected) << "\033[1;32m7)\033[0m Contingency / Joint Probability Table\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  \033[1;33m[c]\033[0m Clear Screen  |  \033[1;31m[0]\033[0m Back to Main Menu\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
                << "\033[1;90m(Use w/s to scroll the menu, Enter to confirm, or type the number)\033[0m\n"
                << "\033[1;35mChoose category > \033[0m";
}

void printFunctionMenu(int selected) {
    std::cout << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "\033[1;33m             --- Relations & Functions ---                  \033[0m\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  " << cursor(1, selected) << "\033[1;32m1)\033[0m Cartesian Product A x B\n"
              << "  " << cursor(2, selected) << "\033[1;32m2)\033[0m Relation from Condition (Domain, Range, Inverse, Type)\n"
              << "  " << cursor(3, selected) << "\033[1;32m3)\033[0m Analyze Relation Equation (Vertical Line Test, Domain, Range)\n"
              << "  " << cursor(4, selected) << "\033[1;32m4)\033[0m Analyze Function f(x) (Domain, Range, Increasing/Decreasing, 1-1)\n"
              << "  " << cursor(5, selected) << "\033[1;32m5)\033[0m Algebra of Functions (f+g, f-g, fg, f/g)\n"
              << "  " << cursor(6, selected) << "\033[1;32m6)\033[0m Composite Functions (g o f, f o g)\n"
              << "  " << cursor(7, selected) << "\033[1;32m7)\033[0m Inverse Function f^-1\n"
              << "  " << cursor(8, selected) << "\033[1;32m8)\033[0m Function Type on A -> B (Function / Onto / One-to-one)\n"
              << "  " << cursor(9, selected) << "\033[1;32m9)\033[0m Function Notation f(x+h), f(a+1), ... (Expand / Solve, any letters)\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  \033[1;90mTip: write 2x instead of 2*x; use ans for your last result; intervals like [0,inf)\033[0m\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  \033[1;33m[c]\033[0m Clear Screen  |  \033[1;31m[0]\033[0m Back to Main Menu\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
                << "\033[1;90m(Use w/s to scroll the menu, Enter to confirm, or type the number)\033[0m\n"
                << "\033[1;35mChoose category > \033[0m";
}

void resolution_graph(int selected) {
    std::cout << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "\033[1;33m             --- Interactive Graph View Controls ---         \033[0m\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  " << cursor(1, selected) << "\033[1;32m1)\033[0m Zoom In (ระบุพิกัด x ศูนย์กลางเพื่อขยายภาพ)\n"
              << "  " << cursor(2, selected) << "\033[1;32m2)\033[0m Zoom Out (ขยายมุมมองกว้างขึ้น)\n"
              << "  " << cursor(3, selected) << "\033[1;32m3)\033[0m กำหนดช่วง xMin / xMax ใหม่โดยตรง\n"
              << "  " << cursor(4, selected) << "\033[1;32m4)\033[0m ปรับเปลี่ยน ความละเอียด (Resolution Width x Height)\n"
              << "  " << cursor(5, selected) << "\033[1;32m5)\033[0m Reset ค่ากลับสู่เริ่มต้น (xMin, xMax และ Resolution)\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
              << "  \033[1;31m[0]\033[0m (Exit Graph Zoom Mode)\n"
              << "\033[1;36m------------------------------------------------------------\033[0m\n"
                << "\033[1;90m(Use w/s to scroll the menu, Enter to confirm, or type the number)\033[0m\n"
                << "\033[1;35mChoose category > \033[0m";
}