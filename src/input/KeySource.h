#ifndef SCHNELLE_ZEICHEN_INPUT_KEYSOURCE_H
#define SCHNELLE_ZEICHEN_INPUT_KEYSOURCE_H

#include <cstdint>
#include <functional>
#include <string>

namespace schnelle_zeichen {

// The three physical key states the engine reasons about. This is the single
// normalized signal every backend must deliver. Whatever un-mangling a platform
// needs to reconstruct it stays *inside* that backend and never reaches the
// engine:
//   - evdev/uinput: unambiguous at the source (kernel value 0/1/2), no work.
//   - X11:          needs XkbSetDetectableAutoRepeat(True) so a held key stops
//                   emitting phantom Release/Press pairs; then one real Release.
//   - fcitx:        the frozen-event-time trick that classifies a synthetic
//                   auto-repeat release (schnelle-umlaute issue #73).
// The engine above therefore never has to know how a release was recognized.
enum class KeyAction {
    Press,   // fresh physical key-down (evdev value 1)
    Repeat,  // auto-repeat while held, NOT a new gesture (evdev value 2)
    Release, // real physical key-up, the only cycling-ending signal (evdev 0)
};

// Backend-agnostic modifier bitmask. AltGr is kept distinct from Alt because a
// leader gesture must tell an ISO_Level3_Shift apart from a plain Alt.
enum class KeyModifier : uint32_t {
    None = 0u,
    Shift = 1u << 0,
    Ctrl = 1u << 1,
    Alt = 1u << 2,
    AltGr = 1u << 3,
    Super = 1u << 4,
};

inline uint32_t operator|(KeyModifier a, KeyModifier b) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}
inline uint32_t operator&(uint32_t mask, KeyModifier b) {
    return mask & static_cast<uint32_t>(b);
}

// One normalized key event. `code` is the layout-independent hardware keycode
// (stable identity for held-key tracking); `text` is the layout-resolved UTF-8
// character(s) for a printable key, empty otherwise; `timeUsec` is a monotonic
// timestamp for hold-duration logic. Resolving `code`+`modifiers` to `text` is
// the backend's job (libxkbcommon on Linux, the OS on macOS), never the
// engine's.
struct KeyEvent {
    KeyAction action = KeyAction::Press;
    uint32_t code = 0;
    uint32_t modifiers = static_cast<uint32_t>(KeyModifier::None);
    std::string text;
    uint64_t timeUsec = 0;
};

// A backend that reads the physical key stream and delivers normalized events.
// Implementations: evdev/uinput (Linux, primary), fcitx (optional best-mode),
// CGEventTap (macOS). The engine owns exactly one active KeySource at a time and
// decides per event whether to consume it (swallow from the host) or pass it
// through; how that consumption is realized is again the backend's concern
// (EVIOCGRAB + re-inject on Linux, the tap's return value on macOS).
class KeySource {
public:
    using Handler = std::function<void(const KeyEvent &)>;

    virtual ~KeySource() = default;

    // Register the event sink. Called once before start().
    virtual void setHandler(Handler handler) = 0;

    // Begin delivering events. Non-blocking: the backend integrates its source
    // into the running event loop rather than blocking the caller.
    virtual bool start() = 0;

    // Stop delivering and release any exclusive device grab.
    virtual void stop() = 0;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_INPUT_KEYSOURCE_H
