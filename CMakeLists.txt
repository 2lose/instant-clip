cmake_minimum_required(VERSION 3.24)

project(instant-clip VERSION 1.2.0 LANGUAGES C CXX)

set(UPDATE_REPO "2lose/instant-clip" CACHE STRING "GitHub owner/repo the plugin checks for updates")

# Builds against an installed OBS Studio rather than an OBS source build:
#   deps/obs-studio  sparse checkout (libobs + frontend/api headers) at the installed OBS version
#   deps/ffmpeg      FFmpeg headers at the version OBS ships
#   deps/qt6         Qt headers + moc from the obs-deps package OBS was built with
#   deps/importlibs  import libraries generated from the installed DLLs (scripts/make-import-libs.ps1)
set(DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps" CACHE PATH "Header/import-lib dependency root")
set(OBS_INSTALL_DIR "C:/Program Files/obs-studio" CACHE PATH "Installed OBS Studio")

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This build setup targets 64-bit Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

set(QT_DIR "${DEPS_DIR}/qt6")
set(IMPORT_LIB_DIR "${DEPS_DIR}/importlibs")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")

# Generated headers the source trees don't ship.
file(WRITE "${GEN_DIR}/obsconfig.h" "#pragma once\n#define OBS_RELEASE_CANDIDATE 0\n#define OBS_BETA 0\n")
file(WRITE "${GEN_DIR}/libavutil/avconfig.h"
     "#ifndef AVUTIL_AVCONFIG_H\n#define AVUTIL_AVCONFIG_H\n#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 1\n#endif\n")

set(MOC_HEADERS src/clip-manager.hpp src/updater.hpp)

set(SOURCES
    src/plugin-main.cpp
    src/clip-manager.cpp
    src/clip-manager.hpp
    src/trimmer.cpp
    src/trimmer.hpp
    src/updater.cpp
    src/updater.hpp
    src/sound.cpp
    src/sound.hpp
    src/ui/theme.cpp
    src/ui/theme.hpp
    src/ui/widgets.cpp
    src/ui/widgets.hpp
    src/ui/clips-dock.cpp
    src/ui/clips-dock.hpp)

set(QT_INCLUDES
    "${QT_DIR}/include"
    "${QT_DIR}/include/QtCore"
    "${QT_DIR}/include/QtGui"
    "${QT_DIR}/include/QtWidgets"
    "${QT_DIR}/include/QtSvg")

set(QT_DEFINES QT_CORE_LIB QT_GUI_LIB QT_WIDGETS_LIB QT_SVG_LIB QT_NO_DEBUG UNICODE _UNICODE WIN32 _ENABLE_EXTENDED_ALIGNED_STORAGE)

# moc without the Qt CMake package.
foreach(header IN LISTS MOC_HEADERS)
  get_filename_component(name "${header}" NAME_WE)
  set(out "${GEN_DIR}/moc_${name}.cpp")
  set(moc_args "")
  foreach(def IN LISTS QT_DEFINES)
    list(APPEND moc_args "-D${def}")
  endforeach()
  foreach(inc IN LISTS QT_INCLUDES)
    list(APPEND moc_args "-I${inc}")
  endforeach()
  add_custom_command(
    OUTPUT "${out}"
    COMMAND "${QT_DIR}/bin/moc.exe" ${moc_args} "${CMAKE_CURRENT_SOURCE_DIR}/${header}" -o "${out}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${header}"
    COMMENT "moc ${header}"
    VERBATIM)
  list(APPEND SOURCES "${out}")
endforeach()

add_library(instant-clip MODULE ${SOURCES})

target_include_directories(
  instant-clip
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
          "${GEN_DIR}"
          "${DEPS_DIR}/obs-studio/libobs"
          "${DEPS_DIR}/obs-studio/frontend/api"
          "${DEPS_DIR}/ffmpeg"
          ${QT_INCLUDES})

target_compile_definitions(instant-clip PRIVATE ${QT_DEFINES} NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
                                                 PLUGIN_VERSION="${PROJECT_VERSION}" UPDATE_REPO="${UPDATE_REPO}")

target_compile_options(instant-clip PRIVATE /W3 /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /MP)

target_link_directories(instant-clip PRIVATE "${IMPORT_LIB_DIR}")
target_link_libraries(
  instant-clip
  PRIVATE obs
          obs-frontend-api
          Qt6Core
          Qt6Gui
          Qt6Widgets
          Qt6Svg
          avformat
          avcodec
          avutil
          swscale
          winmm
          winhttp)

set_target_properties(instant-clip PROPERTIES PREFIX "" OUTPUT_NAME "instant-clip")

# Command-line harness for the trimmer (no OBS needed).
add_executable(trim-test tests/trim-test.cpp src/trimmer.cpp)
target_include_directories(trim-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${GEN_DIR}" "${DEPS_DIR}/ffmpeg")
target_compile_options(trim-test PRIVATE /W3 /utf-8)
target_link_directories(trim-test PRIVATE "${IMPORT_LIB_DIR}")
target_link_libraries(trim-test PRIVATE avformat avcodec avutil swscale)
set_target_properties(trim-test PROPERTIES EXCLUDE_FROM_ALL ON)


# Install layout OBS scans on Windows: %ProgramData%\obs-studio\plugins\<name>\{bin\64bit,data}
set(PLUGIN_INSTALL_DIR "$ENV{ProgramData}/obs-studio/plugins/instant-clip" CACHE PATH "Plugin install dir")
install(TARGETS instant-clip LIBRARY DESTINATION "${PLUGIN_INSTALL_DIR}/bin/64bit")
install(FILES "$<TARGET_PDB_FILE:instant-clip>" DESTINATION "${PLUGIN_INSTALL_DIR}/bin/64bit" OPTIONAL)
install(DIRECTORY data/ DESTINATION "${PLUGIN_INSTALL_DIR}/data")
