cmake_minimum_required(VERSION 3.2)
project(Mari CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-O1 -Wall -Wextra)

set(SOURCES
    src/main.cpp
    src/common/file.cpp
    src/core/intc.cpp
    src/core/Mari.cpp
    src/core/scheduler.cpp
    src/core/bus/bus.cpp
    src/core/cdrom/cdrom.cpp
    src/core/cpu/cop0.cpp
    src/core/cpu/cpu.cpp
    src/core/cpu/gte.cpp
    src/core/dmac/dmac.cpp
    src/core/gpu/gpu.cpp
    src/core/mdec/mdec.cpp
    src/core/sio/sio.cpp
    src/core/spu/spu.cpp
    src/core/timer/timer.cpp
)

set(HEADERS
    src/common/file.hpp
    src/common/types.hpp
    src/core/intc.hpp
    src/core/Mari.hpp
    src/core/scheduler.hpp
    src/core/bus/bus.hpp
    src/core/cdrom/cdrom.hpp
    src/core/cpu/cop0.hpp
    src/core/cpu/cpu.hpp
    src/core/cpu/gte.hpp
    src/core/dmac/dmac.hpp
    src/core/gpu/gpu.hpp
    src/core/mdec/mdec.hpp
    src/core/sio/sio.hpp
    src/core/spu/gauss.hpp
    src/core/spu/spu.hpp
    src/core/timer/timer.hpp
)

find_package(SDL2 REQUIRED)
include_directories(Mari ${SDL2_INCLUDE_DIRS})

add_executable(Mari ${SOURCES} ${HEADERS})
target_link_libraries(Mari ${SDL2_LIBRARIES})
