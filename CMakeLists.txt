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

if(MSVC)
    target_compile_options(chrono PRIVATE /W4)
else()
    target_compile_options(chrono PRIVATE -Wall -Wextra -Wpedantic)
endif()
