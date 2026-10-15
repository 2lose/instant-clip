cmake_minimum_required(VERSION 3.28...3.31)

# Standard OBS plugin build (from obsproject/obs-plugintemplate). The first configure
# downloads the OBS sources and prebuilt dependencies named in buildspec.json into .deps/
# and builds libobs + obs-frontend-api from them.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

if(NOT WIN32)
  message(FATAL_ERROR "Instant Clip is Windows-only for now.")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

set(CMAKE_CXX_STANDARD 20)
set(UPDATE_REPO "2lose/instant-clip" CACHE STRING "GitHub owner/repo the plugin checks for updates")

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON)

# FFmpeg comes with the prebuilt obs-deps package (already on CMAKE_PREFIX_PATH).
find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h REQUIRED)
foreach(lib IN ITEMS avformat avcodec avutil swscale)
  find_library(FFMPEG_${lib}_LIBRARY ${lib} REQUIRED)
  list(APPEND FFMPEG_LIBRARIES "${FFMPEG_${lib}_LIBRARY}")
endforeach()

# Encrypted values for official releases (update token, feedback relay). scripts/embed-secrets.ps1
# writes the real file when publishing; everyone else gets empty values, which just switches
# off update checks and the Feedback form.
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
if(NOT EXISTS "${GEN_DIR}/embedded-secrets.inc")
  file(
    WRITE "${GEN_DIR}/embedded-secrets.inc"
    "// No embedded secrets in this build.\n"
    "static const EmbeddedBlob kUpdateToken = {nullptr, 0, {0}, {0}, {0}};\n"
    "static const EmbeddedBlob kFeedbackUrl = {nullptr, 0, {0}, {0}, {0}};\n"
    "static const EmbeddedBlob kFeedbackKey = {nullptr, 0, {0}, {0}, {0}};\n"
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/clip-manager.cpp
    src/clip-manager.hpp
    src/trimmer.cpp
    src/trimmer.hpp
    src/updater.cpp
    src/updater.hpp
    src/http.cpp
    src/http.hpp
    src/feedback.cpp
    src/feedback.hpp
    src/background.hpp
    src/embedded-token.cpp
    src/embedded-token.hpp
    src/sound.cpp
    src/sound.hpp
    src/ui/theme.cpp
    src/ui/theme.hpp
    src/ui/widgets.cpp
    src/ui/widgets.hpp
    src/ui/clips-dock.cpp
    src/ui/clips-dock.hpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src "${GEN_DIR}" "${FFMPEG_INCLUDE_DIR}")
target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN PLUGIN_VERSION="${PLUGIN_VERSION}" UPDATE_REPO="${UPDATE_REPO}"
)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES} winmm winhttp crypt32 bcrypt)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Command-line harness for the trimmer (no OBS needed): cmake --build --preset windows-x64 --target trim-test
add_executable(trim-test tests/trim-test.cpp src/trimmer.cpp)
target_include_directories(trim-test PRIVATE src "${FFMPEG_INCLUDE_DIR}")
target_compile_definitions(trim-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(trim-test PRIVATE ${FFMPEG_LIBRARIES})
set_target_properties(trim-test PROPERTIES EXCLUDE_FROM_ALL ON)

# Round-trip check for the embedded values (prints match/mismatch only).
add_executable(token-test tests/token-test.cpp src/embedded-token.cpp)
target_include_directories(token-test PRIVATE src "${GEN_DIR}")
target_compile_definitions(token-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(token-test PRIVATE bcrypt)
set_target_properties(token-test PROPERTIES EXCLUDE_FROM_ALL ON)
