#!/usr/bin/env bash
# BrowserECO — lanceur portable.
#
# Le binaire a jour est celui de build/ ; s'il est plus vieux que la racine ./BrowserECO,
# c'est ce dernier qui est utilise (utile pour une copie portable deja installee).
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_BIN="$DIR/BrowserECO"
BUILD_BIN="$DIR/build/BrowserECO"

choose() {
    # $1 : binaire candidat
    [ -x "$1" ] || return 1
    if [ -x "$BUILD_BIN" ] && [ "$ROOT_BIN" != "$BUILD_BIN" ]; then
        # build/ gagne sauf s'il est nettement plus vieux que la copie racine
        if [ "$ROOT_BIN" -nt "$BUILD_BIN" ]; then
            echo "$ROOT_BIN"
            return 0
        fi
    fi
    echo "$1"
    return 0
}

if BIN="$(choose "$ROOT_BIN")"; then
    exec "$BIN" "$@"
elif [ -x "$BUILD_BIN" ]; then
    exec "$BUILD_BIN" "$@"
elif [ -x "$DIR/build/src/BrowserECO" ]; then
    exec "$DIR/build/src/BrowserECO" "$@"
else
    echo "Binaire BrowserECO introuvable." >&2
    echo "Cherche : $ROOT_BIN , $BUILD_BIN" >&2
    echo "Construire d'abord : cmake -B build && cmake --build build -j\$(nproc)" >&2
    exit 1
fi