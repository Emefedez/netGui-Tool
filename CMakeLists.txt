cmake_minimum_required(VERSION 3.10)

project(tcp_stack_cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-Wall -g)

include_directories(include)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(netGui ${SOURCES})

find_package(Curses REQUIRED)

target_include_directories(netGui PRIVATE ${CURSES_INCLUDE_DIRS})

target_link_libraries(netGui PRIVATE 
    ${CURSES_LIBRARIES}
    pthread
    dl
    m
)