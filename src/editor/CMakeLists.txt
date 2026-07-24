# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

# The config editor's C++ model layer (Qt/QML app, a separate user-session
# process like the overlay daemon). Built as a static QML module; the
# executable and the QML views join this module in the next port step.
# Skips itself cleanly when Qt6 is unavailable; nothing else depends on it.

find_package(Qt6 QUIET COMPONENTS Core DBus Gui Qml Quick)
if(NOT Qt6_FOUND)
    message(STATUS "Qt6 not found, skipping schnelle-zeichen-editor.")
    return()
endif()

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()

set(EDITOR_MODEL_SRCS
    KeyComboUtil.cpp
    KeyComboUtil.h
    MappingListModel.cpp
    MappingListModel.h
    MergeManifestModel.cpp
    MergeManifestModel.h
    OverlayDBusClient.cpp
    OverlayDBusClient.h
    ProfileListModel.cpp
    ProfileListModel.h
    SettingsModel.cpp
    SettingsModel.h
    SingleInstance.cpp
    SingleInstance.h
    keycombo.cpp
    keycombo.h
)

qt_add_library(schnelle_zeichen_editor STATIC)

set_target_properties(schnelle_zeichen_editor PROPERTIES
    AUTOMOC TRUE
)

qt_add_qml_module(schnelle_zeichen_editor
    URI SchnelleZeichen
    VERSION 1.0
    # Pinned like the overlay module, so build-tree and installed lookups
    # agree on one prefix (the Qt 6.5+ default).
    RESOURCE_PREFIX "/qt/qml"
    # The build-tree module dir must end in the module's target path, or
    # qmllint/qmlcachegen tooling misresolves the import.
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/SchnelleZeichen"
    SOURCES
        ${EDITOR_MODEL_SRCS}
)

target_link_libraries(schnelle_zeichen_editor PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    # Shortcut-combo canonicalisation goes through the engine's own parser
    # (combo_parse.h, header-only), which resolves keysyms via xkbcommon.
    PkgConfig::XKBCOMMON
)

# Shared pure headers: core/... (config formats, paths, themes, capability)
# plus the engine's combo parser and the input contract it includes.
target_include_directories(schnelle_zeichen_editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../input
)

target_compile_options(schnelle_zeichen_editor PRIVATE -Wall -Wextra -Wpedantic)
# QT_NO_KEYWORDS forces Q_SIGNALS / Q_SLOTS / Q_EMIT, same reasoning as the
# overlay target: the Qt macros stay namespaced so a future include of a
# header declaring methods named `emit` / `foreach` is not preprocessor-
# corrupted into Qt's macro forms.
target_compile_definitions(schnelle_zeichen_editor PRIVATE QT_NO_KEYWORDS)
