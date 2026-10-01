#!/usr/bin/env bash
# BrowserECO — construction et installation.
#
#   ./INSTALL.sh                  construit et lance les tests
#   ./INSTALL.sh --no-tests       construit seulement
#   ./INSTALL.sh --dest <dossier> installe ensuite dans ce dossier (défaut :
#                                 le dossier du projet, installation portable)
#   ./INSTALL.sh --with-ntfs      (déprécié) copie aussi vers une clé NTFS ;
#                                 le chemin se donne avec --ntfs-dest
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

WITH_TESTS=1
DEST="$SCRIPT_DIR"
WITH_NTFS=0
NTFS_DEST=""

while [ $# -gt 0 ]; do
    case "$1" in
        --no-tests)   WITH_TESTS=0 ;;
        --dest)       DEST="${2:?--dest attend un dossier}"; shift ;;
        --with-ntfs)  WITH_NTFS=1 ;;
        --ntfs-dest)  NTFS_DEST="${2:?--ntfs-dest attend un dossier}"; shift ;;
        -h|--help)    sed -n '2,10p' "$0"; exit 0 ;;
        *) echo "Option inconnue : $1" >&2; exit 2 ;;
    esac
    shift
done

echo "=== BrowserECO — construction ==="
echo "Source : $SCRIPT_DIR"

# qmake6 n'est PAS necessaire (build 100 % CMake) : on verifie ce qui est
# reellement utilise, sinon l'erreur afficheait un message trompeur.
if ! command -v cmake >/dev/null 2>&1; then
    echo "ERREUR : cmake est introuvable. Installe cmake, g++ et les paquets Qt6." >&2
    exit 1
fi
cmake --version | head -n 1
if ! pkg-config --exists Qt6Widgets 2>/dev/null; then
    echo "ERREUR : Qt6 est introuvable. Lance :" >&2
    echo "  sudo apt install qt6-base-dev qt6-webengine-dev cmake g++" >&2
    exit 1
fi

echo
echo "1/4  configuration"
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

echo
echo "2/4  compilation"
cmake --build build -j"$(nproc)"

if [ "$WITH_TESTS" -eq 1 ]; then
    echo
    echo "3/4  tests"
    QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -j"$(nproc)"
else
    echo
    echo "3/4  tests ignores (--no-tests)"
fi

echo
echo "4/4  installation dans $DEST"
mkdir -p "$DEST"
cmake --install build --prefix "$DEST" >/dev/null

echo "  (icones et .desktop installes aussi dans share/, hors prefixe)"

# Synchronisation vers une cle NTFS : uniquement sur demande explicite, et
# SANS --delete (rsync -a --delete sur un chemin absolu a deja efface des
# fichiers sans avertissement ; un dossier de destination doit toujours
# pouvoir etre verifie avant d'ecraser quoi que ce soit).
if [ "$WITH_NTFS" -eq 1 ]; then
    if [ -z "$NTFS_DEST" ]; then
        echo "ATTENTION : --with-ntfs demande --ntfs-dest <dossier>. Abandon du deploiement NTFS." >&2
    elif [ ! -d "$NTFS_DEST" ]; then
        echo "ATTENTION : $NTFS_DEST est introuvable. Abandon du deploiement NTFS." >&2
    elif ! touch "$NTFS_DEST/.ecowritetest" 2>/dev/null; then
        echo "ATTENTION : $NTFS_DEST n'est pas inscriptible (souvent un arrêt Windows incomplet)." >&2
    else
        rm -f "$NTFS_DEST/.ecowritetest"
        rsync -a --exclude build/ --exclude '*.db*' --exclude WebEngineProfile/ \
              "$SCRIPT_DIR/" "$NTFS_DEST/"
        mkdir -p "$NTFS_DEST/build"
        cp -a build/BrowserECO "$NTFS_DEST/build/"
        echo "Deploiement NTFS termine : $NTFS_DEST"
    fi
fi

echo
echo "=== BUILD OK ==="
echo "Lancer   : ./build/BrowserECO"
echo "Launcher : ./BrowserECO.sh"
if [ -d "$DEST/share/applications" ]; then
    echo "Menu application : $DEST/share/applications/BrowserECO.desktop"
fi