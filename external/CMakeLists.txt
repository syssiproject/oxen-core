# Copyright (c) 2014-2018, The Monero Project
# 
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without modification, are
# permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice, this list of
#    conditions and the following disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright notice, this list
#    of conditions and the following disclaimer in the documentation and/or other
#    materials provided with the distribution.
# 
# 3. Neither the name of the copyright holder nor the names of its contributors may be
#    used to endorse or promote products derived from this software without specific
#    prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
# THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# 
# Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers


# We always compile if we are building statically to reduce static dependency issues...
# ...except for FreeBSD, because FreeBSD is a special case that doesn't play well with
# others.

if(NOT STATIC AND NOT BUILD_STATIC_DEPS)
  find_package(PkgConfig REQUIRED)
endif()

include(system_or_submodule)

system_or_submodule(OXENC oxenc liboxenc>=1.1.0 oxen-encoding)
system_or_submodule(OXENMQ oxenmq liboxenmq>=1.2.13 oxen-mq)

add_subdirectory(db_drivers)
add_subdirectory(randomx EXCLUDE_FROM_ALL)
oxen_install_library(randomx)
add_subdirectory(date EXCLUDE_FROM_ALL)

set(BLS_ETH ON CACHE BOOL "" FORCE)
set(MCL_STATIC_LIB ON CACHE BOOL "" FORCE)
add_subdirectory(bls EXCLUDE_FROM_ALL)
oxen_install_library(bls256)

if(BUILD_STATIC_DEPS)
  set(OXEN_LOGGING_FORCE_SUBMODULES TRUE CACHE BOOL "" FORCE)
endif()
add_subdirectory(oxen-logging)
oxen_logging_add_source_dir("${PROJECT_SOURCE_DIR}")
oxen_logging_add_source_dir("${PROJECT_SOURCE_DIR}/src")

install(
    DIRECTORY boost
    DESTINATION include${OXEN_INSTALL_INCLUDEDIR_SUFFIX}
    COMPONENT development
    FILES_MATCHING PATTERN "*.hpp")

# uSockets doesn't really have a proper build system (just a very simple Makefile) so build it
# ourselves.
if (NOT CMAKE_VERSION VERSION_LESS 3.12)
    set(conf_depends "CONFIGURE_DEPENDS")
else()
    set(conf_depends "")
endif()
file(GLOB usockets_src ${conf_depends} uWebSockets/uSockets/src/*.c uWebSockets/uSockets/src/eventing/*.c)
file(COPY uWebSockets/uSockets/src/libusockets.h DESTINATION uWebSockets)
add_library(uSockets STATIC EXCLUDE_FROM_ALL ${usockets_src})
target_compile_definitions(uSockets PRIVATE LIBUS_NO_SSL=1)
target_include_directories(uSockets PRIVATE uWebSockets/uSockets/src)

# On Windows uSockets uses libuv for its event loop; on Mac kqueue is the default, but that seems to
# not be reliable on older macos versions (like 10.12), so we use libuv on macos as well.
if (WIN32 OR (APPLE AND NOT IOS))
  add_subdirectory(libuv EXCLUDE_FROM_ALL)
  target_link_libraries(uSockets uv_a)
  target_compile_definitions(uSockets PUBLIC LIBUS_USE_LIBUV)
endif()


# The uWebSockets C++ layer is header-only but isn't actually prefixed in the repository itself, but
# rather only on install (which, as above, is just a very simple Makefile).  This is unfortunate
# because it means that we can't use `#include <uWebSockets/App.h>` directly with the repo; so
# instead we emulate the installation process into the build directory and include it (with the
# prefix) from there.
file(COPY uWebSockets/src/ DESTINATION uWebSockets/uWebSockets FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
add_library(uWebSockets INTERFACE)
target_include_directories(uWebSockets INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/uWebSockets)
target_link_libraries(uWebSockets INTERFACE uSockets)
target_compile_definitions(uWebSockets INTERFACE UWS_HTTPRESPONSE_NO_WRITEMARK UWS_NO_ZLIB)


# Hack around SQLiteCpp's attempts to locate sqlite3 because we *don't* want to link against the
# system one, but don't download and build the embedded one until build time.  Thankfully it
# actually links against the SQLite::SQLite3 cmake target if it already exists, so all we have to do
# is set that up and circumvent some of the non-target bits of its FindSQLite3.cmake.
set(SQLite3_FOUND TRUE CACHE BOOL "" FORCE)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ignored")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ignored/sqlite3.h" "#define SQLITE_VERSION \"${SQLite3_VERSION}\"")
set(SQLite3_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/ignored" CACHE STRING "" FORCE)
set(SQLite3_LIBRARY "ignored" CACHE STRING "" FORCE)
set(SQLITECPP_INTERNAL_SQLITE OFF CACHE BOOL "don't build SQLiteCpp's internal sqlite3" FORCE)
set(SQLITE_ENABLE_COLUMN_METADATA OFF CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_CPPCHECK OFF CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_DOXYGEN OFF CACHE BOOL "" FORCE)
set(SQLITECPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SQLITECPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(SQLiteCpp EXCLUDE_FROM_ALL)
oxen_install_library(SQLiteCpp)


add_subdirectory(Catch2)

if(BUILD_PYBIND)
  add_subdirectory(pybind11 EXCLUDE_FROM_ALL)
endif()


set(ethyl_ENABLE_CRYPTO_LIBRARY FALSE CACHE BOOL "" FORCE)
add_subdirectory(ethyl EXCLUDE_FROM_ALL)
oxen_install_library(ethyl)
oxen_install_library(cpr)  # comes via ethyl

if(NOT TARGET nlohmann_json::nlohmann_json)
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_MultipleHeaders ON CACHE BOOL "") # Allows multi-header nlohmann use
    system_or_submodule(NLOHMANN nlohmann_json nlohmann_json>=3.7.0 nlohmann-json)
endif()


# NOTE: By default cpptrace is configured to make stack-traces using libdwarf.
# On this codebase libdward segfaults trying to handle our stack-traces so we
# switch to a more primitive method, addr2line which does work.
set(CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE ON CACHE BOOL "")
add_subdirectory(cpptrace EXCLUDE_FROM_ALL)
