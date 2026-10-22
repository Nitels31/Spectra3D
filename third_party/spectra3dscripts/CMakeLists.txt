cmake_minimum_required(VERSION 3.16)
project(spscript_example CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the SDK: the headers and the static library
set(SPS_SDK "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(host_example
    example/host_example.cpp
)

target_include_directories(host_example PRIVATE ${SPS_SDK}/include)
target_link_libraries(host_example PRIVATE ${SPS_SDK}/lib/libsps.a)

# the script and the data are next to the executable
add_custom_command(TARGET host_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPS_SDK}/example/game.sps $<TARGET_FILE_DIR:host_example>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPS_SDK}/assets/level_01.spl $<TARGET_FILE_DIR:host_example>
)
