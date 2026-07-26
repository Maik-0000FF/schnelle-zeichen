# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

# Engine behavior tests: the ported gesture semantics driven through fake
# ports (clock, sink, overlay), no hardware and no event loop involved.
add_executable(engine_test engine_test.cpp)
target_link_libraries(engine_test PRIVATE schnelle_zeichen_engine)
add_test(NAME engine_test COMMAND engine_test)

# Core format-layer tests: the shared parsers/serializers (ini, usage,
# merge manifest, mapping outputs, mappings + profiles readers) both the engine
# and the editor consume.
add_executable(format_io_test format_io_test.cpp)
target_link_libraries(format_io_test PRIVATE schnelle_zeichen_core)
add_test(NAME format_io_test COMMAND format_io_test)

# Profile-merge composition: the pure base-anchored merge logic (natural order,
# duplicate provenance, order overrides, runtime projection). Header-only core,
# no framework.
add_executable(profile_compose_test profile_compose_test.cpp)
target_link_libraries(profile_compose_test PRIVATE schnelle_zeichen_core)
add_test(NAME profile_compose_test COMMAND profile_compose_test)

# Accent-window boundary math: case selection, the min >= max degenerate guard,
# unlimited mode, and the inclusive/exclusive window edges. Links the engine
# library for its include dirs and xkbcommon (multi-byte uppercase check).
add_executable(accent_window_test accent_window_test.cpp)
target_link_libraries(accent_window_test PRIVATE schnelle_zeichen_engine)
add_test(NAME accent_window_test COMMAND accent_window_test)

# Self-grab name rule: header-only, no device fd and no libevdev needed.
add_executable(device_discovery_test device_discovery_test.cpp)
target_include_directories(device_discovery_test
                           PRIVATE ${CMAKE_SOURCE_DIR}/src/backends/evdev)
add_test(NAME device_discovery_test COMMAND device_discovery_test)

# Imported pkg-config targets are directory-scoped; resolve xkbcommon here
# (same module the engine dir resolves for itself), shared by the resolver
# and keycombo tests below.
find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

# Resolver state tests: modifier tracking and grab-time lock seeding, no
# hardware involved. Needs a compilable system keymap (xkeyboard-config);
# the binary exits 77 where none is available, reported as skipped.
add_executable(xkb_resolver_test xkb_resolver_test.cpp
                                 ${CMAKE_SOURCE_DIR}/src/backends/evdev/xkb_resolver.cpp)
target_include_directories(
  xkb_resolver_test PRIVATE ${CMAKE_SOURCE_DIR}/src/backends/evdev
                            ${CMAKE_SOURCE_DIR}/src/input)
target_link_libraries(xkb_resolver_test PRIVATE PkgConfig::XKBCOMMON)
add_test(NAME xkb_resolver_test COMMAND xkb_resolver_test)
set_tests_properties(xkb_resolver_test PROPERTIES SKIP_RETURN_CODE 77)

# Editor<->engine combo contract: the capture field's emitted combo strings
# must parse through the engine's parser to the intended modifiers + keysym.
# Needs Qt6::Core only (Qt::Key values and QString); skipped without Qt6,
# like the editor itself.
find_package(Qt6 QUIET COMPONENTS Core Qml)
if(Qt6_FOUND)
  add_executable(keycombo_test keycombo_test.cpp
                               ${CMAKE_SOURCE_DIR}/src/editor/keycombo.cpp)
  target_include_directories(
    keycombo_test PRIVATE ${CMAKE_SOURCE_DIR}/src/editor
                          ${CMAKE_SOURCE_DIR}/src/engine
                          ${CMAKE_SOURCE_DIR}/src/input)
  target_link_libraries(keycombo_test PRIVATE Qt6::Core PkgConfig::XKBCOMMON)
  add_test(NAME keycombo_test COMMAND keycombo_test)

  # Shortcut-collision rules (SelectKeys, cycle slots, reserved pause
  # toggle) against a temporary config root. Needs Qml only for the
  # QML_ELEMENT registration in the model header.
  add_executable(profile_combo_test
                 profile_combo_test.cpp
                 ${CMAKE_SOURCE_DIR}/src/editor/ProfileListModel.cpp)
  set_target_properties(profile_combo_test PROPERTIES AUTOMOC TRUE)
  target_include_directories(
    profile_combo_test PRIVATE ${CMAKE_SOURCE_DIR}/src/editor
                               ${CMAKE_SOURCE_DIR}/src
                               ${CMAKE_SOURCE_DIR}/src/engine
                               ${CMAKE_SOURCE_DIR}/src/input)
  target_link_libraries(profile_combo_test PRIVATE Qt6::Core Qt6::Qml
                                                   PkgConfig::XKBCOMMON)
  add_test(NAME profile_combo_test COMMAND profile_combo_test)
else()
  message(STATUS "Qt6 not found, skipping keycombo_test.")
endif()
