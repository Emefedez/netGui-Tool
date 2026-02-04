cmake_minimum_required(VERSION 3.10)

project(tcp_stack_cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-Wall -g)

include_directories(include)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(netGui ${SOURCES})

# Buscar raylib instalado en el sistema
find_package(raylib REQUIRED)

target_link_libraries(netGui PRIVATE raylib)

target_link_libraries(netGui PRIVATE 
    pthread
    dl
    m
)