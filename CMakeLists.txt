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

# main.cpp เป็น GUI ที่ใช้ Xlib (Linux): sudo apt install libx11-dev
find_package(X11 REQUIRED)
include_directories(${X11_INCLUDE_DIR})

# สร้าง Executable จากไฟล์ .cpp ทั้งหมดที่หาเจอ
add_executable(MyCalculator ${SOURCES})

target_link_libraries(MyCalculator PRIVATE ${X11_LIBRARIES})

if(MSVC)
    target_compile_options(MyCalculator PRIVATE /W3)
else()
    target_compile_options(MyCalculator PRIVATE -Wall)
endif()