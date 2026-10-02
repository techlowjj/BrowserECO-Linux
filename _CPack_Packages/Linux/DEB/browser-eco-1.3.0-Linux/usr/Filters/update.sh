#!/usr/bin/env bash
# Télécharge les listes de filtres EasyList / EasyPrivacy.
#
# Elles ne sont PAS versionnées : ~3,5 Mo, elles changent souvent, et leur
# licence (GPLv3 ou CC BY-SA, voir LICENSE) impose de les redistribuer
# séparément de notre code.
#
#   Filters/update.sh              # télécharge dans ce dossier
#   BROWSERECO_FILTERS=… …        # ailleurs (l'application lit BROWSERECO_FILTERS)
#
# Attribution : voir LICENSE et README.md de ce dossier.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
command -v curl >/dev/null 2>&1 || { echo "curl est nécessaire" >&2; exit 1; }

declare -A LISTES=(
    [easylist.txt]="https://easylist.to/easylist/easylist.txt"
    [easyprivacy.txt]="https://easylist.to/easylist/easyprivacy.txt"
)

cd "$DIR"
for nom in "${!LISTES[@]}"; do
    url="${LISTES[$nom]}"
    printf '  %-18s ' "$nom"
    if curl -fsSL --max-time 120 -o "$nom.tmp" "$url"; then
        # Un proxy d'entreprise peut renvoyer une page HTML au lieu de la liste :
        # on refuse alors le fichier, sinon AdBlocker chargerait du vide.
        if head -c 200 "$nom.tmp" | grep -qi "<!DOCTYPE html\|<html"; then
            rm -f "$nom.tmp"
            echo "refusé (HTML reçu, pas une liste) — proxy ?"
            exit 1
        fi
        mv "$nom.tmp" "$nom"
        printf 'ok (%s)\n' "$(du -h "$nom" | cut -f1)"
    else
        rm -f "$nom.tmp"
        echo "échec — $url"
        exit 1
    fi
done

echo
echo "Listes prêtes dans $DIR"
echo "Nombre de règles : $(cat easylist.txt easyprivacy.txt 2>/dev/null | grep -cvE '^\s*($|!|#|\[)')"