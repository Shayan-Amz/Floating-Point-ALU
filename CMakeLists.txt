cmake_minimum_required(VERSION 3.14)
project(fp32 VERSION 2.0.0 LANGUAGES CXX
        DESCRIPTION "Bit-exact software model of an IEEE 754 binary32 arithmetic unit")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FP32_BUILD_TESTS "Build the conformance test suite" ON)
option(FP32_BUILD_TOOLS "Build the legacy-accuracy measurement tool" ON)

# ---- header-only library ---------------------------------------------------
add_library(fp32 INTERFACE)
add_library(fp32::fp32 ALIAS fp32)
target_include_directories(fp32 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fp32 INTERFACE cxx_std_17)

set(FP32_WARNINGS
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->)

# ---- command-line calculator -----------------------------------------------
add_executable(fp32calc src/main.cpp)
target_link_libraries(fp32calc PRIVATE fp32)
target_compile_options(fp32calc PRIVATE ${FP32_WARNINGS})

# ---- tests -----------------------------------------------------------------
if(FP32_BUILD_TESTS)
  enable_testing()
  add_executable(test_fp32 tests/test_fp32.cpp)
  target_link_libraries(test_fp32 PRIVATE fp32)
  target_compile_options(test_fp32 PRIVATE ${FP32_WARNINGS}
      # The reference values come from the host FPU under fesetround(); these flags
      # stop the compiler from constant-folding or reordering those operations.
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-frounding-math>
      $<$<CXX_COMPILER_ID:MSVC>:/fp:strict>)
  add_test(NAME conformance COMMAND test_fp32 250000)
  add_test(NAME conformance_quick COMMAND test_fp32 20000)
endif()

# ---- tools -----------------------------------------------------------------
if(FP32_BUILD_TOOLS)
  add_executable(legacy_accuracy tools/legacy_accuracy.cpp)
  target_link_libraries(legacy_accuracy PRIVATE fp32)
  target_include_directories(legacy_accuracy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  # the legacy source is kept verbatim (warnings and all) for comparison purposes
  target_compile_options(legacy_accuracy PRIVATE -w)
endif()

install(DIRECTORY include/ DESTINATION include)
install(TARGETS fp32calc RUNTIME DESTINATION bin)
