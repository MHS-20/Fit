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

# Build Target
add_executable(fit
    src/main.cpp
    src/Commands.cpp
    src/Repository.cpp
    src/Objects.cpp
    src/Refs.cpp
    src/Index.cpp
    src/Ignore.cpp
    src/Config.cpp
    src/Remote.cpp
)

target_include_directories(fit PRIVATE ${INIH_INCLUDE_DIR})
target_link_libraries(fit PRIVATE CLI11::CLI11 OpenSSL::Crypto ZLIB::ZLIB CURL::libcurl ${INIH_LIBRARY})

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
