# Out-of-tree clang-tidy module carrying the F Prime checklist checks (fprime-*).
#
#   cmake -S native -B native/build -DClang_DIR=/usr/lib/llvm-15/lib/cmake/clang
#   cmake --build native/build
#   clang-tidy --load=native/build/libFprimeTidy.so --checks='-*,fprime-*' ...
#
# The plugin must be loaded by the clang-tidy of the same LLVM major version it
# was built against (no stable plugin ABI).
cmake_minimum_required(VERSION 3.16)
project(FprimeTidy C CXX)

find_package(Clang REQUIRED CONFIG)
message(STATUS "FprimeTidy: building against LLVM ${LLVM_PACKAGE_VERSION} (${LLVM_INSTALL_PREFIX})")

file(GLOB FPRIME_TIDY_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/checks/*.cpp)
add_library(FprimeTidy MODULE FprimeTidyModule.cpp ${FPRIME_TIDY_SOURCES})
target_include_directories(FprimeTidy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
target_compile_features(FprimeTidy PRIVATE cxx_std_17)
target_compile_options(FprimeTidy PRIVATE -fno-rtti -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(FprimeTidy PRIVATE ${LLVM_DEFINITIONS})
set_target_properties(FprimeTidy PROPERTIES PREFIX "lib" OUTPUT_NAME "FprimeTidy")

# A file the Python side can read to learn which clang-tidy binary matches the plugin.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/clang-tidy-path.txt "${LLVM_TOOLS_BINARY_DIR}/clang-tidy\n")
