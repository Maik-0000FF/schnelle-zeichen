// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_EDITOR_PROTOCOL_H
#define SCHNELLE_ZEICHEN_CORE_EDITOR_PROTOCOL_H

// The editor's single-instance D-Bus identity, the single source for the
// editor (which claims the name and answers Raise, see editor/SingleInstance)
// and the tray (which raises a running editor before spawning a new one).
// Session bus. Reverse-domain naming matching the engine and overlay
// identities (control_protocol.h, overlay_protocol.h); the `1` is the
// major-version suffix recommended by the freedesktop spec so a future
// incompatible API change can coexist with this one.
//
// Q_CLASSINFO needs a preprocessor literal, so the interface name also
// exists as a #define (consumed by SingleInstanceAdaptor's Q_CLASSINFO);
// the constant below aliases it so there is still one definition.
#define SZ_EDITOR_DBUS_INTERFACE "de.schnelle_zeichen.Editor1"

namespace schnelle_zeichen {

// Service name and interface name are deliberately the same string: the
// editor exposes exactly one interface under its well-known name.
inline constexpr const char *kEditorService = SZ_EDITOR_DBUS_INTERFACE;
inline constexpr const char *kEditorPath = "/Editor";
inline constexpr const char *kEditorInterface = SZ_EDITOR_DBUS_INTERFACE;

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_EDITOR_PROTOCOL_H
