cmake_minimum_required(VERSION 3.20)
project(samp_mysql_plugin LANGUAGES C CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(UNIX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WITH_SSL OPENSSL CACHE STRING "" FORCE)
set(CLIENT_PLUGIN_CACHING_SHA2_PASSWORD STATIC CACHE STRING "" FORCE)
set(CLIENT_PLUGIN_SHA256_PASSWORD STATIC CACHE STRING "" FORCE)

add_subdirectory(third_party/mariadb-connector-c)

# vcpkg exports BoostConfig.cmake.  Config mode avoids CMake 4's removed
# FindBoost compatibility module and works with the Windows static-md triplet.
find_package(Boost CONFIG REQUIRED COMPONENTS atomic chrono date_time system thread)

file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS src/*.cpp libs/sdk/*.cpp libs/sdk/amx/*.c)
add_library(mysql_plugin SHARED ${PLUGIN_SOURCES})
set_target_properties(mysql_plugin PROPERTIES OUTPUT_NAME mysql PREFIX "")

target_include_directories(mysql_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/sdk/amx
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mariadb-connector-c/include
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/mariadb-connector-c/include)
target_compile_definitions(mysql_plugin PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(mysql_plugin PRIVATE
    mariadbclient
    Boost::atomic Boost::chrono Boost::date_time Boost::system Boost::thread)

if(WIN32)
    target_compile_definitions(mysql_plugin PRIVATE WIN32 _WINDOWS _USRDLL)
    target_link_libraries(mysql_plugin PRIVATE ws2_32)
    target_link_options(mysql_plugin PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/plugin.def")
else()
    target_compile_options(mysql_plugin PRIVATE -m32 -fPIC)
    target_link_options(mysql_plugin PRIVATE -m32 -Wl,-z,defs)
    target_link_libraries(mysql_plugin PRIVATE dl pthread rt)
endif()
