cmake_minimum_required(VERSION 3.25)
project(kush C)

set(CMAKE_C_STANDARD 23)

set(CMAKE_INSTALL_PREFIX /usr/local)

add_executable(kush kush.c)

install(TARGETS kush
        CONFIGURATIONS Release)
