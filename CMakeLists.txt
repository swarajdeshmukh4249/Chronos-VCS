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

# Bundle the C++ runtime into each .exe on MinGW. Otherwise Windows may load an older
# libstdc++-6.dll from PATH (Git Bash ships one) and the program fails to start.
if(MINGW)
    add_link_options(-static)
endif()

# MSVC calls getenv and localtime "unsafe" because they aren't thread-safe. Chrono
# is single-threaded and checks their results, so the standard functions are fine.
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_executable(chrono src/main.cpp ${CHRONO_SOURCES})
target_include_directories(chrono PRIVATE include src)

# Spreadsheet layer tests. Run with: ctest --test-dir build
file(GLOB SHEET_SOURCES CONFIGURE_DEPENDS src/sheet/*.cpp)
file(GLOB SHEET_TESTS CONFIGURE_DEPENDS tests/sheet/*.cpp)
add_executable(sheet_tests tests/common/TestMain.cpp ${SHEET_TESTS} ${SHEET_SOURCES})
target_include_directories(sheet_tests PRIVATE include src tests/common)

# Core layer tests. The core merges .csv files with the sheet layer's CellMerge, so it links those too.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB CORE_TESTS CONFIGURE_DEPENDS tests/core/*.cpp)
add_executable(core_tests tests/common/TestMain.cpp ${CORE_TESTS} ${CORE_SOURCES} ${SHEET_SOURCES})
target_include_directories(core_tests PRIVATE include src tests/common)

# Plain-English layer tests.
file(GLOB NLP_SOURCES CONFIGURE_DEPENDS src/nlp/*.cpp)
file(GLOB NLP_TESTS CONFIGURE_DEPENDS tests/nlp/*.cpp)
add_executable(nlp_tests tests/common/TestMain.cpp ${NLP_TESTS} ${NLP_SOURCES})
target_include_directories(nlp_tests PRIVATE include src tests/common)

enable_testing()
add_test(NAME sheet_tests COMMAND sheet_tests)
add_test(NAME core_tests COMMAND core_tests)
add_test(NAME nlp_tests COMMAND nlp_tests)

foreach(target chrono sheet_tests core_tests nlp_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
