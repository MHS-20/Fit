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
    main.cpp
    Commands.cpp
    Repository.cpp
    Objects.cpp
    Refs.cpp
    Index.cpp
    Ignore.cpp
    Config.cpp
    Remote.cpp
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
