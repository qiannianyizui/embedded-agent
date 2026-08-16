#!/usr/bin/env bash
# Idempotent FTXUI patch step: applies the vendored nonblocking-flush patch
# only when the pre-populated source directory does not already contain it.
set -e
ROOT="$1"

if ! grep -q O_NONBLOCK src/ftxui/component/app.cpp 2>/dev/null; then
    patch -p1 < "$ROOT/cmake/patches/ftxui-nonblocking-flush.patch"
fi
