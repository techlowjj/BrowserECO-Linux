# BrowserECO-Linux — DataSaver Browser (Qt6 C++)

Navigateur portable Linux orienté **économie de données** et **vie privée**.
Version **1.3** — exception d'images par site, robustesse, blocage adverts
conforme, build et tests propres.

> L'original (version 1.0) est conservé tel quel dans
> `BACKUP-original-20260929-113846/` : sources, binaire et réglages.

---

## Ce que la 1.3 apporte

### Blocage contournable / mal interprété

| Symptôme | Cause corrigée |
|---|---|
| `…/ads.js?a=.pdf` passait la liste de blocage | l'extension était cherchée dans l'**URL entière** : elle est désormais testée sur `url.path()` seulement |
| 74 règles ALLOW (`-règle`) **bloquaient** au contraire | le marqueur `-` n'était pas géré : ces lignes sont ignorées |
| 24 019 règles `##` masquage d'élément bloquaient des URL | `##`, `#@#` sont ignorées (elles ne sont jamais réseau) |
| 1 059 règles `$domain=…` appliquées **partout** | elles sont ignorées : appliquées globalement, elles bloquaient des sites sans rapport |
| Une exception `@@` par chemin désactivait tout un domaine | seules les exceptions « hôte seul » sont prises en compte |

### Robustesse

- `shouldBlock()` : **1 900 µs → 9 µs** par requête (mesuré, thread IO de
  QtWebEngine, vraies listes). Index par hôte exact + index des règles libres
  par jeton, plus de parcours linéaire de toutes les règles à chaque requête.
- Plus de `QRegularExpression` reconstruite à chaque requête (156 µs → 1 µs).
- La ligne d'état n'est plus écrasée 350 ms après le message : **une erreur de
  chargement ou de téléchargement restait invisible** (le timer réécrivait le
  texte au-dessus). Les messages temporaires ont maintenant la main.
- Compteurs interrogés sur événement et non toutes les 350 ms : c'est ce qui
  gonflait `history.db-wal` à 4 Mo pour 300 lignes. `journal_size_limit` +
  `wal_checkpoint(TRUNCATE)` à la fermeture, écritures en transaction.
- La croix de fermeture d'onglet suivait un **index figé** : après un
  déplacement d'onglet, elle fermait le mauvais onglet.
- `m_tabInfos` n'était jamais réordonné alors qu'il était indexé par position :
  la session était sauvegardée dans le désordre.
- `F6` ouvrait la page d'erreur de Chromium (`setUrl("about:home")` au lieu du
  `setHtml` utilisé partout ailleurs) ; le bouton « Recharger » affichait
  l'icône « Arrêter » mais ne faisait que recharger.
- Crash du renderer : aucun gestionnaire → un onglet blanc **définitivement**.
  Une page d'erreur avec « Réessayer » s'affiche maintenant.
- Téléchargements : `deleteLater()` sur l'objet émetteur depuis son propre
  signal, et boîte de dialogue modale imbriquée dans le handler (source du
  SIGSEGV historique). Résolu par `QPointer` et exécution différée.
- Mode privé : il ne désactivait que les cookies persistants — le profil Qt
  continuait d'écrire `localStorage`/cache sur le disque. Le profil est
  maintenant **hors disque**.
- Écran null (QPA headless, machine sans sortie) : `primaryScreen()->…` sans
  test → démarrage en crash.
- Session restaurée : liste blanche de schémas (`javascript:`, `file:`,
  `data:` sont ignorés) et limite de 12 onglets.
- Recherche dans la page, zoom et réglages : une écriture de fichier par cran de
  molette → écriture groupée. Filtrage de l'historique groupé (250 ms).
- Suggestions réseau : délai maximal (8 s), statut HTTP vérifié, et les
  résultats sont rattachés à la frappe **qui a déclenché la requête** (aupour-
  vant, une frappe rapide affichait les résultats de la requête précédente).
- `SearXNG` / endpoint de suggestions : `javascript:` et autres schémas refusés.
- Hygiène du dépôt : `.gitignore` (profil QtWebEngine, bases SQLite, backups,
  exports, filtres), `settings.txt` réel et `WebEngineProfile/` exclus du
  dépôt, `settings.txt.exemple` vierge (le fichier réel contenait une URL de
  session privée), `LICENCE` GPL-3.0, attribution des listes de filtres,
  CI GitHub Actions, `.deb` et métadonnées AppStream.

