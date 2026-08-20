cmake_minimum_required(VERSION 3.22.1)
project("shaper")

# Include both the TUN shaper and the WAN saturator
add_library(shaper SHARED shaper_engine.cpp wan_saturator.cpp)
find_library(log-lib log)
target_link_libraries(shaper ${log-lib})
