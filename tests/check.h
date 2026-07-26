// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_TESTS_CHECK_H
#define SCHNELLE_ZEICHEN_TESTS_CHECK_H

// Minimal shared harness for the framework-free unit tests: a per-binary
// failure counter and a CHECK macro that logs file:line plus the failed
// expression. Each test is its own single-translation-unit executable, so the
// static counter is one instance per binary (no ODR concern). Kept tiny on
// purpose; the suite deliberately avoids a full test framework. main() reads
// `failures` to decide its exit code.

#include <cstdio>

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

#endif // SCHNELLE_ZEICHEN_TESTS_CHECK_H
