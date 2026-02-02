cmake_minimum_required(VERSION 3.10)
project(faustpkg)

add_executable(faustpkg faustpkg.cpp)

set_target_properties(faustpkg PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
)
