cmake_minimum_required(VERSION 3.20)

project(cpp_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)

add_executable(cpp_backend src/main.cpp)

target_compile_options(cpp_backend PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(cpp_backend PRIVATE Drogon::Drogon)