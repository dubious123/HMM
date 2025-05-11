cmake_minimum_required(VERSION 3.31)
project(HMM_JH)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(HMM_JH main.cpp
        common.h)
