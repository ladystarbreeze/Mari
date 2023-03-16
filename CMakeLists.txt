cmake_minimum_required(VERSION 3.2)
project(Mari CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

set(SOURCES
    src/main.cpp
    src/common/file.cpp
    src/core/intc.cpp
    src/core/Mari.cpp
    src/core/scheduler.cpp
    src/core/bus/bus.cpp
    src/core/cpu/cop0.cpp
    src/core/cpu/cpu.cpp
    src/core/dmac/dmac.cpp
    src/core/gpu/gpu.cpp
    src/core/timer/timer.cpp
)

set(HEADERS
    src/common/file.hpp
    src/common/types.hpp
    src/core/intc.hpp
    src/core/Mari.hpp
    src/core/scheduler.hpp
    src/core/bus/bus.hpp
    src/core/cpu/cop0.hpp
    src/core/cpu/cpu.hpp
    src/core/dmac/dmac.hpp
    src/core/gpu/gpu.hpp
    src/core/timer/timer.hpp
)

add_executable(Mari ${SOURCES} ${HEADERS})
