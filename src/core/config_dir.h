#ifndef SCHNELLE_ZEICHEN_CORE_CONFIG_DIR_H
#define SCHNELLE_ZEICHEN_CORE_CONFIG_DIR_H

// Resolve schnelle-zeichen's per-user config root per the XDG Base Directory
// spec: $XDG_CONFIG_HOME/schnelle-zeichen when XDG_CONFIG_HOME holds an
// absolute path, otherwise $HOME/.config/schnelle-zeichen. The spec says a
// relative XDG_CONFIG_HOME is invalid and must be ignored, hence the
// leading-slash checks. Replaces fcitx StandardPaths(PkgConfig) from
// schnelle-umlaute, which resolved into fcitx5's config dir instead of an own
// root.
//
// Cold path only (config load/save); nothing here is called per keystroke.

#include <cstdlib>
#include <string>

namespace schnelle_zeichen {

// Directory name under the XDG config base. The one place the product name
// enters the filesystem layout.
inline constexpr const char *kConfigDirName = "schnelle-zeichen";

// Absolute config root without trailing slash, "" when neither
// XDG_CONFIG_HOME nor HOME yields an absolute base (no config is readable or
// writable then).
inline std::string configDir() {
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && xdg[0] == '/') {
        return std::string(xdg) + "/" + kConfigDirName;
    }
    const char *home = std::getenv("HOME");
    if (home != nullptr && home[0] == '/') {
        return std::string(home) + "/.config/" + kConfigDirName;
    }
    return {};
}

// Absolute path of a config-root-relative file, "" when there is no config
// root.
inline std::string configFilePath(const std::string &relPath) {
    const std::string dir = configDir();
    if (dir.empty()) {
        return {};
    }
    return dir + "/" + relPath;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_CONFIG_DIR_H
