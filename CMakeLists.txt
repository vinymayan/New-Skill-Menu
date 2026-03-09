# ---------------------------------------------------- #
#                 Required envs assert                 #
# ---------------------------------------------------- #

if(NOT DEFINED ENV{COMMONLIB_SSE_FOLDER})
  message(FATAL_ERROR "Missing COMMONLIB_SSE_FOLDER environment variable")
endif()

# ---------------------------------------------------- #
#               Imports and definitions                #
# ---------------------------------------------------- #

include(cmake/lib/copyOutputs.cmake)
include(cmake/lib/automaticGameFolderOutput.cmake)
include(cmake/lib/autoCollectSources.cmake)


add_src_folder(src)
add_include_folder(include)

add_src_folder(vendor/src)
add_include_folder(vendor/include)


cmake_minimum_required(VERSION 3.21)
set(AUTHOR_NAME "AuthorName")
set(PRODUCT_NAME "PrismaUISKSETemplate")
set(BEAUTIFUL_NAME "PrismaUI SKSE Template")

project(${PRODUCT_NAME} VERSION 3.0.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# ---------------------------------------------------- #
#                      Resources                       #
# ---------------------------------------------------- #

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY)

set(RESOURCE_FILE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
set(RESOURCE_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.res)

add_custom_command(
    OUTPUT ${RESOURCE_OUTPUT}
    COMMAND rc /fo ${RESOURCE_OUTPUT} ${RESOURCE_FILE}
    DEPENDS ${RESOURCE_FILE}
)

add_custom_target(Resource ALL DEPENDS ${RESOURCE_OUTPUT})

# ---------------------------------------------------- #
#                Plugin Configuration                  #
# ---------------------------------------------------- #

include(FetchContent)

set(ENABLE_SKYRIM_SE ON CACHE BOOL " " FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL " " FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL " " FORCE)
set(BUILD_TESTS OFF CACHE BOOL " " FORCE)
set(DEV_SERVER ON CACHE BOOL " " FORCE)


add_subdirectory("$ENV{COMMONLIB_SSE_FOLDER}" ${CMAKE_CURRENT_BINARY_DIR}/_deps/clib-build)
get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)

find_path(CLIB_UTIL_INCLUDE_DIRS "ClibUtil/utils.hpp")
file(
  GLOB_RECURSE
  sources
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc
  ${sources}
    imgui/backends/imgui_impl_dx11.cpp
    imgui/backends/imgui_impl_win32.cpp
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
)

source_group(
  TREE ${CMAKE_CURRENT_SOURCE_DIR}
  FILES ${headers} ${sources}
)

set(BUILD_TESTS OFF)
include(${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake)

add_commonlibsse_plugin(
  ${PROJECT_NAME}
  AUTHOR AUTHOR_NAME
  SOURCES ${headers} ${sources}
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23) 
target_precompile_headers(${PROJECT_NAME} PRIVATE include/PCH.h)

target_compile_definitions(${PROJECT_NAME}  PRIVATE BEAUTIFUL_NAME="${BEAUTIFUL_NAME}" PRODUCT_NAME="${PRODUCT_NAME}" IMGUI_DEFINE_MATH_OPERATORS)


set(header_dirs "")

foreach(header ${headers})
    get_filename_component(dir ${header} DIRECTORY)
    list(APPEND header_dirs ${dir})
endforeach()

list(REMOVE_DUPLICATES header_dirs)


find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
    ${header_dirs}
    ${CLIB_UTIL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(${PROJECT_NAME} PRIVATE ${DETOURS_LIBRARY})
target_compile_definitions(${PRODUCT_NAME} PRIVATE MOD_NAME="${BEAUTIFUL_NAME}")


# ---------------------------------------------------- #
#                        Npm                           #
# ---------------------------------------------------- #

if(NOT ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(DEV_SERVER OFF)
endif()

if(DEV_SERVER)

add_compile_definitions(DEV_SERVER)
set(PRISMA_UI_DIR ${CMAKE_BINARY_DIR}/PrismaUI)
add_custom_command(
    OUTPUT ${PRISMA_UI_DIR}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${PRISMA_UI_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PRISMA_UI_DIR}
)
add_custom_target(dev_prisma_ui ALL DEPENDS ${PRISMA_UI_DIR})

else()


set(UI_DIR ${CMAKE_SOURCE_DIR}/ui)
set(UI_BUILD_DIR ${UI_DIR}/dist)
set(UI_OUTPUT_DIR ${CMAKE_BINARY_DIR}/PrismaUI/views/${PRODUCT_NAME})

file(GLOB_RECURSE UI_SOURCES
    ${UI_DIR}/*
)

list(FILTER UI_SOURCES EXCLUDE REGEX ".*/dist/.*")
list(FILTER UI_SOURCES EXCLUDE REGEX ".*/node_modules/.*")

add_custom_command(
    OUTPUT ${UI_BUILD_DIR}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${UI_OUTPUT_DIR}
    COMMAND npm install
    COMMAND npm run build
    WORKING_DIRECTORY ${UI_DIR}
    DEPENDS ${UI_SOURCES}
)

add_custom_command(
    OUTPUT ${UI_OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${UI_OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${UI_BUILD_DIR} ${UI_OUTPUT_DIR}
    DEPENDS ${UI_BUILD_DIR}
)

endif()

add_custom_target(build_ui ALL
    DEPENDS ${UI_OUTPUT_DIR}
)

add_dependencies(${PROJECT_NAME} build_ui)

# ---------------------------------------------------- #
#                        Output                        #
# ---------------------------------------------------- #

set(wildlander_output false)
set(steam_owrt_output false)
set(steam_mods_output true)
automaticGameFolderOutput(wildlander_output, steam_owrt_output, steam_mods_output)
