// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_CURSOR_REQUEST_H
#define SCHNELLE_ZEICHEN_CORE_CURSOR_REQUEST_H

// Query-id bookkeeping for the KWin cursor source: which reply belongs to
// the query in flight, and how ids are handed out. Pure, unit-tested
// without a compositor. 1:1 port.

#include <limits>

namespace schnelle_zeichen {

// kNoRequest means "no query in flight"; it is never handed to a script, so
// an idle source cannot be matched by any reply. Ids travel over D-Bus as a
// plain int (KWin's callDBus marshals a script number as int32).
constexpr int kNoRequest = 0;
constexpr int kFirstRequestId = 1;

// True when a reply belongs to the query actually in flight. Correlation,
// not authorisation: it keeps replies apart, not callers out.
inline bool isReplyForActiveQuery(int replyId, int activeId) {
    return activeId != kNoRequest && replyId == activeId;
}

// The counter value after `counter`, wrapping back to the first id instead
// of overflowing (signed overflow is UB); the wrap never yields kNoRequest.
inline int nextRequestId(int counter) {
    return counter == std::numeric_limits<int>::max() ? kFirstRequestId
                                                      : counter + 1;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_CURSOR_REQUEST_H
