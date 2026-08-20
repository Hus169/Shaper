# ==============================================================================
# File: app/src/main/cpp/CMakeLists.txt
# ==============================================================================
cmake_minimum_required(VERSION 3.22.1)
project("shaper")

# Add the native C++ source file provided earlier
add_library(shaper SHARED shaper_engine.cpp)

# Link against Android logging and standard libraries
find_library(log-lib log)
target_link_libraries(shaper ${log-lib})
