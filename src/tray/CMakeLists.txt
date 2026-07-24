# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

# Tray companion: status icon + menu (pause/resume, open editor, quit),
# a pure D-Bus client of the engine's control interface. Needs QtWidgets
# for QSystemTrayIcon/QMenu; skips itself cleanly when unavailable.

find_package(Qt6 QUIET COMPONENTS Core DBus Gui Svg Widgets)
if(NOT Qt6_FOUND)
    message(STATUS "Qt6 (Widgets) not found, skipping schnelle-zeichen-tray.")
    return()
endif()

include(GNUInstallDirs)

qt_add_executable(schnelle-zeichen-tray main.cpp)

set_target_properties(schnelle-zeichen-tray PROPERTIES AUTOMOC TRUE)

# Fallback icon for sessions without a themed icon install: the repo logo,
# embedded under :/tray/.
qt_add_resources(schnelle-zeichen-tray "tray_assets"
    PREFIX "/tray"
    BASE ${CMAKE_SOURCE_DIR}/assets
    FILES ${CMAKE_SOURCE_DIR}/assets/schnelle-zeichen-logo.svg)

target_link_libraries(schnelle-zeichen-tray PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Svg
    Qt6::Widgets
)

# Shared wire-protocol headers (core/control_protocol.h).
target_include_directories(schnelle-zeichen-tray PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_options(schnelle-zeichen-tray PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(schnelle-zeichen-tray PRIVATE QT_NO_KEYWORDS)

install(TARGETS schnelle-zeichen-tray
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
