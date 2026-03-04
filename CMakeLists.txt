cmake_minimum_required(VERSION 3.20)
project(HNxFMRadio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release build optimized for Pi Zero W 2
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ALSA
find_package(ALSA REQUIRED)

# nlohmann/json (header-only)
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
  DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# Create a header-only interface target for nlohmann/json
add_library(nlohmann_json_iface INTERFACE)
target_include_directories(nlohmann_json_iface INTERFACE
  ${nlohmann_json_SOURCE_DIR}
)

# Sources
set(SOURCES
  src/main.cpp
  src/Config.cpp
  src/Logger.cpp
  src/AudioLoopback.cpp
  src/FMTransmitterManager.cpp
  src/AudioInjector.cpp
  src/HttpServer.cpp
)

add_executable(hnxfmradiod ${SOURCES})

target_include_directories(hnxfmradiod PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${ALSA_INCLUDE_DIRS}
)

target_link_libraries(hnxfmradiod PRIVATE
  ${ALSA_LIBRARIES}
  nlohmann_json_iface
  pthread
)

target_compile_options(hnxfmradiod PRIVATE
  -Wall -Wextra -Wpedantic
  $<$<CONFIG:Release>:-O2>
)

install(TARGETS hnxfmradiod DESTINATION /usr/local/bin)
install(FILES systemd/hnxfmradiod.service DESTINATION /etc/systemd/system)
install(FILES config/hnxfmradio.conf.example DESTINATION /etc)
