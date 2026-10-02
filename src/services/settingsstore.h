#pragma once
#include <QString>
#include <QStringList>

/*
 * SettingsStore — lecture / écriture de settings.txt.
 *
 * Isolé de MainWindow pour deux raisons :
 *  1. les tests s'exercent sur le VRAI code de production (ils recopiaient
 *     jusqu'ici la logique de lecture dans le test lui-même : ils passaient
 *     même si le vrai parseur était cassé) ;
 *  2. l'écriture est atomique (QSaveFile) : un crash ou une coupure pendant la
 *     sauvegarde ne peut pas laisser un settings.txt tronqué, seul point de
 *     corruption de réglages encore possible.
 *
 * Format : « clé=valeur », une ligne par réglage, « # » pour commenter.
 * L'ancien format (5 lignes, qualité seule en 2e ligne) est toujours lu.
 * Toute valeur hors bornes ou de type incorrect est ignorée silencieusement :
 * un fichier corrompu ne doit jamais empêcher l'application de démarrer.
 */

struct SettingsData {
    bool dataSaver = true;
    bool imagesOff = true;
    bool ultraEco = false;
    bool favicons = true;
    // Chargement d'images a la demande : placeholder cliquable au lieu de
    // telecharger chaque image. OFF par defaut (c'est un choix, pas une
    // surprise) et sans effet si « Images » est deja bloquees.
    bool lazyImages = false;
    int quality = 65;          // 0-85. 0 = compression des images désactivée.
                               // Source de vérité unique : pas de booléen
                               // séparé, qui finirait par diverger du curseur.
    int imageCacheMb = 32;     // 16-128. Mémoire du cache d'images compressées.
                               // Source de vérité unique, comme la qualité.
    double zoom = 1.0;         // 0.25-3.0
    QString engineId;
    QString searxUrl;
    QString providerUrl;
    bool autoFallback = true;
    bool remoteSuggestions = true;
    bool restoreSession = false;
    bool secGpc = true;           // signal « Global Privacy Control »
    QStringList session;            // URLs, dans l'ordre des onglets
    QStringList imageAllowedHosts;  // sites dont les images sont autorisees
};

namespace SettingsStore {

// Bornes centralisees (aussi utilisees par l'IHM pour borner les widgets).
constexpr int kQualityMin = 0;
constexpr int kQualityMax = 85;
constexpr double kZoomMin = 0.25;
constexpr double kZoomMax = 3.0;

// Lecture d'un fichier ; fichier absent ou illisible -> valeurs par defaut.
SettingsData load(const QString &path);

// Ecriture ATOMIQUE : renvoie false si l'ecriture a echoue (le fichier
// d'origine reste alors intact).
bool save(const QString &path, const SettingsData &data);

// Les deux etapes separees, pour etre testables sans fichier.
SettingsData parse(const QString &contents);
QString serialize(const SettingsData &data);

// Liste d'hotes « a.example.fr|b.example.fr ». Un entree contenant « / » ou
// depourvue de point est rejetee : ce n'est pas un nom de domaine, et le
// fichier est librement modifiable.
QStringList parseHostList(const QString &value);
QString joinHostList(const QStringList &hosts);

} // namespace SettingsStore