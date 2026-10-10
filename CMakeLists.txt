cmake_minimum_required(VERSION 3.20)
project(fit CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(CLI11 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)

find_path(INIH_INCLUDE_DIR NAMES ini.h)
find_library(INIH_LIBRARY NAMES inih)

# Build Targets: everything but the CLI entry point is a library, so tests
# can link it without CLI11.
add_library(fit_lib STATIC
    src/Commands.cpp
    src/Repository.cpp
    src/Objects.cpp
    src/Refs.cpp
    src/Index.cpp
    src/Ignore.cpp
    src/Config.cpp
    src/Remote.cpp
)
target_include_directories(fit_lib PUBLIC ${INIH_INCLUDE_DIR})
target_link_libraries(fit_lib PUBLIC OpenSSL::Crypto ZLIB::ZLIB CURL::libcurl ${INIH_LIBRARY})

add_executable(fit src/main.cpp)
target_link_libraries(fit PRIVATE fit_lib CLI11::CLI11)

# Tests: one CTest entry per TEST(name), run as `fit_tests <name>`. Most
# cross-check against the real `git` binary, which must be on PATH.
file(GLOB FIT_TEST_SOURCES tests/*_tests.cpp)
add_executable(fit_tests tests/main.cpp ${FIT_TEST_SOURCES})
target_link_libraries(fit_tests PRIVATE fit_lib)
target_compile_definitions(fit_tests PRIVATE
    FIT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

enable_testing()
foreach(test_source ${FIT_TEST_SOURCES})
    file(STRINGS ${test_source} test_lines REGEX "^TEST\\(")
    foreach(test_line ${test_lines})
        string(REGEX REPLACE "^TEST\\(([a-z0-9_]+)\\).*" "\\1" test_name "${test_line}")
        add_test(NAME ${test_name} COMMAND fit_tests ${test_name})
        set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 20)
    endforeach()
endforeach()

# 1. Installation Feature
include(GNUInstallDirs)
install(TARGETS fit DESTINATION ${CMAKE_INSTALL_BINDIR})

# 2. Code Formatting Automation
find_program(CLANG_FORMAT_BIN clang-format)
if(CLANG_FORMAT_BIN)
    file(GLOB_RECURSE ALL_SOURCE_FILES src/*.cpp src/*.hpp)
    add_custom_target(fmt
        COMMAND ${CLANG_FORMAT_BIN} -i ${ALL_SOURCE_FILES}
        COMMENT "Formatting codebase with clang-format..."
    )
endif()

# 3. Static Analysis Linter
find_program(CLANG_TIDY_BIN clang-tidy)
if(CLANG_TIDY_BIN)
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_BIN};-extra-arg=-std=c++20)
endif()
