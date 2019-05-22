cmake_minimum_required(VERSION 3.05)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER clang)
project(communication_sim)

add_compile_options(-std=c++2a -fcoroutines-ts -stdlib=libc++ -Wall -Wextra)
set(CMAKE_EXE_LINKER_FLAGS "-std=c++2a -stdlib=libc++")


add_executable(csma_cd_sim csma_cd.cpp)
add_executable(new_method new_method.cpp)
