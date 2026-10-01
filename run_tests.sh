#!/usr/bin/env bash
# Lance toute la suite de tests de BrowserECO-Linux.
#
#   ./run_tests.sh            # compile puis exécute via ctest (artefacts dans build/tests)
#
# Les tests tournent en headless (QT_QPA_PLATFORM=offscreen), chacun dans son
# propre dossier build/tests/run/<test> : settings.txt, cache.db et history.db
# réellement utilisés par l'application ne sont jamais touchés.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"
JOBS="$(nproc)"

echo "== Configuration =="
cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null

echo "== Construction =="
cmake --build "$BUILD" -j"$JOBS"

echo "== Tests =="
# Chaque test a son propre TIMEOUT (tests/CMakeLists.txt) : aucun blocage infini.
# Les binaires restent dans build/tests (--keep n'a plus rien à faire).
ctest --test-dir "$BUILD" --output-on-failure -j"$JOBS"