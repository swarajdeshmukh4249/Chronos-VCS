cmake_minimum_required(VERSION 3.16)
project(ChronoVCS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Each layer lives in its own folder; new .cpp files are picked up on the next build.
file(GLOB_RECURSE CHRONO_SOURCES CONFIGURE_DEPENDS
    src/core/*.cpp
    src/nlp/*.cpp
    src/sheet/*.cpp
)

add_executable(chrono src/main.cpp ${CHRONO_SOURCES})
target_include_directories(chrono PRIVATE include src)

# Spreadsheet layer tests. Run with: ctest --test-dir build
file(GLOB SHEET_SOURCES CONFIGURE_DEPENDS src/sheet/*.cpp)
file(GLOB SHEET_TESTS CONFIGURE_DEPENDS tests/sheet/*.cpp)
add_executable(sheet_tests ${SHEET_TESTS} ${SHEET_SOURCES})
target_include_directories(sheet_tests PRIVATE include src)

enable_testing()
add_test(NAME sheet_tests COMMAND sheet_tests)

foreach(target chrono sheet_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
