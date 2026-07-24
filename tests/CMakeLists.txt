# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

# Engine behavior tests: the ported gesture semantics driven through fake
# ports (clock, sink, overlay), no hardware and no event loop involved.
add_executable(engine_test engine_test.cpp)
target_link_libraries(engine_test PRIVATE schnelle_zeichen_engine)
add_test(NAME engine_test COMMAND engine_test)

# Editor<->engine combo contract: the capture field's emitted combo strings
# must parse through the engine's parser to the intended modifiers + keysym.
# Needs Qt6::Core only (Qt::Key values and QString); skipped without Qt6,
# like the editor itself.
find_package(Qt6 QUIET COMPONENTS Core)
if(Qt6_FOUND)
  # Imported pkg-config targets are directory-scoped; resolve xkbcommon here
  # too (same module the engine dir resolves for itself).
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
  add_executable(keycombo_test keycombo_test.cpp
                               ${CMAKE_SOURCE_DIR}/src/editor/keycombo.cpp)
  target_include_directories(
    keycombo_test PRIVATE ${CMAKE_SOURCE_DIR}/src/editor
                          ${CMAKE_SOURCE_DIR}/src/engine
                          ${CMAKE_SOURCE_DIR}/src/input)
  target_link_libraries(keycombo_test PRIVATE Qt6::Core PkgConfig::XKBCOMMON)
  add_test(NAME keycombo_test COMMAND keycombo_test)
else()
  message(STATUS "Qt6 not found, skipping keycombo_test.")
endif()
