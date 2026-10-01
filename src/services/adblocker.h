#pragma once
#include <QString>
#include <QHash>
#include <QList>
#include <QSet>
#include <QMutex>

/*
 * AdBlocker — listes de filtres ABP (EasyList / EasyPrivacy).
 *
 * Version 2 : parsing ABP correct + index par hote exact.
 *
 *  - Seules les regles de type reseau sont prises en compte. Sont ignorees :
 *      ! et [        commentaires
 *      #, ##, #@#    regles cosmetiques (masquage d'elements, jamais du reseau)
 *      -             regles ALLOW / desactivees (les inverser en BLOCK etait
 *                    une faute grave : 74 regles de la liste transformaient
 *                    « autoriser » en « bloquer »)
 *      /.../ $       regles regex (non supportees)
 *      $domain=...   regles a portee : appliquees globalement, elles bloquaient
 *                    des sites entiers sans rapport
 *  - @@ : seules les exceptions « hote seul » sontprises en compte ; une
 *    exception limitee a un chemin est ignoree (elle ne peut pas etre
 *    respectee a l'echelle du domaine).
 *
 * Performances : shouldBlock() est appele par requete, sur le thread IO de
 * QtWebEngine (donc hors thread GUI). Il fait O(nb de labels) recherches dans
 * un QHash au lieu de parcourir lineairment TOUTES les regles (2 ms/requete
 * mesures, 170 ms pour une page de 100 requetes).
 */
class AdBlocker {
public:
    AdBlocker() = default;

    void loadFilters(const QString &directory);
    void loadFilterFile(const QString &filePath);
    bool shouldBlock(const QString &url) const;
    void clear();

    // Compteur fige au chargement : ne recalcule rien (appele par l'IHM).
    int ruleCount() const;
    int blockedHostCount() const;   // verrouille en interne
    bool hasRules() const { return ruleCount() > 0; }

private:
    /* Une regle de chemin :
       anchored  = "||example.com/banner" -> la commence par "://example.com/banner"
       loose     = "/ads/banner.gif"     -> simple sous-chaine (longueur mini 4) */
    struct PathRule {
        bool anchored = false;
        QString pattern;
    };

    static QString domainRoot(const QString &host);

    // Charge les lignes dans des conteneurs locaux puis les publie sous un seul
    // verrou (avant : un lock/unlock par ligne, 138 000 fois).
    void mergeParsed(QSet<QString> &hosts,
                     QSet<QString> &whitelist,
                     QHash<QString, QList<PathRule>> &pathRules,
                     QHash<QString, QList<PathRule>> &looseRules,
                     int &ruleCount);

    QSet<QString> m_blockedHosts;                       // hotes bloques
    QSet<QString> m_whitelist;                          // exceptions (hote seul)
    QHash<QString, QList<PathRule>> m_pathRules;        // hote exact -> regles
    // Regles « libres » (sous-chaine) indexees par jeton significatif :
    // sans cet index, chaque requete parcourait les milliers de sous-chaines
    // (600 µs mesurees, sur le thread IO de QtWebEngine).
    QHash<QString, QList<PathRule>> m_looseRules;       // jeton -> regles
    mutable QMutex m_mutex;
    int m_ruleCount = 0;
};