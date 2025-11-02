cmake_minimum_required(VERSION 4.1)
project(demo_to_ghost)

add_subdirectory(libs/ddnet_ghost)
add_subdirectory(libs/ddnet_demo)
add_executable(demo_to_ghost src/main.c)
target_link_libraries(demo_to_ghost PRIVATE ddnet_demo ddnet_ghost m)

