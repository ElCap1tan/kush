cmake_minimum_required(VERSION 3.21)
project(kush C)

set(CMAKE_C_STANDARD 23)

add_executable(kush kush.c)

install(TARGETS kush)
