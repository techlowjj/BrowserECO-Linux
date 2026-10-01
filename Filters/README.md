# Listes de filtres

Ce dossier contient (ou doit contenir) les listes de filtres utilisées par
`AdBlocker`, au format Adblock Plus :

| Fichier | Contenu | Rôle |
|---|---|---|
| `easylist.txt` | EasyList | publicités et contenus intrigants |
| `easyprivacy.txt` | EasyPrivacy | traqueurs, pixels, historique |

## Licences et attribution

Ces listes sont des œuvres tierces, publiées sous leurs propres licences.
Elles **ne sont pas** couvertes par la GPL-3.0 de ce projet et ne sont pas
redistribuées avec le code source.

- **EasyList / EasyPrivacy**
  - Auteur : The EasyList Authors ( easylist.to )
  - Licence : GPLv3 ou CC BY-SA 3.0, au choix du redistributeur
    (<https://easylist.to/>)
  - Téléchargement :
    <https://easylist.to/easylist/easylist.txt>
    <https://easylist.to/easylist/easyprivacy.txt>
  - © LesEasyList Authors — utilisé conformément aux conditions ci-dessus.

Si vous redistribuez ce logiciel, vous devez soit :
1. inclure ces listes et reproduire cette attribution, soit
2. ne pas les inclure (l'application démarre alors sans blocage et
   l'indique dans la barre d'état) et les laisser télécharger séparément.

## Mise à jour

```bash
cd Filters
curl -sSLo easylist.txt     https://easylist.to/easylist/easylist.txt
curl -sSLo easyprivacy.txt  https://easylist.to/easylist/easyprivacy.txt
```

Les fichiers sont ignorés par git (ils pèsent ~3,5 Mo et changent souvent).
Au démarrage, `AdBlocker` lit tous les `*.txt` du dossier ; le dossier est
cherché à côté du binaire, puis à `../Filters`, puis dans le dossier de
profil, puis dans `/usr/share/BrowserECO/Filters`, et enfin dans la variable
d'environnement `BROWSERECO_FILTERS`.

Si aucune liste n'est trouvée, un avertissement est journalisé et le compteur
« Filtres : 0 » de la barre d'état le signale.