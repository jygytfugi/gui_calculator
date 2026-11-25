cmake_minimum_required(VERSION 3.15)
project(MyCalculator CXX)

# กำหนดมาตรฐาน C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ถ้าไม่ระบุ ให้ build แบบ Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ระบุโฟลเดอร์สำหรับ Header Files (.hpp)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

# ดึงไฟล์ .cpp ทั้งหมดใน root directory และในโฟลเดอร์ src/ โดยอัตโนมัติ
file(GLOB SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# ค้นหาแพ็กเกจ Wayland, libdecor, xkbcommon และ cairo ผ่าน pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(Wayland REQUIRED 
    wayland-client
    wayland-cursor
    wayland-egl
    xkbcommon
    libdecor-0
    cairo
)

# เพิ่ม Include และ Library directories ของ Wayland/libdecor/cairo
include_directories(${Wayland_INCLUDE_DIRS})
link_directories(${Wayland_LIBRARY_DIRS})

# สร้าง Executable จากไฟล์ .cpp ทั้งหมดที่หาเจอ
add_executable(MyCalculator ${SOURCES})

# ลิงก์ไลบรารีทั้งหมด
target_link_libraries(MyCalculator PRIVATE ${Wayland_LIBRARIES})

if(MSVC)
    target_compile_options(MyCalculator PRIVATE /W3)
else()
    target_compile_options(MyCalculator PRIVATE -Wall)
endif()