cmake_minimum_required(VERSION 3.20)
project(AviationComms LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AVIATION_COMMS_ENABLE_COVERAGE "Enable source-based coverage instrumentation for project targets." OFF)
option(AVIATION_COMMS_ENABLE_STRICT_WARNINGS "Enable strict compiler warnings for project-owned sources." OFF)
option(AVIATION_COMMS_ENABLE_SANITIZERS "Enable runtime sanitizers for project targets." OFF)
set(AVIATION_COMMS_SANITIZER_SET "address,undefined" CACHE STRING
  "Comma-separated sanitizer list when AVIATION_COMMS_ENABLE_SANITIZERS is ON.")

function(aviation_split_csv_to_list out_var csv_value)
  string(REPLACE "," ";" parsed_list "${csv_value}")
  set(${out_var} "${parsed_list}" PARENT_SCOPE)
endfunction()

function(aviation_apply_linker_instrumentation target)
  if(AVIATION_COMMS_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
      target_link_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(${target} PRIVATE --coverage)
      target_link_options(${target} PRIVATE --coverage)
    endif()
  endif()

  if(AVIATION_COMMS_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      aviation_split_csv_to_list(aviation_sanitizer_list "${AVIATION_COMMS_SANITIZER_SET}")
      foreach(sanitizer IN LISTS aviation_sanitizer_list)
        string(STRIP "${sanitizer}" sanitizer)
        if(NOT sanitizer STREQUAL "")
          target_compile_options(${target} PRIVATE -fsanitize=${sanitizer} -fno-omit-frame-pointer)
          target_link_options(${target} PRIVATE -fsanitize=${sanitizer} -fno-omit-frame-pointer)
        endif()
      endforeach()
    endif()
  endif()
endfunction()

function(aviation_apply_source_warnings)
  if(NOT AVIATION_COMMS_ENABLE_STRICT_WARNINGS)
    return()
  endif()

  set(options)
  set(one_value_args)
  set(multi_value_args SOURCES)
  cmake_parse_arguments(AVIATION_WARN "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT AVIATION_WARN_SOURCES)
    return()
  endif()

  set(project_warning_flags "")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    list(APPEND project_warning_flags
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
      -Wshadow
      -Wdouble-promotion
      -Wformat=2
      -Wnull-dereference
      -Wimplicit-fallthrough
    )
  endif()

  foreach(source IN LISTS AVIATION_WARN_SOURCES)
    set_source_files_properties(${source} PROPERTIES COMPILE_OPTIONS "${project_warning_flags}")
  endforeach()
endfunction()

include(FetchContent)

FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

find_package(OpenGL REQUIRED)

set(IMGUI_SOURCES
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/imgui_impl_glfw.cpp
  third_party/imgui/imgui_impl_opengl3.cpp
)

add_library(common STATIC
  common/logger.cpp
)
target_include_directories(common
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)
aviation_apply_linker_instrumentation(common)
aviation_apply_source_warnings(SOURCES common/logger.cpp)

add_executable(aircraft_client
  client/aircraft_client.cpp
  client/client_app.cpp
  client/client_options.cpp
  client/main.cpp
  client/client_utils.cpp
  client/flight_sim.cpp
)
target_link_libraries(aircraft_client PRIVATE common)
aviation_apply_linker_instrumentation(aircraft_client)
aviation_apply_source_warnings(SOURCES
  client/aircraft_client.cpp
  client/client_app.cpp
  client/client_options.cpp
  client/main.cpp
  client/client_utils.cpp
  client/flight_sim.cpp
)

add_executable(ground_server
  server/client_session.cpp
  server/dashboard_state.cpp
  server/ground_server.cpp
  server/main.cpp
  server/server_options.cpp
  server/server_dashboard_sync.cpp
  server/server_session_logic.cpp
  server/server_utils.cpp
  server/state_machine.cpp
  server/weather_map.cpp
  server/imgui_dashboard.cpp
  ${IMGUI_SOURCES}
)
target_include_directories(ground_server
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/server
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
)
target_link_libraries(ground_server PRIVATE common glfw OpenGL::GL)
aviation_apply_linker_instrumentation(ground_server)
aviation_apply_source_warnings(SOURCES
  server/client_session.cpp
  server/dashboard_state.cpp
  server/ground_server.cpp
  server/main.cpp
  server/server_options.cpp
  server/server_dashboard_sync.cpp
  server/server_session_logic.cpp
  server/server_utils.cpp
  server/state_machine.cpp
  server/weather_map.cpp
  server/imgui_dashboard.cpp
)
if(AVIATION_COMMS_ENABLE_STRICT_WARNINGS)
  foreach(imgui_source IN LISTS IMGUI_SOURCES)
    set_source_files_properties(${imgui_source} PROPERTIES COMPILE_OPTIONS "-w")
  endforeach()
endif()

if(WIN32)
  target_compile_definitions(common INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(aircraft_client PRIVATE ws2_32)
  target_link_libraries(ground_server PRIVATE ws2_32)
endif()

if(APPLE)
  target_compile_definitions(ground_server PRIVATE GL_SILENCE_DEPRECATION)
endif()

include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(unit_tests
  tests/dashboard_state_tests.cpp
  tests/checksum_tests.cpp
  tests/client_options_tests.cpp
  tests/client_session_tests.cpp
  tests/client_utils_tests.cpp
  tests/imgui_dashboard_tests.cpp
  tests/large_file_payload_tests.cpp
  tests/logger_tests.cpp
  tests/packet_header_tests.cpp
  tests/server_options_tests.cpp
  tests/server_dashboard_sync_tests.cpp
  tests/server_session_logic_tests.cpp
  tests/server_utils_tests.cpp
  tests/socket_utils_tests.cpp
  tests/state_machine_tests.cpp
  tests/telemetry_payload_tests.cpp
  tests/weather_map_tests.cpp
  client/client_options.cpp
  client/client_utils.cpp
  common/logger.cpp
  server/client_session.cpp
  server/dashboard_state.cpp
  server/imgui_dashboard.cpp
  server/server_options.cpp
  server/server_dashboard_sync.cpp
  server/server_session_logic.cpp
  server/server_utils.cpp
  server/state_machine.cpp
  server/weather_map.cpp
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
)
target_include_directories(unit_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/client
  ${CMAKE_CURRENT_SOURCE_DIR}/common
  ${CMAKE_CURRENT_SOURCE_DIR}/server
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
)
target_link_libraries(unit_tests PRIVATE GTest::gtest_main)
aviation_apply_linker_instrumentation(unit_tests)
aviation_apply_source_warnings(SOURCES
  client/client_options.cpp
  client/client_utils.cpp
  common/logger.cpp
  server/client_session.cpp
  server/dashboard_state.cpp
  server/imgui_dashboard.cpp
  server/server_options.cpp
  server/server_dashboard_sync.cpp
  server/server_session_logic.cpp
  server/server_utils.cpp
  server/state_machine.cpp
  server/weather_map.cpp
)
if(WIN32)
  target_link_libraries(unit_tests PRIVATE ws2_32)
endif()
include(GoogleTest)
gtest_discover_tests(unit_tests)
