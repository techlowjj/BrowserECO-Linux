#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QHash>

/*
 * SearchEngine — moteur de recherche multi-sources pour DataSaver Browser.
 *
 * Objectif : plus PRECIS que l'ancien Mojeek unique, sans exploser la data.
 *  - Chaque moteur declare son poids HTML et son besoin de JS.
 *  - Chaine de repli automatique si un moteur renvoie un mur anti-bot.
 *  - Suggestions : historique local (0 octet) + API DuckDuckGo (~100 octets).
 *  - Bangs / raccourcis de sites : !w wikipedia, !so stackoverflow, !gh github...
 */

struct SearchEngine {
    QString id;            // identifiant stable dans settings.txt
    QString label;         // nom affiche
    QString urlTemplate;   // contient {q} et {lang}
    QString host;          // pour la detection anti-bot / SERP guard
    QString weightNote;    // "~14 Ko HTML - 0 JS"
    QString shortLabel;    // badge dans la barre d'adresse
    bool    needsJs = false;   // moteur qui ne s'affiche pas sans JS
    bool    precise = true;    // index large (Bing/Google derriere)
    bool    fallbackCapable = true;
};

class SearchEngineManager : public QObject {
    Q_OBJECT
public:
    explicit SearchEngineManager(QObject *parent = nullptr);

    static const QVector<SearchEngine> &registry();
    static const SearchEngine *byId(const QString &id);
    static QString defaultEngineId();
    // Moteur correspondant a une URL de resultats (pour le repli anti-bot)
    static QString engineForHost(const QUrl &url);

    QString engineId() const { return m_engineId; }
    void setEngineId(const QString &id);

    const SearchEngine *current() const { return byId(m_engineId); }

    // Instance SearXNG personnalisable (menu "Mon SearXNG")
    QString searxUrl() const { return m_searxUrl; }
    void setSearxUrl(const QString &url);

    // Mode "Auto" : bascule sur le moteur suivant si un mur anti-bot est detecte
    bool autoFallback() const { return m_autoFallback; }
    void setAutoFallback(bool on) { m_autoFallback = on; }

    // Suggestions distantes (~100 octets). Desactivees si pas de reseau.
    bool remoteSuggestions() const { return m_remoteSuggestions; }
    void setRemoteSuggestions(bool on) { m_remoteSuggestions = on; }

    // providerUrl() = endpoint utilise pour les suggestions
    QString providerUrl() const { return m_providerUrl; }
    void setProviderUrl(const QString &url);

    // Analyse une saisie : URL, ou recherche, ou bang.
    // Retourne l'URL a charger. *usedEngineInId recoit l'id du moteur choisi.
    QUrl resolve(const QString &input, QString *usedEngineId = nullptr) const;

    // Detecte un bang "!w foo" -> (engineId, "foo")
    static bool parseBang(const QString &input, QString *engineId, QString *query);

    // Detecte un prefixe court "w foo" / "so foo" -> (engineId, "foo")
    static bool parsePrefix(const QString &input, QString *engineId, QString *query);

    // Regles de recherche ajoutees automatiquement (host: + langue).
    QUrl buildUrl(const SearchEngine &engine, const QString &query) const;

    // Chaine de repli apres un echec : liste d'id dans l'ordre.
    QStringList fallbackChain(const QString &engineId) const;

    // Suggestions
    void requestSuggestions(const QString &text, quint64 token);
    void cancelSuggestions();

signals:
    void suggestionsReady(const QString &text, const QStringList &items, quint64 token);

private slots:
    void onDebounce();

private:
    void onSuggestionsReply(QNetworkReply *reply, const QString &text, quint64 token);

private:
    QString m_engineId;
    QString m_searxUrl;
    bool m_autoFallback = true;
    bool m_remoteSuggestions = true;
    QString m_providerUrl = QStringLiteral("https://duckduckgo.com/ac/");
    quint64 m_token = 0;
    QString m_pendingText;
    QNetworkAccessManager m_nam;
    QTimer m_debounce;
    QNetworkReply *m_reply = nullptr;
    QHash<QString, QString> m_langCache;
};
