# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

# Framework-free contracts shared by the engine and every input backend
# (input/) plus the framework-free logic ported from schnelle-umlaute (core/:
# mapping format, cycling order, profile merge, usage counters, config-file
# loading). input/ is header-only (an INTERFACE target publishing the include
# root); core/ carries translation units. Both share the src/ root so headers
# include as "input/..." / "core/...".
add_library(schnelle_zeichen_input INTERFACE)
target_include_directories(schnelle_zeichen_input INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(schnelle_zeichen_core STATIC core/mappings_loader.cpp)
target_include_directories(schnelle_zeichen_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The cycling engine: the legacy keyEvent orchestration on the clean
# three-state signal, event-loop-free (TimerPort) and UI-free (OverlayPort),
# so it unit-tests with fakes. Links xkbcommon for shortcut-combo keysym
# resolution only; layout resolution stays in the backends.
find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
add_library(schnelle_zeichen_engine STATIC engine/engine.cpp)
target_include_directories(
  schnelle_zeichen_engine
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core ${CMAKE_CURRENT_SOURCE_DIR}/input
         ${CMAKE_CURRENT_SOURCE_DIR}/engine)
target_link_libraries(schnelle_zeichen_engine
                      PUBLIC schnelle_zeichen_core PkgConfig::XKBCOMMON)
