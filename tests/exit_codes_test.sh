#!/bin/sh
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The daemon's startup exit codes, driven through --check-session so no
# keyboard is grabbed and no session is disturbed.
#
# What this pins down: an unreachable compositor must NOT report the permanent
# "this session is unsupported" code. That distinction is load-bearing, because
# the engine unit suppresses restarts for the permanent code; collapsing the
# two would take the service down for good on a compositor restart or a slow
# login, exactly when the retry is needed most.
#
# Not covered here: the permanent case itself, and it is not covered by hand
# either. Producing it needs a compositor that answers and advertises neither
# zwp_virtual_keyboard_v1 nor zwp_input_method_v1, which no build environment
# provides and which none of the compositors available for testing are. A
# weston run does reach the permanent-looking path, but through a rejected
# bind (weston holds its own input method), which is a different situation and
# reports a transient failure now.

set -eu

ENGINE=${1:?usage: exit_codes_test.sh <path to schnelle-zeichen>}

# Value the daemon uses for a session that can never work, read from the header
# that defines it (install.sh and nix/home-module.nix parse the same line).
HEADER="$(dirname "$0")/../src/core/exit_codes.h"
UNSUPPORTED=$(
    sed -n 's/.*kExitSessionUnsupported = \([0-9]\{1,\}\);.*/\1/p' "$HEADER" |
        head -1
)
if [ -z "$UNSUPPORTED" ]; then
    echo "FAIL: could not read kExitSessionUnsupported from $HEADER"
    exit 1
fi

failures=0

check() {
    label=$1
    expected_status=$2
    expected_text=$3
    actual_output=$4
    actual_status=$5

    if [ "$actual_status" != "$expected_status" ]; then
        echo "FAIL: $label: expected exit $expected_status, got $actual_status"
        if [ "$actual_status" = "$UNSUPPORTED" ]; then
            # The specific regression this test exists for, worth naming
            # rather than leaving as a bare number mismatch.
            echo "  that is the permanent code: restarts would be suppressed" \
                "for a failure that can heal"
        fi
        echo "  output: $actual_output"
        failures=$((failures + 1))
        return
    fi
    case "$actual_output" in
        *"$expected_text"*) ;;
        *)
            # Guards against passing for an unrelated reason: without the text
            # check, any other early failure would also exit 1 and look green.
            echo "FAIL: $label: exit code right but message missing"
            echo "  expected to contain: $expected_text"
            echo "  output: $actual_output"
            failures=$((failures + 1))
            return
            ;;
    esac
    echo "ok: $label (exit $actual_status)"
}

# No compositor answers: transient, so plain failure and the service manager's
# normal retry, never the permanent code.
tmp_runtime=$(mktemp -d)
trap 'rm -rf "$tmp_runtime"' EXIT
status=0
output=$(
    WAYLAND_DISPLAY=schnelle-zeichen-no-such-display \
        XDG_RUNTIME_DIR="$tmp_runtime" \
        "$ENGINE" --check-session 2>&1
) || status=$?
check "unreachable compositor" 1 "no compositor reachable" "$output" "$status"

if [ "$failures" -ne 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
echo "all exit-code checks passed"