### Images : exception par site (palier A)

« Images OFF » est le réglage le plus rentable — mesuré sur une photo 1600×1067
(308 Ko) : 182 Ko en 1280 px, 76 Ko en 800 px, 29 Ko en 480 px, soit **−41 % à
−91 %**. Mais il rend les sites photos, recettes ou documentation illisibles.

Le clic droit propose donc **« Images de ce site »** (entrée cochable qui reflète
l'état réel), et l'autorisation est mémorisée :

- la décision porte sur **le site visité**, pas sur l'hôte de l'image : les
  images viennent presque toujours d'un CDN (`i.ytimg.com`, `cloudfront`…) ;
- la correspondance se fait sur les **frontières de label** : `example.com`
  couvre `www.example.com` et `cdn.example.com`, mais **pas** `notexample.com` ;
- le réglage est persisté dans `settings.txt` (`imageAllow=`) ; une entrée
  contenant un `/` ou sans point est ignorée à la lecture ;
- la page est rechargée après bascule, sinon les images resteraient absentes de
  l'écran et le réglage semblerait sans effet ;
- la barre d'état indique le nombre d'exceptions : `Eco ON +NoImg/2` ;
- remise à zéro : Menu → *Images autorisées : tout réinitialiser* (entrée
  présente seulement s'il existe au moins une exception).

Le curseur « qualité des images » reste, lui, sans effet tant que la
compression n'est pas branchée (voir *Limite connue*).

### Ergonomie

- **Menu contextuel au clic droit** : il n'y en avait aucun. Ouverture d'un
  lien ou d'une image dans un onglet (avant ou de fond), copie du lien / de
  l'image / de la sélection, recherche de la sélection avec le moteur courant,
  recharger ou arrêter, zoom, code source, inspecteur, réglages Data saver.
  Les entrées n'apparaissent que si elles ont du sens (pas de menu à moitié
  gris), le tout en français — le menu par défaut de Qt est en anglais et
  QWebEngineView ne le remplace pas de façon fiable.
- **Bouton bouclier enfin utile** : il était `setEnabled(false)` depuis sa
  création, donc un contrôle mort dans la barre d'outils et dans le parcours
  de tabulation. Il bascule maintenant le mode économie de données, avec une
  icône qui reflète l'état.
- Infobulles corrigées : elles annonçaient « Alt+clic = nouvel onglet », ce que
  le code ne fait pas (`BrowserPage::createWindow` renvoie volontairement
  l'onglet courant).

## Résumé des changements

### 1. Moteur de recherche : plus précis, toujours léger

Avant : un seul moteur, Mojeek (~5,5 Ko de HTML, mais un index indépendant
très réduit → beaucoup de résultats manqués).

Maintenant : **9 moteurs au choix**, chacun avec son poids réel mesuré, et une
chaîne de repli automatique quand un moteur sert un mur anti-bot.

| Moteur | Poids | JS | Précision |
|---|---|---|---|
| DuckDuckGo (HTML) | ~14 Ko | non | index Bing — **défaut** |
| SearXNG (méta) | ~10 Ko | non | agrège Google/Bing/DDG — la plus précise |
| Mojeek | ~6 Ko | non | index indépendant, réduit |
| Wiby | ~4 Ko | non | web indie |
| Marginalia | ~40 Ko | non | index niche |
| Startpage | ~22 Ko | oui | résultats Google |
| Yep | ~20 Ko | oui | index communautaire |
| Brave Search | ~300 Ko | oui | très précise, coûteuse |
| Google | ~500 Ko | oui | très coûteuse |

**La règle eco tient** : le repli automatique ne bascule **jamais** vers un moteur
qui nécessite du JavaScript. Un test le vérifie.

Changer de moteur : `Alt+1` … `Alt+9`, le badge à droite de la barre d'adresse,
ou le panneau Eco.

#### Suggestions

Deux sources, fusionnées :

- **Historique local** : 0 octet, instantané.
- **API DuckDuckGo** : **~200 octets** par frappe pour 8 propositions,
  sans JS ni traçage. Désactivable (panneau Eco → *Service de suggestions*).

Le debounce est de 170 ms et chaque réponse obsolète est ignorée : taper vite
n'attrape pas de rafale de requêtes.

#### Bangs et raccourcis de sites

Résolus **localement** : `!w`, `!so`, `!gh`, `!mdn`, `!yt`, `!wp`, `!npm`…
Un site direct coûte une requête au lieu d'une page de résultats complète.

```
!w linux kernel        → fr.wikipedia.org  (1 requête)
so lambda              → stackoverflow.com  (1 requête)
gh qt6                 → github.com         (1 requête)
!sxiu test             → moteur (typo gérée : le "!" est retiré)
```

#### Repli anti-bot

Certains moteurs répondent `200`/`202` avec une page de vérification au lieu
des résultats. Sans détection, la recherche *paraît* marcher mais affiche du
vide. Un script JS de ~700 octets inspecte la page chargée ; si aucun résultat
n'est trouvé, le navigateur bascule sur le moteur suivant et l'indique dans la
barre d'état. Le moteur qui répond est alors **adopté** : le badge change et les
recherches suivantes l'utilisent, au lieu d'annoncer « DDG » en affichant du
SearXNG.

#### Challenge anti-bot : l'image du captcha doit s'afficher

Quand un moteur sert une page de vérification, **le blocage d'images est levé**
sur cette page. Sans cela, l'utilisateur voit « challenge » mais ne peut pas le
résoudre : l'image du captcha est bloquée par le réglage « Images OFF » et le
navigateur devient inutilisable. C'est le seul cas où l'économie de données
passe au second plan.

Si les moteurs légers sont tous bloqués, le navigateur ne boucle plus : il
laisse la vérification affichée (maintenant résolvable) et l'indique.

### 2. Panneau du haut

```
[←][→][⟳][⌂]  [🔒 Rechercher ou saisir une URL      ]  [🔍DDG ▾]  [🛡]  [Eco]  [☰]
 ─────────────────────────────────────────────────────────────────────────────────
  [🔍 Nouvel onglet ×][🔍 second onglet ×][+]           ← barre d'onglets
 ─────────────────────────────────────────────────────────────────────────────────
 Filtres : 114310 · Cache : 0 · Historique : 3          Eco ON · 47 bloqués · ≈2,1Mo  Zoom 100 %
```

Ce qui a changé :

- **Barre d'adresse** : icône de sécurité (cadenas HTTPS / avertissement HTTP),
  favicon de l'onglet, badge du moteur cliquable.
- **Panneau Eco** (`Ctrl+E`) : les 4 réglages et le curseur de qualité qui
  squashaient la barre sont regroupés. Le panneau affiche aussi les compteurs
  par catégorie (pubs, trackers, polices, médias, images, préchargement,
  streaming) et l'économie estimée.
- **Icônes vectorielles** dessinées à la volée (`src/ui/icons.cpp`) : nettes à
  toute taille, colorées selon le thème, 0 octet téléchargé, aucun emoji.
- **Croix de fermeture** dessinée (l'icône par défaut de Fusion était un pavé
  rouge illisible).
- **Bouton `+`** en fin de barre d'onglets, croix qui suit l'onglet déplacé.
- **Suggestions** : liste déroulante avec historique local, sélection au clavier
  (↑/↓/Tab), repli automatique à la perte de focus ou au changement d'onglet.
- **Menu hamburger** et **raccourcis** (`F1`) : un seul générateur, testé contre
  les `QShortcut` réels — impossible d'annoncer un raccourci inexistant.

### 3. Corrections trouvées en chemin

Elles n'étaient pas visibles avant les tests :

| Symptôme | Cause |
|---|---|
| Plantage après 1 frappe dans la barre d'adresse | `deleteLater()` appelé deux fois sur le même `QNetworkReply` |
| Page d'accueil illisible | `{{` littéral : double échappement au lieu d'un jeton `{...}` |
| `localhost:8080` traité comme un schéma | `QUrl` lit `localhost` comme scheme |
| `%` mangés dans le CSS de l'accueil | `.arg()` interpreting les `%` du gradient |
| Doublon `deleteLater` sur la popup | collision popup ↔ `QListWidget` |
| Statistiques décalées dans « À propos » | `%1` utilisé deux fois dans le même `arg()` |
| `Ctrl+D` annoncé mais inexistant | raccourci retiré, liste régénérée depuis le code |
| 10 moteurs pour 9 raccourcis | le 10ᵉ était inaccessible au clavier |
| SIGSEGV à la fermeture | pages détruites après le profil QtWebEngine |
| « Menu → Téléchargements » semble figé | bouton **Fermer** sans connexion, `QMenu` encore vivant pendant le dialogue modal, raccourcis dupliqués menu + fenêtre |
| Challenge anti-bot à chaque recherche | images du captcha bloquées par « Images OFF » → vérification impossible à résoudre ; badge moteur resté sur l'ancien moteur alors que le repli en changeait |

---

## Installation

### Prérequis

```bash
sudo apt install qt6-base-dev qt6-webengine-dev cmake g++
```

**Qt 6.2 minimum** (validé par la CI sur Qt 6.2.4 / Ubuntu 22.04 et 6.4.2 /
Ubuntu 24.04 ; testé en développement sur Qt 6.8). Les rares API plus récentes
— le délai maximal d'une requête réseau, disponible depuis Qt 6.8 — sont
protégées par `#if QT_VERSION`, pas par une exigence de version qui ferait
échouer la configuration sur une distribution ancienne.

### Build

```bash
cd ~/Documents/BrowserECO-Linux
cmake -B build          # Release par défaut si rien n'est précisé
cmake --build build -j$(nproc)
./build/BrowserECO
```

Ou en une passe, build + tests + installation portable :

```bash
./INSTALL.sh                              # installe dans le dossier du projet
./INSTALL.sh --dest /chemin/portable      # ailleurs
./INSTALL.sh --no-tests
./UNINSTALL.sh /chemin/portable           # ne supprime que ce qui a été installé
```

### Ligne de commande

```bash
./build/BrowserECO --version
./build/BrowserECO --help
./build/BrowserECO --private              # profil hors disque, rien n'est écrit
./build/BrowserECO --data-dir ~/Beco     # réglages et bases dans un dossier choisi
./build/BrowserECO https://exemple.fr    # ouvre l'URL au démarrage
```

Une seule instance par dossier de profil : un second lancement s'arrête avec un
message au lieu de se disputer `settings.txt` et les bases SQLite avec le
premier.

### Tests

```bash
./run_tests.sh                              # build + toute la suite
ctest --test-dir build --output-on-failure   # tests seuls (déjà construits)
ctest --test-dir build -R test_adblock      # une suite
```

Les tests sont des cibles CMake (`tests/CMakeLists.txt`) : plus de linkage
manuel des `.o`. Chaque test a un **timeout** (120 s pour les tests purs, 300 s
pour ceux qui démarrent QtWebEngine) et s'exécute dans son propre dossier vide,
recréé à chaque exécution — aucun test ne peut voir un `settings.txt`, une base
ou un profil réel, ni les réglages laissés par un autre test.

**12 suites, exécution headless** (`QT_QPA_PLATFORM=offscreen`) :

| Suite | Couverture |
|---|---|
| `test_adblock` | sémantique ABP (règles `-`, `##`, `$domain=`, `@@`, ancres), absence de bypass par query string, **performance** (µs par requête sur les vraies listes) |
| `test_searchengine` | résolution URL/recherche, bangs, repli, moteurs JS écartés, refus des URL `javascript:` |
| `test_settings` | ancien format (5 lignes) → `clé=valeur`, bornes, fichiers corrompus |
| `test_history` | jokers `LIKE` échappés, suppression, effacement |
| `test_homepage` | aucun placeholder résiduel, CSS intact |
| `test_icons` | chaque icône dessine bien des pixels |
| `test_eco_stats` | cohérence et bornage des compteurs d'économie |
| `test_images` | exceptions d'images par site : frontières de label, sous-domaines, faux positifs (`notexample.com`), persistance, idempotence |
| `serp_test` | repli anti-challenge, détection de mur, exemption d'images sur les pages de vérification |
| `freeze_test` | anti-gel : Menu → Téléchargements, thread témoin |
| `ui_test` | **21 tests d'interface** (dont menu contextuel et entrée d'exception d'images) : barre d'adresse, suggestions, panneau Eco, historique, raccourcis, onglets, zoom, raccourcis annoncés = réels, menu contextuel, bouton Eco |

---

## Développement

Les données de développement ne sont **jamais** dans l'arbre source : elles
vont dans `~/.local/share/BrowserECO/dev`. On peut donc supprimer `build/`,
reconfigurer ou faire n'importe quelle manip dans le dépôt sans toucher à un
profil de navigation.

```bash
./dev.sh                 # compile si besoin et lance l'application (données isolées)
./dev.sh test            # compile + 12 suites de tests
./dev.sh check           # compilation propre (0 warning) + tests + .desktop + version
./dev.sh install [préf.] # installe (par défaut ~/Applications/BrowserECO-dev)
./dev.sh clean           # supprime build/
./dev.sh purge-data      # supprime les données de dev (demande confirmation)
```

### Deux installations, deux profils

| | Copie personnelle | Développement |
|---|---|---|
| Version | figée sur un tag | en cours |
| Installation | `~/Applications/BrowserECO-<version>` | `./dev.sh install` |
| Données | `~/.local/share/BrowserECO/perso` | `~/.local/share/BrowserECO/dev` |

Le dossier de données est choisi par `--data-dir` (ou `BROWSERECO_DATA_DIR`) :
on peut donc mettre le binaire sur une clé USB ou une partition NTFS **sans
risque**, les bases SQLite (WAL) restant sur un disque ext4.

### Règles de contribution

- `settings.txt`, les bases, `WebEngineProfile/` et les données de navigation
  ne doivent jamais être committés (`.gitignore` s'en charge) ;
- toute correction de bug s'accompagne d'un test dans `tests/` ;
- le format de `settings.txt` est centralisé dans `SettingsStore`, testé par
  `test_settings` — ne pas dupliquer la logique de lecture ailleurs ;
- `./dev.sh check` doit sortir « TOUT EST VERT » avant un commit ;
- version : une seule source, `CMakeLists.txt` ; les tags Git marquent les versions.

---

## Structure

```
BrowserECO-Linux/
├── CMakeLists.txt                  ← cible unique, warnings, tests, install, CPack
├── LICENCE                         ← GPL-3.0 (texte intégral)
├── run_tests.sh                    ← build + ctest
├── dev.sh                          ← point d'entrée du développement (run/test/check/install)
├── INSTALL.sh / UNINSTALL.sh       ← installation portable (+ .deb via cpack)
├── settings.txt.exemple            ← modèle de réglages (sans session)
├── BrowserECO.desktop.in           ← modèle d'entrée de menu (généré à l'install)
├── BrowserECO.desktop.example      ← idem pour un déploiement portable manuel
├── cmake/                          ← version.h.in, install_desktop.cmake.in
├── packaging/                      ← AppStream (métadonnées .deb)
├── src/
│   ├── main.cpp
│   ├── mainwindow.{h,cpp,ui}       ← barre d'adresse, barre d'onglets, menu
│   ├── browserpage.{h,cpp}         ← liens internes browseeco://
│   ├── ecointerceptor.{h,cpp}      ← blocage + compteurs par catégorie
│   ├── services/
│   │   ├── searchengine.{h,cpp}    ← 9 moteurs, bangs, suggestions
│   │   ├── serpguard.{h,cpp}       ← détection des murs anti-bot
│   │   ├── settingsstore.{h,cpp}   ← lecture/écriture atomique de settings.txt
│   │   ├── adblocker.{h,cpp}
│   │   ├── cachemanager.{h,cpp}
│   │   ├── historymanager.{h,cpp}
│   │   └── imageoptimizer.{h,cpp}
│   └── ui/
│       ├── omnibox.{h,cpp}         ← barre d'adresse + suggestions
│       ├── ecopanel.{h,cpp}        ← panneau Data saver
│       └── icons.{h,cpp}           ← icônes vectorielles (QPainter)
├── tests/                          ← 11 tests + CMakeLists.txt (CTest)
├── Filters/                        ← EasyList + EasyPrivacy (~116 000 règles)
│                                    + LICENSE/README (attribution obligatoire)
└── .github/workflows/ci.yml        ← build, tests, .desktop, .deb
```

La version (1.2.0) est écrite **une seule fois**, dans `CMakeLists.txt` :
elle est générée dans `browsereco_version.h` et utilisée par `--version`, la
fenêtre « À propos » et le User-Agent. Plus de « 1.0.0 » oublié dans `main.cpp`
pendant que le README annonçait 1.1.

---

## Réglages (`settings.txt`, à côté du binaire)

```ini
dataSaver=1          # en-tête Save-Data + bloqueurs
imagesOff=1          # pas d'images
ultraEco=0           # + streaming/favicons bloqués
favicons=1           # sauf en mode Ultra
quality=65           # qualité WebP 0-85 (0 = pas de compression)
zoom=1.00
engine=ddg           # ddg | searx | mojeek | wiby | marginalia | startpage | yep | brave | google
searxUrl=https://searx.be/search
providerUrl=https://duckduckgo.com/ac/
autoFallback=1       # bascule si mur anti-bot
remoteSuggest=1      # suggestions réseau (~200 o)
restoreSession=0
imageAllow=          # sites dont les images sont autorisées (clic droit)
session=             # écrit automatiquement à la sortie (URLs séparées par « | »)
```

Priorité du dossier de profil : `--data-dir` > variable `BROWSERECO_DATA_DIR`
> dossier du binaire (si inscriptible) > `~/.local/share/BrowserECO`.

L'ancien format (5 lignes, qualité seule en 2ᵉ ligne) est lu sans erreur :
le test `test_settings` couvre ce cas.

---

## Raccourcis

| Navigation | |
|---|---|
| `Ctrl+T` / `Ctrl+W` | nouvel onglet / fermer |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | onglet suivant / précédent |
| `Ctrl+L` | barre d'adresse |
| `Alt+←` / `Alt+→` | retour / avancer |
| `F5` / `Ctrl+Shift+R` | recharger / recharger sans cache |
| `Échap` | arrêter le chargement, fermer les suggestions |
| `Ctrl+F` | rechercher dans la page |
| `F6` / `F11` | accueil / plein écran |
| `Ctrl+molette`, `Ctrl+0`, `Ctrl+=`, `Ctrl+-` | zoom |

| Économie de données | |
|---|---|
| `Ctrl+E` | panneau Data saver |
| `Alt+1` … `Alt+9` | changer de moteur |
| `Ctrl+H` / `Ctrl+J` | historique / téléchargements |
| `F1` | raccourcis |

---

## Portabilité

- `settings.txt`, `cache.db`, `history.db` et `WebEngineProfile/` à côté du
  binaire : aucun écrit dans `~` si le dossier est inscriptible, repli
  automatique sur `~/.local/share/BrowserECO` sinon.
- `--private` : profil **hors disque** (aucun `WebEngineProfile`, aucun
  localStorage, aucun cache écrit), cache mémoire 20 Mo, historique non écrit.
- Dépend des libs Qt6 système, aucun bundling.

```bash
# Menu d'applications (installation système : l'entrée est générée avec le
# bon préfixe, y compris Exec= et Icon=)
sudo cmake --install build
update-desktop-database /usr/share/applications
```

### Paquet .deb

```bash
cmake -S . -B build-deb -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr -DBROWSERECO_DEB_LAYOUT=ON \
      -DBROWSERECO_BUILD_TESTS=OFF
cmake --build build-deb -j$(nproc)
cpack --config build-deb/CPackConfig.cmake -G DEB
```

## Licences

- **Code** : GPL-3.0 — texte intégral dans [`LICENCE`](LICENCE).
- **Listes de filtres** : EasyList / EasyPrivacy (tiers, GPLv3 ou CC BY-SA 3.0)
  — attribution et conditions dans [`Filters/LICENSE`](Filters/LICENSE) et
  [`Filters/README.md`](Filters/README.md). Elles ne sont pas incluses dans le
  dépôt (3,5 Mo) ; sans elles l'application démarre et l'indique
  (« Filtres : 0 »).

## Limite connue

Le curseur « qualité des images » enregistre le réglage `quality=` mais la
compression WebP n'est pas encore branchée : `ImageOptimizer` n'a pas de
`QWebEngineUrlSchemeHandler` qui l'appellerait. Le réglage est donc pour l'instant
sans effet sur le trafic : c'est la seule fonction annoncée qui ne fait rien.
