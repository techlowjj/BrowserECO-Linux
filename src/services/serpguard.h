#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantMap>

/*
 * SerpGuard — detecte les murs anti-bot sur les pages de resultats.
 *
 * Certains moteurs (DuckDuckGo, Mojeek, SearXNG) repondent 200/202 avec une page
 * "verification" ou une "anomaly" au lieu des resultats. Sans detection, la
 * recherche semble "marcher" mais affiche une page vide : tres frustrant.
 *
 * On injecte un script JS leger (~600 octets) apres chaque chargement et on
 * bascule automatiquement sur le moteur suivant si aucun resultat n'est trouve.
 */
class SerpGuard : public QObject {
    Q_OBJECT
public:
    explicit SerpGuard(QObject *parent = nullptr);

    // true si l'URL ressemble a une page de resultats de recherche
    static bool isSearchPage(const QUrl &url);
    // JS d'analyse execute dans la page (renvoie un objet)
    static QString probeScript();
    // Analyse le retour du script
    static bool looksBlocked(const QVariantMap &data);

    // Injecte le probe dans une vue (n'echantillonne que les pages de recherche)
    void probe(class QWebEngineView *view, const QUrl &url, const QString &engineId);

signals:
    // engineId = moteur bloque (MainWindow choisit le suivant)
    void blocked(const QString &engineId, const QString &nextEngineId);
    // engineId = moteur qui a reellement repondu avec des resultats
    void succeeded(const QString &engineId);

private:
};
