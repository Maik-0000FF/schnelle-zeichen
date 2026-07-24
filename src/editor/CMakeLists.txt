# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

# The config editor (Qt/QML app, a separate user-session process like the
# overlay daemon): C++ models + QML views in one module. Skips itself cleanly
# when Qt6 is unavailable; nothing else depends on it.

find_package(Qt6 QUIET COMPONENTS Core DBus Gui Qml Quick QuickControls2 Svg)
if(NOT Qt6_FOUND)
    message(STATUS "Qt6 not found, skipping schnelle-zeichen-editor.")
    return()
endif()

# qt_policy was introduced in Qt 6.5. Skip cleanly on older Qt (e.g. Qt
# 6.4 on Ubuntu 24.04 / Linux Mint 22) so the build still works; the pinned
# RESOURCE_PREFIX below covers the 6.4 default-prefix difference.
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()

include(GNUInstallDirs)

set(EDITOR_SRCS
    main.cpp
    KeyComboUtil.cpp
    MappingListModel.cpp
    MergeManifestModel.cpp
    OverlayDBusClient.cpp
    ProfileListModel.cpp
    SettingsModel.cpp
    SingleInstance.cpp
    keycombo.cpp
)

qt_add_executable(schnelle-zeichen-editor ${EDITOR_SRCS})

set_target_properties(schnelle-zeichen-editor PROPERTIES
    AUTOMOC TRUE
)

set_source_files_properties(Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

# The app logo lives once at the repo root (assets/); alias it into this
# module's resource tree so Theme.appIconSource resolves it from the qrc.
set(EDITOR_LOGO ${CMAKE_SOURCE_DIR}/assets/schnelle-zeichen-logo.svg)
set_source_files_properties(${EDITOR_LOGO} PROPERTIES
    QT_RESOURCE_ALIAS "assets/schnelle-zeichen-logo.svg"
)

qt_add_qml_module(schnelle-zeichen-editor
    URI SchnelleZeichen
    VERSION 1.0
    # Pin the QML resource prefix to /qt/qml on every Qt version. This is the
    # Qt 6.5+ default (via QTP0001), but on Qt 6.4 (Ubuntu 24.04 / Linux Mint
    # 22) the default is /<URI>/, so the module files land at a different path
    # than where `import SchnelleZeichen` and the hard-coded asset URLs look
    # for them. That mismatch left the Theme singleton unresolved on 6.4: every
    # Theme.* binding was undefined and the controls collapsed onto one
    # position. Forcing the prefix keeps the layout identical across distros.
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        Main.qml
        Mappings.qml
        Settings.qml
        Theme.qml
        components/Header.qml
        components/AddMappingCard.qml
        components/MappingRow.qml
        components/EmptyState.qml
        components/Footer.qml
        components/SettingsCard.qml
        components/SettingRow.qml
        components/LabeledSlider.qml
        components/LabeledRangeSlider.qml
        components/LabeledSwitch.qml
        components/ThemedSwitch.qml
        components/DirectionalLeaderRow.qml
        components/CustomLeaderRow.qml
        components/CycleShortcutRow.qml
        components/AppListEditor.qml
        components/ConfirmDialog.qml
        components/AboutDialog.qml
        components/Wordmark.qml
        components/FocusRect.qml
        components/FocusSink.qml
        components/DropdownIndicator.qml
        components/KeyCaptureField.qml
        components/LibrarySelector.qml
        components/PositionPicker.qml
        components/ProfileSelector.qml
        components/ThemeSelector.qml
        components/ThemedComboBox.qml
        components/ThemedTextField.qml
        components/ThemedToolTip.qml
    RESOURCES
        ${EDITOR_LOGO}
)

target_link_libraries(schnelle-zeichen-editor PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    # SVG image-format support for the qrc app logo (Theme.appIconSource).
    Qt6::Svg
    # Shortcut-combo canonicalisation goes through the engine's own parser
    # (combo_parse.h, header-only), which resolves keysyms via xkbcommon.
    PkgConfig::XKBCOMMON
    # Shared theme-palette module (import SchnelleZeichenPalette in Theme.qml).
    schnelle-zeichen-paletteplugin
)

# Shared pure headers: core/... (config formats, paths, themes, capability)
# plus the engine's combo parser and the input contract it includes.
target_include_directories(schnelle-zeichen-editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../input
)

target_compile_definitions(
    schnelle-zeichen-editor
    PRIVATE SCHNELLE_ZEICHEN_VERSION="${PROJECT_VERSION}")

target_compile_options(schnelle-zeichen-editor PRIVATE -Wall -Wextra -Wpedantic)
# QT_NO_KEYWORDS forces Q_SIGNALS / Q_SLOTS / Q_EMIT, same reasoning as the
# overlay target: the Qt macros stay namespaced so a future include of a
# header declaring methods named `emit` / `foreach` is not preprocessor-
# corrupted into Qt's macro forms.
target_compile_definitions(schnelle-zeichen-editor PRIVATE QT_NO_KEYWORDS)

install(TARGETS schnelle-zeichen-editor
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(FILES schnelle-zeichen-editor.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")

install(FILES ${EDITOR_LOGO}
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
        RENAME schnelle-zeichen-editor.svg)

# Absolute install path of the bundled presets, baked in as the last-resort
# fallback for preset_paths.h::presetsDir() when neither the binary-relative
# path nor XDG resolves (e.g. an unusual prefix). The primary resolution stays
# runtime (binary-relative + XDG + env override), so this only needs to be
# correct, not the only source.
target_compile_definitions(schnelle-zeichen-editor PRIVATE
    SCHNELLE_ZEICHEN_PRESETS_DIR="${CMAKE_INSTALL_FULL_DATADIR}/schnelle-zeichen/presets")

# Bundled preset profiles, read at runtime by the editor's "add from library"
# picker (see preset_paths.h). Installed next to the editor's other share files
# so the binary-relative resolution (<prefix>/bin -> <prefix>/share/...) finds
# them on every prefix-based layout, including the Nix store.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/presets/
        DESTINATION "${CMAKE_INSTALL_DATADIR}/schnelle-zeichen/presets"
        FILES_MATCHING PATTERN "*.txt")
