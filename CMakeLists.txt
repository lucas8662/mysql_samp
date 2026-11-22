cmake_minimum_required(VERSION 3.20)

# Must be chosen before project() so compiler checks and library searches
# already target 32-bit.
if(CMAKE_HOST_UNIX AND NOT APPLE AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/linux-x86.cmake)
endif()

project(samp_mysql_plugin LANGUAGES C CXX)

# Single build for every platform:
#   Linux:   bin/mysql.so (system OpenSSL) and bin/mysql_static.so (OpenSSL embedded)
#   Windows: mysql.dll
# Both servers are 32-bit; Linux uses cmake/linux-x86.cmake (-m32).
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --parallel
#   ctest --test-dir build --output-on-failure

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(UNIX AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Happens when -DCMAKE_C_FLAGS/-DCMAKE_CXX_FLAGS replace the toolchain's -m32.
    message(FATAL_ERROR "The plugin must be 32-bit like the SA-MP/open.mp server. "
        "Add -m32 to any CMAKE_C_FLAGS/CMAKE_CXX_FLAGS/linker flags you pass.")
endif()

if(UNIX)
    # Resolve the connector's OpenSSL to static archives; mysql.so swaps them
    # for the shared libraries below.
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif()

# --- MariaDB Connector/C -----------------------------------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WITH_SSL OPENSSL CACHE STRING "" FORCE)
set(WITH_UNIT_TESTS OFF CACHE BOOL "" FORCE)
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
# requires a destination even though this project only ships the plugin.
set(INSTALL_PLUGINDIR "lib/mariadb/plugin" CACHE PATH "" FORCE)

add_subdirectory(third_party/mariadb-connector-c EXCLUDE_FROM_ALL)

set(CONNECTOR_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mariadb-connector-c/include
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/mariadb-connector-c/include)

# --- Plugin --------------------------------------------------------------------
set(PLUGIN_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin CACHE PATH "Where the Linux plugins and test programs are written")
file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS src/*.cpp libs/sdk/*.cpp libs/sdk/amx/*.c)
add_library(plugin_objects OBJECT ${PLUGIN_SOURCES})
set_target_properties(plugin_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(plugin_objects PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libs
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/sdk/amx
    ${CONNECTOR_INCLUDE_DIRS})
target_compile_definitions(plugin_objects PUBLIC HAVE_STDINT_H _CRT_SECURE_NO_WARNINGS)
# The connector's generated headers must exist before the plugin compiles.
add_dependencies(plugin_objects mariadbclient)

if(WIN32)
    # The legacy AMX SDK predates MSVC's standard <stdint.h> and otherwise
    # redeclares int32_t/uint32_t with incompatible Windows types.
    # Windows min/max macros conflict with standard C++ functions.
    target_compile_definitions(plugin_objects PUBLIC WIN32 _WINDOWS _USRDLL NOMINMAX)

    add_library(mysql_plugin SHARED $<TARGET_OBJECTS:plugin_objects>)
    set_target_properties(mysql_plugin PROPERTIES OUTPUT_NAME mysql PREFIX "")
    target_link_libraries(mysql_plugin PRIVATE mariadbclient ws2_32)
    target_link_options(mysql_plugin PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/plugin.def")
else()
    target_compile_definitions(plugin_objects PUBLIC LINUX)
    target_compile_options(plugin_objects PRIVATE -Wall)

    # Connector dependencies with OpenSSL replaced by the shared libraries.
    get_target_property(CONNECTOR_LIBS mariadbclient INTERFACE_LINK_LIBRARIES)
    set(CONNECTOR_LIBS_SHARED_SSL)
    foreach(lib IN LISTS CONNECTOR_LIBS)
        if(NOT lib MATCHES "(ssl|crypto)\\.a$")
            list(APPEND CONNECTOR_LIBS_SHARED_SSL ${lib})
        endif()
    endforeach()


    add_library(mysql_plugin SHARED $<TARGET_OBJECTS:plugin_objects>)
    set_target_properties(mysql_plugin PROPERTIES OUTPUT_NAME mysql PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR})
    add_dependencies(mysql_plugin mariadbclient)
    target_link_libraries(mysql_plugin PRIVATE
        $<TARGET_FILE:mariadbclient> ${CONNECTOR_LIBS_SHARED_SSL} ssl crypto dl pthread rt)

    add_library(mysql_plugin_static SHARED $<TARGET_OBJECTS:plugin_objects>)
    set_target_properties(mysql_plugin_static PROPERTIES OUTPUT_NAME mysql_static PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR})
    target_link_libraries(mysql_plugin_static PRIVATE mariadbclient dl pthread rt)

    foreach(target mysql_plugin mysql_plugin_static)
        target_link_options(${target} PRIVATE -Wl,-z,defs)
    endforeach()
endif()

# --- Tests (Linux) -------------------------------------------------------------
if(UNIX)
    enable_testing()

    function(plugin_test name source)
        add_executable(${name} ${source})
        set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR})
        # Tests rely on assert() even in Release builds.
        target_compile_options(${name} PRIVATE -UNDEBUG)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    endfunction()

    # Parser/config regressions; no connector or database.
    plugin_test(input_regression tests/input_regression.cpp)
    target_include_directories(input_regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME input_regression COMMAND input_regression)

    # Loads the shipped binaries and checks the embedded connector.
    plugin_test(connector_smoke tests/connector_smoke.c)
    target_include_directories(connector_smoke PRIVATE ${CONNECTOR_INCLUDE_DIRS})
    target_link_libraries(connector_smoke PRIVATE dl)
    add_test(NAME connector_smoke COMMAND connector_smoke $<TARGET_FILE:mysql_plugin>)
    add_test(NAME connector_smoke_static COMMAND connector_smoke $<TARGET_FILE:mysql_plugin_static>)

    # White-box regressions against the production binary.
    foreach(name orm_regression core_regression)
        plugin_test(${name} tests/${name}.cpp)
        target_include_directories(${name} PRIVATE $<TARGET_PROPERTY:plugin_objects,INTERFACE_INCLUDE_DIRECTORIES>)
        target_compile_definitions(${name} PRIVATE LINUX HAVE_STDINT_H)
        target_link_libraries(${name} PRIVATE mysql_plugin_static)
        add_test(NAME ${name} COMMAND ${name})
    endforeach()

    # End-to-end against a live server; skipped unless MYSQL_TEST_HOST is set.
    plugin_test(mysql_integration tests/mysql_integration.cpp)
    target_include_directories(mysql_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs ${CMAKE_CURRENT_SOURCE_DIR}/libs/sdk/amx)
    target_compile_definitions(mysql_integration PRIVATE LINUX HAVE_STDINT_H)
    target_link_libraries(mysql_integration PRIVATE dl pthread)
    foreach(target mysql_plugin mysql_plugin_static)
        add_test(NAME integration_${target} COMMAND mysql_integration $<TARGET_FILE:${target}>
            WORKING_DIRECTORY ${PLUGIN_OUTPUT_DIR})
        set_tests_properties(integration_${target} PROPERTIES LABELS integration SKIP_RETURN_CODE 77)
    endforeach()
endif()
