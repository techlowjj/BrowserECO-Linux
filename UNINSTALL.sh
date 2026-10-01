#!/usr/bin/env bash
# BrowserECO — désinstallation de ce qui a été installé par
# « cmake --install build --prefix <dossier> ».
#
#   ./UNINSTALL.sh <dossier>     (par défaut : le dossier du projet)
#
# Ne supprime QUE les fichiers listés dans install_manifest.txt, donc jamais
# vos réglages, votre historique ou votre profil. Le dossier des données
# (settings.txt, cache.db, history.db, WebEngineProfile/) est laissé intact :
# supprimez-le à la main si vous voulez repartir de zéro.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${1:-$SCRIPT_DIR}"
MANIFEST="$SCRIPT_DIR/build/install_manifest.txt"

if [ ! -f "$MANIFEST" ]; then
    echo "ERREUR : manifest d'installation introuvable ($MANIFEST)." >&2
    echo "Rejouez d'abord ./INSTALL.sh pour savoir quoi supprimer." >&2
    exit 1
fi

# Chemins guesses puis retranches du prefixe : c'est ce qui evite de
# supprimer un /usr/bin/BrowserECO qui n'aurait pas ete installe par nous.
count=0
while IFS= read -r f; do
    [ -n "$f" ] || continue
    case "$f" in
        "$PREFIX"/*) ;;
        *) echo "Ignore (hors prefixe) : $f" >&2; continue ;;
    esac
    if [ -e "$f" ]; then
        rm -rf -- "$f"
        echo "supprime : ${f#"$PREFIX"/}"
        count=$((count + 1))
    fi
done < "$MANIFEST"

echo
echo "$count fichier(s) supprime(s) sous $PREFIX"
echo "Donnees conservees (volontairement) : settings.txt, cache.db, history.db, WebEngineProfile/"
echo "Pour tout effacer aussi : rm -rf \"$PREFIX\""