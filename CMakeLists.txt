cmake_minimum_required(VERSION 3.22)
project(learn_epoll CXX C)

set(CMAKE_CXX_STANDARD 14)

#include_directories(include)
#
#aux_source_directory(./src DIR_SRCS)


IF (WIN32)
    add_definitions(-D__win__)
    aux_source_directory(./wepoll DIR_SRCS)
ENDIF()

add_executable(learn_epoll ${DIR_SRCS} main.cpp)

IF (WIN32)
    target_link_libraries(${PROJECT_NAME} ws2_32)
ENDIF()