cmake_minimum_required(VERSION 2.6)
project(enet C)

set(ENET_STATIC "0" CACHE BOOL "Create a static library")
set(ENET_SHARED "0" CACHE BOOL "Create a shared library")
set(ENET_LZ4 "0" CACHE BOOL "Add support for an optional packet-level compression")

if (MSVC)
	add_definitions(-W3)
else()
	add_definitions(-Wno-error)
endif()

include_directories(${PROJECT_SOURCE_DIR})

if (ENET_LZ4)
    add_definitions(-DENET_LZ4)
    set(SOURCES lz4/lz4.c)
endif()

if (ENET_STATIC)
    add_library(enet_static STATIC enet.c ${SOURCES})

    if (WIN32)
        target_link_libraries(enet_static winmm ws2_32)
    endif()
endif()

if (ENET_SHARED)
    add_library(enet SHARED enet.c ${SOURCES})

    if (WIN32)
        target_link_libraries(enet winmm ws2_32)
    endif()
endif()
