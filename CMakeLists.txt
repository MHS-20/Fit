cmake_minimum_required(VERSION 3.20)
project(fit CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CLI11 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)

find_path(INIH_INCLUDE_DIR NAMES ini.h)
find_library(INIH_LIBRARY NAMES inih)

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

target_link_libraries(fit
    PRIVATE
    CLI11::CLI11
    OpenSSL::Crypto
    ZLIB::ZLIB
    CURL::libcurl
    ${INIH_LIBRARY}
)
