cmake_minimum_required(VERSION 3.27)
project(untitled)

set(CMAKE_CXX_STANDARD 23)

find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE Boost::boost Threads::Threads OpenSSL::Crypto OpenSSL::SSL)

if (WIN32)
    target_link_libraries(main PRIVATE ws2_32)
endif ()