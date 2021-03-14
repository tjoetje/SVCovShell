project(shell)
cmake_minimum_required(VERSION 3.0)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

SET(SRC_LIST shell.cpp)
add_library (${PROJECT_NAME}lib ${SRC_LIST})

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} ${PROJECT_NAME}lib)

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    target_link_libraries(${PROJECT_NAME}test pthread)
ENDIF()

