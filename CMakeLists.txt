cmake_minimum_required(VERSION 3.22)

project(cranked)

set(CMAKE_CXX_STANDARD 20)

if (MSVC)
    add_compile_options(/Zc:__cplusplus /utf-8 /bigobj)
endif ()

include(FindPkgConfig)

add_subdirectory(core)

if (NOT SKIP_TESTING)
    add_subdirectory(testing)
endif ()

if (NOT SKIP_JAVA)
    add_subdirectory(java)
endif ()

if (NOT SKIP_DESKTOP)
    add_subdirectory(desktop)
endif ()

if (NOT SKIP_LIBRETRO)
    add_subdirectory(libretro)
endif ()
