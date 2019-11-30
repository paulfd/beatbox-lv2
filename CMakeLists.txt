cmake_minimum_required(VERSION 3.11)
project(beatbox-lv2)

# Force add relocation to the flags globally. It's ugly but what can you do...
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

# Export the compile_commands.json file
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(beatbox-lv2 SHARED beatbox.c)
set_target_properties(beatbox-lv2 PROPERTIES PREFIX "")
set_target_properties(beatbox-lv2 PROPERTIES OUTPUT_NAME "beatbox")

if(UNIX)
target_compile_options(beatbox-lv2 PRIVATE -Wextra -pedantic -Wall -Werror)
endif()

file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/beatbox.lv2)
add_custom_command(TARGET beatbox-lv2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
        ${CMAKE_CURRENT_SOURCE_DIR}/beatbox.ttl 
        ${CMAKE_CURRENT_BINARY_DIR}/beatbox.lv2/beatbox.ttl)

add_custom_command(TARGET beatbox-lv2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
        ${CMAKE_CURRENT_BINARY_DIR}/beatbox.so 
        ${CMAKE_CURRENT_BINARY_DIR}/beatbox.lv2/beatbox.so)

add_custom_command(TARGET beatbox-lv2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
        ${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl 
        ${CMAKE_CURRENT_BINARY_DIR}/beatbox.lv2/manifest.ttl)