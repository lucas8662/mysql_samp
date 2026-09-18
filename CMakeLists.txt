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
# The plugin embeds the authentication methods needed for MySQL 8. Do not
# build Connector/C's optional dynamic modules; on Windows, client_ed25519
# links extra CryptoAPI and Winsock dependencies that mysql.dll never uses.
set(CLIENT_PLUGIN_DIALOG OFF CACHE STRING "" FORCE)
set(CLIENT_PLUGIN_CLIENT_ED25519 OFF CACHE STRING "" FORCE)
set(CLIENT_PLUGIN_PARSEC OFF CACHE STRING "" FORCE)
set(CLIENT_PLUGIN_AUTH_GSSAPI_CLIENT OFF CACHE STRING "" FORCE)
set(CLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD OFF CACHE STRING "" FORCE)
set(CLIENT_PLUGIN_PVIO_SHMEM STATIC CACHE STRING "" FORCE)
# MariaDB Connector/C omits this default when built as a subproject.  Its
# dynamic authentication plugins still declare install rules, and CMake
# requires a destination even though this project only ships mysql.dll.
set(INSTALL_PLUGINDIR "lib/mariadb/plugin" CACHE PATH "" FORCE)

add_subdirectory(third_party/mariadb-connector-c)

# vcpkg exports BoostConfig.cmake.  Config mode avoids CMake 4's removed
# FindBoost compatibility module and works with the Windows static-md triplet.
find_package(Boost CONFIG REQUIRED COMPONENTS headers atomic chrono date_time system thread)

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
    Boost::headers Boost::atomic Boost::chrono Boost::date_time Boost::system Boost::thread)

if(WIN32)
    # The legacy AMX SDK predates MSVC's standard <stdint.h> and otherwise
    # redeclares int32_t/uint32_t with incompatible Windows types.
    target_compile_definitions(mysql_plugin PRIVATE WIN32 _WINDOWS _USRDLL HAVE_STDINT_H)
    target_link_libraries(mysql_plugin PRIVATE ws2_32)
    target_link_options(mysql_plugin PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/plugin.def")
else()
    target_compile_options(mysql_plugin PRIVATE -m32 -fPIC)
    target_link_options(mysql_plugin PRIVATE -m32 -Wl,-z,defs)
    target_link_libraries(mysql_plugin PRIVATE dl pthread rt)
endif()
