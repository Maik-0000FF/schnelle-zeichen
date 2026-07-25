# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Qt6 QUIET COMPONENTS Core DBus Gui Qml Quick)
if(NOT Qt6_FOUND)
    message(STATUS "Qt6 not found, skipping schnelle-zeichen-overlay.")
    return()
endif()

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()

# The shared theme-palette module lives outside the LayerShellQt gate below:
# the editor imports it too and must build even where LayerShellQt (and thus
# the overlay daemon) is unavailable.
add_subdirectory(palette)

# LayerShellQt is Wayland-only and not packaged on every distro. Skip the
# overlay daemon cleanly if the dev package is missing; the engine does not
# depend on it, so builds on bare systems still succeed.
find_package(LayerShellQt QUIET)
if(NOT LayerShellQt_FOUND)
    message(STATUS
        "LayerShellQt not found, skipping schnelle-zeichen-overlay. "
        "Install layer-shell-qt (Arch), liblayershellqtinterface-dev "
        "(Debian/Ubuntu), layer-shell-qt-devel (Fedora) or "
        "layer-shell-qt6-devel (openSUSE) to enable the cycle overlay.")
    return()
endif()

include(GNUInstallDirs)

set(OVERLAY_SRCS
    main.cpp
    OverlayController.cpp
    CursorSource.cpp
)

qt_add_executable(schnelle-zeichen-overlay ${OVERLAY_SRCS})

set_target_properties(schnelle-zeichen-overlay PROPERTIES
    AUTOMOC TRUE
)

qt_add_qml_module(schnelle-zeichen-overlay
    URI SchnelleZeichenOverlay
    VERSION 1.0
    # Pin the resource prefix to /qt/qml on every Qt version, same as the
    # editor module (see src/editor/CMakeLists.txt). Overlay.qml is a single
    # self-contained file with no module import, so the old-prefix mismatch did
    # not break it on Qt 6.4, but keeping both modules on one prefix removes the
    # latent inconsistency and matches the Qt 6.5+ default.
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        Overlay.qml
)

target_link_libraries(schnelle-zeichen-overlay PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    LayerShellQt::Interface
    # Shared theme-palette module (import SchnelleZeichenPalette in Overlay.qml).
    schnelle-zeichen-paletteplugin
)

# The shared pure headers (overlay_render, geometry, ini parser for the
# initial look) come from core; header-only consumption, no lib link.
target_include_directories(schnelle-zeichen-overlay
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core)
target_compile_definitions(
    schnelle-zeichen-overlay
    PRIVATE SCHNELLE_ZEICHEN_VERSION="${PROJECT_VERSION}")

target_compile_options(schnelle-zeichen-overlay PRIVATE -Wall -Wextra -Wpedantic)
# QT_NO_KEYWORDS forces use of Q_SIGNALS / Q_SLOTS / Q_EMIT / Q_FOREACH in
# OverlayController (already the case today). Same reasoning as the editor
# target: keeps the Qt macros namespaced so a future include of a foreign
# header declaring methods named `emit` / `foreach` is not preprocessor-
# corrupted into Qt's macro forms.
target_compile_definitions(schnelle-zeichen-overlay PRIVATE QT_NO_KEYWORDS)

# Screen-selection API differs across LayerShellQt versions:
#   6.6+  → setWantsToBeOnActiveScreen (preferred)
#   6.0-6.5 → setScreenConfiguration(ScreenFromCompositor) (deprecated 6.6)
#   < 6.0 → neither API exists (Ubuntu 24.04 ships 5.27.11); main.cpp falls
#           back to QWindow::setScreen(primaryScreen()), same-monitor pin.
if(LayerShellQt_VERSION VERSION_GREATER_EQUAL 6.6)
    target_compile_definitions(schnelle-zeichen-overlay
        PRIVATE SCHNELLE_LAYERSHELLQT_HAS_ACTIVE_SCREEN)
elseif(LayerShellQt_VERSION VERSION_GREATER_EQUAL 6.0)
    # Mutually exclusive with the 6.6 branch: 6.6+ uses the new API and
    # must not advertise the deprecated SCREEN_CONFIG path. Matches the
    # #if/#elif structure in main.cpp 1:1 so exactly one tier is active.
    target_compile_definitions(schnelle-zeichen-overlay
        PRIVATE SCHNELLE_LAYERSHELLQT_HAS_SCREEN_CONFIG)
endif()

install(TARGETS schnelle-zeichen-overlay
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# DBus session service for on-demand activation. The Exec= line is
# templated via configure_file() so it tracks the actual install prefix.
# Deviation from legacy (named): installed under CMAKE_INSTALL_DATADIR
# instead of a hardcoded /usr/share, so prefix-based installs (nix) work;
# distro packaging maps datadir to /usr/share anyway. The daemon is started
# lazily on the engine's first Show/enable, never via XDG autostart.
configure_file(
    de.schnelle_zeichen.Overlay.service.in
    de.schnelle_zeichen.Overlay.service
    @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/de.schnelle_zeichen.Overlay.service"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/services")
