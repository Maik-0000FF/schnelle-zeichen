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

# The raw Linux backend (spike findings productionized): evdev grab +
# uinput passthrough, xkb resolution, Wayland virtual-keyboard injection.
# The protocol client code is generated from the vendored XML at build time.
if(SCHNELLE_ZEICHEN_BACKEND_EVDEV)
  pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
  pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
  find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

  set(VK_XML
      ${CMAKE_SOURCE_DIR}/protocols/virtual-keyboard-unstable-v1.xml)
  set(VK_SRC
      ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-unstable-v1-protocol.c)
  set(VK_HDR
      ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-unstable-v1-client-protocol.h
  )
  add_custom_command(
    OUTPUT ${VK_SRC}
    COMMAND ${WAYLAND_SCANNER} private-code ${VK_XML} ${VK_SRC}
    DEPENDS ${VK_XML})
  add_custom_command(
    OUTPUT ${VK_HDR}
    COMMAND ${WAYLAND_SCANNER} client-header ${VK_XML} ${VK_HDR}
    DEPENDS ${VK_XML})

  pkg_check_modules(SDBUS REQUIRED IMPORTED_TARGET libsystemd)
  add_library(
    schnelle_zeichen_backend_evdev STATIC
    backends/evdev/control_service.cpp
    backends/evdev/device_discovery.cpp
    backends/evdev/epoll_timer_port.cpp
    backends/evdev/evdev_key_source.cpp
    backends/evdev/overlay_dbus_client.cpp
    backends/evdev/uinput_forwarder.cpp
    backends/evdev/virtual_keyboard_sink.cpp
    backends/evdev/xkb_resolver.cpp
    ${VK_SRC}
    ${VK_HDR})
  target_include_directories(
    schnelle_zeichen_backend_evdev PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backends/evdev
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(
    schnelle_zeichen_backend_evdev
    PUBLIC schnelle_zeichen_engine PkgConfig::LIBEVDEV
           PkgConfig::WAYLAND_CLIENT PkgConfig::SDBUS)

  add_executable(schnelle-zeichen app/main.cpp)
  target_link_libraries(schnelle-zeichen
                        PRIVATE schnelle_zeichen_backend_evdev)
  include(GNUInstallDirs)
  install(TARGETS schnelle-zeichen
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

# The cycling-preview overlay daemon (Qt/QML + layer-shell), a separate
# user-session process the privileged engine talks to over D-Bus. Skips
# itself cleanly when Qt6 or LayerShellQt are unavailable.
add_subdirectory(overlay)

# The config editor's model layer (Qt/QML). Skips itself cleanly when Qt6
# is unavailable.
add_subdirectory(editor)

# The tray companion (pause/resume, open editor, quit). Skips itself
# cleanly when Qt6 Widgets is unavailable.
add_subdirectory(tray)
