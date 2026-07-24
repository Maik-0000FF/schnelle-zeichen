# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
# C is needed for the wayland-scanner-generated protocol code.
project(schnelle-zeichen VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Input backend selection. The framework-independent goal drives the defaults:
# the raw evdev/uinput interposer is the primary Linux path (works on X11 and
# Wayland because it sits below the display server). fcitx5 is an optional
# best-mode backend for users who already run it (true in-app pre-edit). macOS
# uses CGEventTap/CGEventPost. Exactly one primary backend is active at runtime;
# see src/input/KeySource.h for the shared contract every backend delivers.
option(SCHNELLE_ZEICHEN_BACKEND_EVDEV "Linux evdev/uinput interposer (X11 + Wayland)" ON)
option(SCHNELLE_ZEICHEN_BACKEND_FCITX "Optional fcitx5 best-mode backend" OFF)
option(SCHNELLE_ZEICHEN_BACKEND_MACOS "macOS CGEventTap/CGEventPost backend" OFF)

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)
