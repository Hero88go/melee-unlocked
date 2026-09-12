# Standalone: built on demand, not part of the port build.
cmake_minimum_required(VERSION 3.15)
project(latency_probe LANGUAGES CXX)
add_executable(latency_probe latency_probe.cpp)
target_compile_features(latency_probe PRIVATE cxx_std_17)
