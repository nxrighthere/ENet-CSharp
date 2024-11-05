cmake_minimum_required(VERSION 2.9)
project(enet C)

set(ENET_DEBUG "0" CACHE BOOL "Enable debug functionality")

if (WIN32)
    set(ENET_EXCLUDE_WINDOWS_H "0" CACHE BOOL "Exclude \"windows.h\" header")
endif()

set(ENET_STATIC "0" CACHE BOOL "Create a static library")
set(ENET_SHARED "0" CACHE BOOL "Create a shared library")

if (MSYS OR MINGW)
    set(CMAKE_C_FLAGS "-static") 

    add_definitions(-DWINVER=0x0601)
    add_definitions(-D_WIN32_WINNT=0x0601)
endif()

if (ENET_DEBUG)
    add_definitions(-DENET_DEBUG)
endif()

if (ENET_STATIC)
    add_library(enet_static STATIC enet.c ${SOURCES})

    if (NOT UNIX)
        target_link_libraries(enet_static winmm ws2_32)
        SET_TARGET_PROPERTIES(enet_static PROPERTIES PREFIX "")
    endif()
endif()

if (ENET_SHARED)
    if (APPLE)
        SET(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()
    add_definitions(-DENET_DLL)
    add_library(enet SHARED enet.c ${SOURCES})

    if (NOT UNIX)
        target_link_libraries(enet winmm ws2_32)
        SET_TARGET_PROPERTIES(enet PROPERTIES PREFIX "")
    endif()
endif()
