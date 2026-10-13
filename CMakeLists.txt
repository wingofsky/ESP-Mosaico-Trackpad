cmake_minimum_required(VERSION 3.16)
set(ENV{IDF_COMPONENT_MANAGER} 0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.2.1")
project(mosaico_trackpad)

# Also protect callers that bypass idf.py and invoke Ninja/CMake directly.
add_custom_target(trackpad_block_generic_flash
    COMMAND ${CMAKE_COMMAND} -E echo "Generic flashing is disabled. Use tools/flash_app.py with a verified private backup."
    COMMAND ${CMAKE_COMMAND} -E false
    VERBATIM)
foreach(target flash app-flash bootloader-flash partition-table-flash otadata-flash
               erase-flash erase-otadata encrypted-flash encrypted-app-flash merge-bin)
    if(TARGET ${target})
        add_dependencies(${target} trackpad_block_generic_flash)
    endif()
endforeach()
