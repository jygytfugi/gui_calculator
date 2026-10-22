#ifndef MENU_HPP
#define MENU_HPP

void clearScreen();

// ทุกฟังก์ชันเมนูรับพารามิเตอร์ selected = หมายเลขตัวเลือกที่กำลังถูกไฮไลต์อยู่
// (สำหรับวาดสัญลักษณ์ > นำหน้าตัวเลือกนั้น) ค่าเริ่มต้น 0 = ไม่ไฮไลต์ตัวใดเลย
void printMainMenu(int selected = 0);
void printAlgebraMenu();
void printCalculusMenu(int selected = 0);
void printEquationMenu(int selected = 0);
void printGraphMenu(int selected = 0);
void printMatrixMenu(int selected = 0);
void printTableMenu(int selected = 0);
void printFunctionMenu(int selected = 0); // [7] Relations & Functions

// เพิ่มการประกาศฟังก์ชันเมนูสำหรับควบคุมกราฟ Interactive
void resolution_graph(int selected = 0);

// เลือกตัวเลือกเมนูแบบ Interactive ด้วยปุ่ม w (ขึ้น) / s (ลง) แล้วกด Enter เพื่อยืนยัน
// - printFn: ฟังก์ชันแสดงเมนู รับพารามิเตอร์ selected เพื่อวาดสัญลักษณ์ > นำหน้าตัวที่ไฮไลต์
// - numOptions: จำนวนตัวเลือกทั้งหมด (1..numOptions)
// คืนค่า: หมายเลขตัวเลือกที่เลือก (1..numOptions),
//         0 = กด 0/q หรือเลือกออก/กลับเมนูก่อนหน้า,
//        -1 = กด c เพื่อเคลียร์หน้าจอ
int selectMenu(void (*printFn)(int), int numOptions);

#endif // MENU_HPP