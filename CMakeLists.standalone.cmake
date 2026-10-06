# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.16)

project(obs-auto-resize-output VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ---------------------------------------------------------------------------
# Dependencies
#
# Point CMAKE_PREFIX_PATH at an OBS development install that exports the
# libobs / obs-frontend-api CMake packages (e.g. an obs-studio build tree, or
# the prebuilt dependencies used by obs-plugintemplate). Qt6 must match the Qt
# version your OBS was built against (OBS 30+ uses Qt6).
# ---------------------------------------------------------------------------
find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_library(
    ${PROJECT_NAME}
    MODULE
    src/plugin-main.cpp
    src/ScenePreset.cpp
    src/PresetValidation.cpp
    src/AudioSessionState.cpp
    src/AudioSetup.cpp
    src/ApplyPreset.cpp
    src/PresetDock.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE PLUGIN_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ole32 uuid)
endif()

# OBS expects the module file to be named exactly "<name>.{dll,so}" with no
# "lib" prefix.
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

# ---------------------------------------------------------------------------
# Install (optional convenience). For a quick local test on Windows you can
# instead copy the built .dll + the data/ folder into:
#   %ProgramData%\obs-studio\plugins\obs-auto-resize-output\bin\64bit\
#   %ProgramData%\obs-studio\plugins\obs-auto-resize-output\data\
# ---------------------------------------------------------------------------
install(
    TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION "${PROJECT_NAME}/bin/64bit"
    RUNTIME DESTINATION "${PROJECT_NAME}/bin/64bit"
)
install(DIRECTORY data/ DESTINATION "${PROJECT_NAME}/data")
