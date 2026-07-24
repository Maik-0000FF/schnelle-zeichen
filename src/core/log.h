#ifndef SCHNELLE_ZEICHEN_CORE_LOG_H
#define SCHNELLE_ZEICHEN_CORE_LOG_H

// Minimal framework-free warning sink, replacing FCITX_WARN from
// schnelle-umlaute. Core code reports skipped malformed input through warn();
// an embedding backend may redirect it (e.g. into fcitx logging) via
// setWarnHandler, set once at startup before any other thread runs. Default:
// stderr. Only cold paths (config load/save) log; the per-keystroke hot path
// never does.

#include <cstdio>
#include <string>

namespace schnelle_zeichen {

using WarnHandler = void (*)(const std::string &message);

namespace detail {
inline WarnHandler &warnHandler() {
    static WarnHandler handler = nullptr;
    return handler;
}
} // namespace detail

inline void setWarnHandler(WarnHandler handler) {
    detail::warnHandler() = handler;
}

inline void warn(const std::string &message) {
    if (WarnHandler handler = detail::warnHandler()) {
        handler(message);
        return;
    }
    std::fprintf(stderr, "schnelle-zeichen: %s\n", message.c_str());
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_LOG_H
