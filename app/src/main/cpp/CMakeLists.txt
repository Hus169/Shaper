cmake_minimum_required(VERSION 3.22.1)
project("shaper")

# Compile the WAN saturator
add_library(shaper SHARED wan_saturator.cpp)
find_library(log-lib log)
target_link_libraries(shaper ${log-lib})
