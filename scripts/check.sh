#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run the GitHub CI's fast checks locally before pushing, so nothing red
# reaches the remote. Mirrors the workflow's build + ctest, clang-format,
# clang-tidy, REUSE, shellcheck and actionlint jobs. The multi-distro
# build/install matrices and the sanitizer job need containers, so they
# stay in CI only.
#
# Usage (re-enters the Nix dev shell automatically if needed):
#   ./scripts/check.sh          # fast gates
#   ./scripts/check.sh --nix    # also run `nix flake check` + `nix build`
set -euo pipefail

# Re-exec inside the dev shell so clang, clang-tidy, shellcheck, actionlint and
# the Qt6/libevdev headers are all on PATH.
if [ -z "${IN_NIX_SHELL:-}" ]; then
    exec nix develop --command "$0" "$@"
fi

RUN_NIX=0
[ "${1:-}" = "--nix" ] && RUN_NIX=1

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR=build-check # gitignored via build-*/

step() { printf '\n\033[1;34m==> %s\033[0m\n' "$1"; }

step "clang-format (dry-run, -Werror)"
git ls-files '*.cpp' '*.h' | xargs clang-format --dry-run --Werror

step "REUSE compliance"
reuse lint

step "actionlint (workflow syntax)"
actionlint

step "shellcheck (scripts, warnings and above)"
shellcheck -S warning install.sh uninstall.sh scripts/check.sh

step "autostart unit names in sync (install.sh vs uninstall.sh)"
# The two scripts duplicate the systemd unit names so uninstall.sh stays
# standalone; enforce that they agree (sorted, so a reorder is not a change).
pat='^(USER_UNIT_DIR|ENGINE_UNIT_NAME|TRAY_UNIT_NAME)='
if ! diff <(grep -E "$pat" install.sh | sort) \
          <(grep -E "$pat" uninstall.sh | sort); then
    echo "autostart unit names drifted between install.sh and uninstall.sh"
    exit 1
fi

step "configure + build (clang, compile_commands)"
cmake -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"

step "ctest"
ctest --test-dir "$BUILD_DIR" --output-on-failure

step "clang-tidy (src + tests)"
# Unquoted on purpose: the file list splits into one argument per source file.
files=$(git ls-files 'src/*.cpp' 'tests/*.cpp')
run-clang-tidy -p "$BUILD_DIR" -j "$(nproc)" -quiet $files

if [ "$RUN_NIX" = 1 ]; then
    step "nix flake check"
    nix flake check -L
    step "nix build (all four binaries + derivation check phase)"
    nix build --print-build-logs
fi

printf '\n\033[1;32mAll local checks passed.\033[0m\n'
