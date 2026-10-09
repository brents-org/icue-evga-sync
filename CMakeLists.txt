cmake_minimum_required(VERSION 3.24)
project(icue_evga_sync LANGUAGES CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "icue-evga-sync is Windows x64 / MSVC only (NvAPI and the iCUE SDK are x64 DLLs)")
endif()

get_property(MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(MULTI_CONFIG)
  # setup_payload.rc embeds the built exes by path; one config keeps that simple.
  message(FATAL_ERROR "use a single-config generator, e.g. -G Ninja -DCMAKE_BUILD_TYPE=Release")
endif()

set(ICUE_EVGA_SYNC_VERSION "0.0.0-dev" CACHE STRING "Version stamped into the binaries and the installer")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options(/W4 /permissive- /utf-8)
add_compile_definitions(UNICODE _UNICODE ICUE_EVGA_SYNC_VERSION="${ICUE_EVGA_SYNC_VERSION}")

# Corsair's iCUE SDK ships without a license grant, so it is fetched at
# configure time (pinned by hash) rather than committed.
set(ICUESDK_VERSION 4.0.84)
set(ICUESDK_SHA256 eb4414cf505145f3e507dc839ad587bf7ca684d64be3d9a834f416a136736d5d)
set(ICUESDK_ZIP ${CMAKE_BINARY_DIR}/_deps/iCUESDK_${ICUESDK_VERSION}.zip)
set(ICUESDK_DIR ${CMAKE_BINARY_DIR}/_deps/iCUESDK)
if(NOT EXISTS ${ICUESDK_DIR}/include/iCUESDK/iCUESDK.h)
  file(DOWNLOAD
    https://github.com/CorsairOfficial/cue-sdk/releases/download/v${ICUESDK_VERSION}/iCUESDK_${ICUESDK_VERSION}.zip
    ${ICUESDK_ZIP}
    EXPECTED_HASH SHA256=${ICUESDK_SHA256}
    TLS_VERIFY ON)
  file(ARCHIVE_EXTRACT INPUT ${ICUESDK_ZIP} DESTINATION ${CMAKE_BINARY_DIR}/_deps)
endif()

add_library(icue_sdk SHARED IMPORTED)
set_target_properties(icue_sdk PROPERTIES
  IMPORTED_IMPLIB ${ICUESDK_DIR}/lib/x64/iCUESDK.x64_2019.lib
  IMPORTED_LOCATION ${ICUESDK_DIR}/redist/x64/iCUESDK.x64_2019.dll
  INTERFACE_INCLUDE_DIRECTORIES ${ICUESDK_DIR}/include)

# Pure logic (packets, mapping): no SDK, no hardware, unit tested.
add_library(sync_core STATIC src/evga_v3.cpp src/mapping.cpp)
target_include_directories(sync_core PUBLIC src)

set(APP_SOURCES
  src/main.cpp src/card.cpp src/config.cpp src/icue_source.cpp
  src/install.cpp src/log.cpp src/nvapi_i2c.cpp src/sync_engine.cpp)
set(APP_LIBS sync_core icue_sdk secur32 shell32 user32 delayimp)
# Delay-loaded so --uninstall can run from a temp copy with no DLL beside it.
set(APP_LINK_OPTIONS /DELAYLOAD:iCUESDK.x64_2019.dll)

add_executable(icue-evga-sync WIN32 ${APP_SOURCES})
target_link_libraries(icue-evga-sync PRIVATE ${APP_LIBS})
target_link_options(icue-evga-sync PRIVATE ${APP_LINK_OPTIONS})

add_executable(icue-evga-syncctl ${APP_SOURCES})
target_compile_definitions(icue-evga-syncctl PRIVATE ICUE_EVGA_CONSOLE)
target_link_libraries(icue-evga-syncctl PRIVATE ${APP_LIBS})
target_link_options(icue-evga-syncctl PRIVATE ${APP_LINK_OPTIONS})

add_custom_command(TARGET icue-evga-sync POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:icue_sdk> $<TARGET_FILE_DIR:icue-evga-sync>)

# icue-evga-sync-setup.exe: one-file per-user installer carrying the three
# binaries as RCDATA (ids from src/setup_payload.h, order = kInstalledFiles).
set(SETUP_RC ${CMAKE_BINARY_DIR}/setup_payload.rc)
file(GENERATE OUTPUT ${SETUP_RC} CONTENT
"101 RCDATA \"$<TARGET_FILE:icue-evga-sync>\"
102 RCDATA \"$<TARGET_FILE:icue-evga-syncctl>\"
103 RCDATA \"$<TARGET_FILE:icue_sdk>\"
")
# OBJECT_DEPENDS takes no generator expressions; with one config the exes
# land in the binary dir, so name them there to re-embed after a relink.
set_source_files_properties(${SETUP_RC} PROPERTIES
  OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/icue-evga-sync.exe;${CMAKE_BINARY_DIR}/icue-evga-syncctl.exe")
add_executable(icue-evga-sync-setup WIN32 src/setup.cpp src/install.cpp src/log.cpp ${SETUP_RC})
target_include_directories(icue-evga-sync-setup PRIVATE src)
target_link_libraries(icue-evga-sync-setup PRIVATE secur32 shell32 user32)
add_dependencies(icue-evga-sync-setup icue-evga-sync icue-evga-syncctl)

enable_testing()
add_executable(sync_core_tests tests/sync_core_tests.cpp)
target_link_libraries(sync_core_tests PRIVATE sync_core)
add_test(NAME sync_core_tests COMMAND sync_core_tests)
