#!/usr/bin/env bash
# Point d'entrée unique pour le développement.
#
#   ./dev.sh              compile si besoin et lance l'application
#   ./dev.sh test         compile et lance toute la suite de tests (headless)
#   ./dev.sh check        tout : warnings, tests, .desktop, version
#   ./dev.sh install [préfixe]   installe (par défaut ~/Applications/BrowserECO-dev)
#   ./dev.sh clean        supprime build/
#   ./dev.sh purge-data   SUPPRIME les données de dev (réglages, historique, profil)
#
# Les données de développement ne sont JAMAIS dans l'arbre source : elles vont
# dans ~/.local/share/BrowserECO/dev (surchargeable par BROWSERECO_DEV_DATA).
# C'est ce qui permet de supprimer build/, de reconfigurer ou de faire n'importe
# quelle manip dans le dépôt sans toucher à un profil de navigation.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"
DATA="${BROWSERECO_DEV_DATA:-$HOME/.local/share/BrowserECO/dev}"
JOBS="$(nproc)"

# QtWebEngine exige un dossier inscriptible ; hors session graphique on force
# offscreen pour que les tests passent en CI comme en local.
headless() { [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; }

build() {
    if [ ! -f "$BUILD/CMakeCache.txt" ]; then
        cmake -B "$BUILD" -S "$ROOT" -DCMAKE_BUILD_TYPE=Release
    fi
    cmake --build "$BUILD" -j"$JOBS"
}

tests() {
    build
    if headless; then
        QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD" --output-on-failure -j"$JOBS"
    else
        ctest --test-dir "$BUILD" --output-on-failure -j"$JOBS"
    fi
}

cmd_build() { build; }

cmd_run() {
    build
    mkdir -p "$DATA"
    echo "Données de dev : $DATA"
    if headless; then
        QT_QPA_PLATFORM=offscreen exec "$BUILD/BrowserECO" --data-dir "$DATA" "$@"
    else
        exec "$BUILD/BrowserECO" --data-dir "$DATA" "$@"
    fi
}

cmd_test() { tests; }

cmd_check() {
    echo "== avertissements du compilateur =="
    # Une compilation propre du début : c'est le seul moyen de voir les
    # avertissements d'un fichier qu'on n'a pas touché.
    rm -rf "$BUILD"
    build 2>&1 | tee /tmp/browsereco-build.log
    local warn
    warn="$(grep -cE "warning:|error:" /tmp/browsereco-build.log || true)"
    echo "   $warn avertissement(s)/erreur(s)"
    [ "$warn" -eq 0 ] || { echo "ECHEC : le build n'est pas propre"; exit 1; }

    echo
    echo "== tests =="
    tests

    echo
    echo "== version =="
    "$BUILD/BrowserECO" --version

    echo
    echo "== entrée de menu =="
    if command -v desktop-file-validate >/dev/null 2>&1; then
        desktop-file-validate "$BUILD/BrowserECO.desktop"
        echo "   BrowserECO.desktop valide"
    else
        echo "   desktop-file-validate absent : vérifié par la CI"
    fi

    echo
    echo "== état du dépôt =="
    git -C "$ROOT" status --short || true
    echo
    echo "TOUT EST VERT"
}

cmd_install() {
    local prefix="${1:-$HOME/Applications/BrowserECO-dev}"
    build
    cmake --install "$BUILD" --prefix "$prefix" >/dev/null
    echo "Installé dans $prefix"
}

cmd_clean() { rm -rf "$BUILD"; echo "build/ supprimé"; }

cmd_purge_data() {
    echo "Données de dev qui vont être SUPPRIMÉES : $DATA"
    read -r -p "Confirmez en tapant oui : " ans
    [ "$ans" = "oui" ] || { echo "Annulé"; exit 1; }
    rm -rf "$DATA"
    echo "Données supprimées : $DATA"
}

case "${1:-run}" in
    run)     shift || true; cmd_run "$@" ;;
    build)   cmd_build ;;
    test)    cmd_test ;;
    check)   cmd_check ;;
    install) shift; cmd_install "$@" ;;
    clean)   cmd_clean ;;
    purge-data) cmd_purge_data ;;
    -h|--help|help)
        sed -n '2,14p' "$0" | sed 's/^#\{1\} \{0,1\}//' ;;
    *) echo "Commande inconnue : $1 (voir ./dev.sh --help)" >&2; exit 2 ;;
esac