# CMake Windows defaults module

include_guard(GLOBAL)

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

include(buildspec)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  # Forward slashes: a raw "C:\ProgramData" breaks cmake_install.cmake ("\P" escape).
  file(TO_CMAKE_PATH "$ENV{ALLUSERSPROFILE}/obs-studio/plugins" _obs_plugins_dir)
  set(
    CMAKE_INSTALL_PREFIX
    "${_obs_plugins_dir}"
    CACHE STRING
    "Default plugin installation directory"
    FORCE
  )
endif()
