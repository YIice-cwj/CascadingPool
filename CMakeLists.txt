cmake_minimum_required(VERSION 3.20)

# 设置项目版本(主版本.次版本.修订版本)
set(PROJECT_VERSION_MAJOR "1")
set(PROJECT_VERSION_MINOR "0")
set(PROJECT_VERSION_PATCH "0")

set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
project(cascading_pool VERSION ${PROJECT_VERSION}  LANGUAGES CXX)

# 设置C++标准(C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/W4)
    
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /RTC1 /MDd")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG")
    
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /Oi /Ot /MD")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/OPT:REF /OPT:ICF")
    
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/O2 /Ob2 /Oi /Ot /Zi /MD")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG /OPT:REF /OPT:ICF")
    
    set(CMAKE_CXX_FLAGS_MINSIZEREL "/O1 /Ob1 /Os /MD")
    set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "/OPT:REF /OPT:ICF")
    
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")
    
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -funroll-loops -fomit-frame-pointer")
    
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -march=native")
    
    set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -fdata-sections -ffunction-sections")
    set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "-Wl,--gc-sections")
endif()

# 头文件目录
set(INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 源文件目录
set(SRC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

#  测试文件目录
set(TEST_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# 设置库文件输出目录
set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# 设置可执行文件输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# 设置库名称
set(LIBRARY_NAME cascading_pool)

# 添加头文件目录
include_directories(${INCLUDE_DIRS})

# 添加源文件目录
set(SRC_FILES
    ${SRC_DIRS}/cpu_affinity.cc
    ${SRC_DIRS}/dirty_tree.cc
    ${SRC_DIRS}/muzzy_tree.cc
    ${SRC_DIRS}/in_use_tree.cc
    ${SRC_DIRS}/retained_tree.cc
    ${SRC_DIRS}/extent_manager.cc
    ${SRC_DIRS}/arena.cc
    ${SRC_DIRS}/size_class.cc
    ${SRC_DIRS}/chunk.cc
    ${SRC_DIRS}/slab.cc
    ${SRC_DIRS}/slab_group.cc
    ${SRC_DIRS}/cache_bin.cc
    ${SRC_DIRS}/thread_cache.cc
    ${SRC_DIRS}/cascading_pool.cc
    ${SRC_DIRS}/thread_cache_manager.cc
)

# 设置库文件
add_library(${LIBRARY_NAME} ${SRC_FILES})

# 启用测试
add_subdirectory(${TEST_DIRS})
