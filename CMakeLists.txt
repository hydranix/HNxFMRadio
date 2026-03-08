cmake_minimum_required(VERSION 3.20)
project(HNxFMRadio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release build optimized for Pi Zero W 2
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# nlohmann/json (header-only)
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

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
)

target_link_libraries(hnxfmradiod PRIVATE
  nlohmann_json::nlohmann_json
  pthread
)

target_compile_options(hnxfmradiod PRIVATE
  -Wall -Wextra -Wpedantic
  $<$<CONFIG:Release>:-O2>
)

install(TARGETS hnxfmradiod DESTINATION /usr/local/bin)
install(FILES systemd/hnxfmradiod.service DESTINATION /etc/systemd/system)
install(FILES config/hnxfmradio.conf.example DESTINATION /etc)
