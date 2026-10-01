#include "searchengine.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStringList>
#include <QUrlQuery>
#include <QDebug>

namespace {

QString trim(const QString &s) { return s.trimmed(); }

// Heuristique "est-ce une URL ?" plus solide que l'ancien test (contenait '.')
bool looksLikeHost(const QString &s)
{
    if (s.isEmpty() || s.contains(QLatin1Char(' '))) return false;
    // localhost:port / IP:port
    static const QRegularExpression localRx(QString::fromLatin1(
        R"RX(^(localhost|127\.0\.0\.1|0\.0\.0\.0|\[::1\])(:\d+)?(/.*)?$)RX"),
        QRegularExpression::CaseInsensitiveOption);
    if (localRx.match(s).hasMatch()) return true;
    // IPv4
    static const QRegularExpression ipRx(QString::fromLatin1(
        R"RX(^(\d{1,3}\.){3}\d{1,3}(:\d+)?(/.*)?$)RX"));
    if (ipRx.match(s).hasMatch()) return true;
    // host.tld / sous.domaine.tld  (TLD >= 2 lettres)
    static const QRegularExpression hostRx(QString::fromLatin1(
        R"RX(^([a-z0-9]([a-z0-9\-_]*[a-z0-9])?\.)+[a-z]{2,24}(:\d+)?(/.*)?$)RX"),
        QRegularExpression::CaseInsensitiveOption);
    return hostRx.match(s).hasMatch();
}

QString pct(const QString &s)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(s));
}

} // namespace

// ---------------------------------------------------------------------------
// Registre des moteurs
// ---------------------------------------------------------------------------
const QVector<SearchEngine> &SearchEngineManager::registry()
{
    static const QVector<SearchEngine> engines = [] {
        QVector<SearchEngine> v;
        v.append({ QStringLiteral("ddg"),
                   QStringLiteral("DuckDuckGo (HTML léger)"),
                   QStringLiteral("https://html.duckduckgo.com/html/?q={q}&kl={kl}"),
                   QStringLiteral("html.duckduckgo.com"),
                   QStringLiteral("~14 Ko · 0 JS · index Bing"),
                   QStringLiteral("DDG"), false, true, true });

        v.append({ QStringLiteral("searx"),
                   QStringLiteral("SearXNG (méta, très précis)"),
                   QStringLiteral(""), // construit dynamiquement depuis m_searxUrl
                   QStringLiteral("searx"),
                   QStringLiteral("~10 Ko · 0 JS · agrège Google/Bing/DDG"),
                   QStringLiteral("SXR"), false, true, true });

        v.append({ QStringLiteral("mojeek"),
                   QStringLiteral("Mojeek (ultra-léger, index réduit)"),
                   QStringLiteral("https://www.mojeek.com/search?q={q}&t=10"),
                   QStringLiteral("www.mojeek.com"),
                   QStringLiteral("~6 Ko · 0 JS · index indépendant"),
                   QStringLiteral("MJK"), false, false, true });

        v.append({ QStringLiteral("wiby"),
                   QStringLiteral("Wiby (minimaliste)"),
                   QStringLiteral("https://wiby.me/?q={q}"),
                   QStringLiteral("wiby.me"),
                   QStringLiteral("~4 Ko · 0 JS · web indie"),
                   QStringLiteral("WBY"), false, false, true });

        v.append({ QStringLiteral("marginalia"),
                   QStringLiteral("Marginalia (web old-school)"),
                   QStringLiteral("https://search.marginalia.nu/search?query={q}"),
                   QStringLiteral("search.marginalia.nu"),
                   QStringLiteral("~40 Ko · index niche"),
                   QStringLiteral("MRG"), false, false, true });

        v.append({ QStringLiteral("startpage"),
                   QStringLiteral("Startpage (résultats Google)"),
                   QStringLiteral("https://www.startpage.com/sp/search?query={q}&language=francais"),
                   QStringLiteral("www.startpage.com"),
                   QStringLiteral("~22 Ko · résultats Google"),
                   QStringLiteral("SP"), true, true, false });

        v.append({ QStringLiteral("yep"),
                   QStringLiteral("Yep"),
                   QStringLiteral("https://yep.com/web?q={q}"),
                   QStringLiteral("yep.com"),
                   QStringLiteral("~20 Ko · index communitaire"),
                   QStringLiteral("YEP"), true, true, false });

        v.append({ QStringLiteral("brave"),
                   QStringLiteral("Brave Search (très précis, lourd)"),
                   QStringLiteral("https://search.brave.com/search?q={q}&source=web"),
                   QStringLiteral("search.brave.com"),
                   QStringLiteral("~300 Ko + JS · data mobile déconseillée"),
                   QStringLiteral("BRV"), true, true, false });

        v.append({ QStringLiteral("google"),
                   QStringLiteral("Google (précis, très lourd)"),
                   QStringLiteral("https://www.google.com/search?q={q}&num=20&hl=fr&filter=0"),
                   QStringLiteral("www.google.com"),
                   QStringLiteral("~500 Ko + JS · data mobile déconseillée"),
                   QStringLiteral("GGL"), true, true, false });
        return v;
    }();
    return engines;
}

const SearchEngine *SearchEngineManager::byId(const QString &id)
{
    for (const SearchEngine &e : registry())
        if (e.id == id) return &e;
    return nullptr;
}

QString SearchEngineManager::defaultEngineId()
{
    return QStringLiteral("ddg");
}

QString SearchEngineManager::engineForHost(const QUrl &url)
{
    const QString h = url.host();
    if (h.isEmpty()) return QString();
    if (h == QLatin1String("html.duckduckgo.com")) return QStringLiteral("ddg");
    if (h == QLatin1String("lite.duckduckgo.com")) return QStringLiteral("ddg");
    if (h == QLatin1String("duckduckgo.com")) return QStringLiteral("ddg");
    if (h.endsWith(QLatin1String("mojeek.com"))) return QStringLiteral("mojeek");
    if (h == QLatin1String("wiby.me")) return QStringLiteral("wiby");
    if (h == QLatin1String("search.marginalia.nu")) return QStringLiteral("marginalia");
    if (h == QLatin1String("www.startpage.com")) return QStringLiteral("startpage");
    if (h == QLatin1String("yep.com")) return QStringLiteral("yep");
    if (h == QLatin1String("search.brave.com")) return QStringLiteral("brave");
    if (h.endsWith(QLatin1String("google.com"))) return QStringLiteral("google");
    // SearXNG : instance par defaut ou host contenant "searx"
    if (h.contains(QLatin1String("searx"))) return QStringLiteral("searx");
    return QString();
}

SearchEngineManager::SearchEngineManager(QObject *parent)
    : QObject(parent)
    , m_engineId(defaultEngineId())
    , m_searxUrl(QStringLiteral("https://searx.be/search"))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(170);
    connect(&m_debounce, &QTimer::timeout, this, &SearchEngineManager::onDebounce);
}

void SearchEngineManager::setEngineId(const QString &id)
{
    if (byId(id) && id != m_engineId) {
        m_engineId = id;
        emit suggestionsReady(QString(), QStringList(), 0); // vide la liste
    }
}

void SearchEngineManager::setProviderUrl(const QString &url)
{
    QString u = url.trimmed();
    if (u.isEmpty()) return;
    if (!u.contains(QLatin1String("://"))) u = QStringLiteral("https://") + u;
    // Seuls http/https sont acceptes : sans ce controle, « javascript:alert(1) »
    // devenait une URL « https://javascript:alert(1)/ ».
    const QUrl parsed(u);
    const QString scheme = parsed.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        qWarning("SearchEngine: endpoint de suggestions refuse (schema %s)", qPrintable(scheme));
        return;
    }
    if (!u.endsWith(QLatin1Char('/'))) u += QLatin1Char('/');
    m_providerUrl = u;
}

void SearchEngineManager::setSearxUrl(const QString &url)
{
    QString u = url.trimmed();
    if (u.isEmpty()) return;
    if (!u.contains(QLatin1String("://"))) u = QStringLiteral("https://") + u;
    const QUrl parsed(u);
    const QString scheme = parsed.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        qWarning("SearchEngine: instance SearXNG refusee (schema %s)", qPrintable(scheme));
        return;
    }
    if (!parsed.isValid() || parsed.host().isEmpty()) {
        qWarning() << QStringLiteral("SearchEngine: instance SearXNG invalide :") << url;
        return;
    }
    m_searxUrl = u;
}

// ---------------------------------------------------------------------------
// Bangs et prefixes courts
// ---------------------------------------------------------------------------
namespace {
struct BangRule { const char *token; const char *engineId; };

const QVector<BangRule> &bangRules()
{
    static const QVector<BangRule> rules = {
        { "!ddg", "ddg" },        { "!duckduckgo", "ddg" },
        { "!sx",   "searx" },      { "!searxng", "searx" },
        { "!m",    "mojeek" },     { "!mojeek", "mojeek" },
        { "!wiby", "wiby" },
        { "!mrg",  "marginalia" }, { "!sp", "startpage" },
        { "!yep",  "yep" },
        { "!brave","brave" },      { "!b", "brave" },
        { "!g",    "google" },     { "!google", "google" },
        { "!w",    "wikipedia" },  { "!wiki", "wikipedia" }, { "!wp", "wikipedia" },
        { "!so",   "stackoverflow" }, { "!stack", "stackoverflow" },
        { "!gh",   "github" },
        { "!mdn",  "mdn" },        { "!npm", "npm" }, { "!pypi", "pypi" },
        { "!yt",   "youtube" },    { "!maps", "maps" },
        { "!d",    "ddg" },        { "!wiki", "wikipedia" },
        { "!imdb", "imdb" },
    };
    return rules;
}

const QVector<BangRule> &prefixRules()
{
    static const QVector<BangRule> rules = {
        { "ddg", "ddg" }, { "duckduckgo", "ddg" }, { "sx", "searx" },
        { "so", "stackoverflow" }, { "gh", "github" }, { "wiki", "wikipedia" },
        { "w", "wikipedia" }, { "yt", "youtube" }, { "mdn", "mdn" },
        { "npm", "npm" }, { "pypi", "pypi" }, { "g", "google" },
        { "b", "brave" }, { "m", "mojeek" }, { "sp", "startpage" },
    };
    return rules;
}

// URL directe pour les bangs "site"
QString siteUrl(const QString &id, const QString &q)
{
    if (q.isEmpty()) return QString();
    if (id == QLatin1String("wikipedia")) {
        return QStringLiteral("https://fr.wikipedia.org/w/index.php?search=") + pct(q);
    }
    if (id == QLatin1String("stackoverflow")) {
        return QStringLiteral("https://stackoverflow.com/search?q=") + pct(q);
    }
    if (id == QLatin1String("github")) {
        return QStringLiteral("https://github.com/search?q=") + pct(q) + QStringLiteral("&type=repositories");
    }
    if (id == QLatin1String("mdn")) {
        return QStringLiteral("https://developer.mozilla.org/fr/search?q=") + pct(q);
    }
    if (id == QLatin1String("npm")) {
        return QStringLiteral("https://www.npmjs.com/search?q=") + pct(q);
    }
    if (id == QLatin1String("pypi")) {
        return QStringLiteral("https://pypi.org/search/?q=") + pct(q);
    }
    if (id == QLatin1String("youtube")) {
        return QStringLiteral("https://m.youtube.com/results?search_query=") + pct(q);
    }
    if (id == QLatin1String("maps")) {
        return QStringLiteral("https://www.openstreetmap.org/search?query=") + pct(q);
    }
    if (id == QLatin1String("imdb")) {
        return QStringLiteral("https://www.imdb.com/find/?q=") + pct(q);
    }
    return QString();
}
} // namespace

bool SearchEngineManager::parseBang(const QString &input, QString *engineId, QString *query)
{
    const QString s = trim(input);
    if (!s.startsWith(QLatin1Char('!'))) return false;
    const int sp = s.indexOf(QLatin1Char(' '));
    const QString tok = (sp < 0) ? s : s.left(sp);
    const QString rest = (sp < 0) ? QString() : trim(s.mid(sp + 1));
    for (const BangRule &r : bangRules()) {
        if (tok.compare(QLatin1String(r.token), Qt::CaseInsensitive) == 0) {
            if (engineId) *engineId = QString::fromLatin1(r.engineId);
            if (query) *query = rest;
            return true;
        }
    }
    return false;
}

bool SearchEngineManager::parsePrefix(const QString &input, QString *engineId, QString *query)
{
    const QString s = trim(input);
    const int sp = s.indexOf(QLatin1Char(' '));
    if (sp <= 0) return false;
    const QString tok = s.left(sp).toLower();
    const QString rest = trim(s.mid(sp + 1));
    if (rest.isEmpty()) return false;
    // Ne pas capturer une phrase normale ("le chat dort" -> "le" n'est pas un prefixe)
    for (const BangRule &r : prefixRules()) {
        if (tok == QLatin1String(r.token)) {
            if (engineId) *engineId = QString::fromLatin1(r.engineId);
            if (query) *query = rest;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Construction d'URL
// ---------------------------------------------------------------------------
QUrl SearchEngineManager::buildUrl(const SearchEngine &engine, const QString &query) const
{
    const QString q = trim(query);
    if (q.isEmpty()) return QUrl();

    if (engine.id == QLatin1String("searx")) {
        // Construction par QUrl/QUrlQuery : une instance dont l'URL contient
        // deja « ?q=... » ne doit pas devenir « ?q=preset?q=recherche ».
        QUrl base(m_searxUrl);
        QUrlQuery params(base);
        params.removeQueryItem(QStringLiteral("q"));
        params.addQueryItem(QStringLiteral("q"), q);
        params.addQueryItem(QStringLiteral("language"), QStringLiteral("fr-FR"));
        params.addQueryItem(QStringLiteral("safesearch"), QStringLiteral("0"));
        base.setQuery(params);
        return base;
    }
    QString tpl = engine.urlTemplate;
    tpl.replace(QStringLiteral("{q}"), pct(q));
    tpl.replace(QStringLiteral("{kl}"), QStringLiteral("fr-fr"));
    tpl.replace(QStringLiteral("{lang}"), QStringLiteral("fr"));
    return QUrl(tpl);
}

QStringList SearchEngineManager::fallbackChain(const QString &engineId) const
{
    // Ordre : meme famille -> meta -> leger. evite les moteurs JS (data mobile).
    QStringList out;
    const SearchEngine *e = byId(engineId);
    if (!e) { out << defaultEngineId(); e = byId(defaultEngineId()); }
    if (e->id == QLatin1String("ddg")) out << QStringLiteral("searx") << QStringLiteral("mojeek") << QStringLiteral("wiby");
    if (e->id == QLatin1String("searx")) out << QStringLiteral("ddg") << QStringLiteral("mojeek") << QStringLiteral("wiby");
    if (e->id == QLatin1String("mojeek")) out << QStringLiteral("ddg") << QStringLiteral("searx") << QStringLiteral("wiby");
    if (e->id == QLatin1String("wiby")) out << QStringLiteral("ddg") << QStringLiteral("mojeek") << QStringLiteral("searx");
    if (e->id == QLatin1String("marginalia")) out << QStringLiteral("ddg") << QStringLiteral("wiby");
    // Regle eco : on ne bascule JAMAIS vers un moteur qui necessite du JS
    // (le but est de rester leger pour la data mobile).
    QStringList light;
    for (const QString &id : out)
        if (const SearchEngine *f = byId(id))
            if (!f->needsJs) light << id;
    out = light;
    // dedup + jamais deux fois le moteur courant
    out.removeAll(engineId);
    out.removeDuplicates();
    if (out.isEmpty()) out << QStringLiteral("mojeek");
    return out;
}

QUrl SearchEngineManager::resolve(const QString &input, QString *usedEngineId) const
{
    const QString s = trim(input);
    if (s.isEmpty()) return QUrl();

    auto finish = [&](const QUrl &u, const QString &engineId) {
        if (usedEngineId) *usedEngineId = engineId;
        return u;
    };

    // 1. URL explicite
    if (s.startsWith(QLatin1String("about:")) || s.startsWith(QLatin1String("chrome:")))
        return finish(QUrl(s), engineId());
    if (s.contains(QLatin1String("://")))
        return finish(QUrl(s), engineId());
    if (s.startsWith(QLatin1String("file:")) || s.startsWith(QLatin1String("data:")))
        return finish(QUrl(s), engineId());

    // 2. Bang " !w foo " -> site direct ou moteur
    QString bangEngine, bangQuery;
    if (parseBang(s, &bangEngine, &bangQuery)) {
        if (!bangQuery.isEmpty()) {
            const QString direct = siteUrl(bangEngine, bangQuery);
            if (!direct.isEmpty()) return finish(QUrl(direct), bangEngine);
        }
        if (const SearchEngine *be = byId(bangEngine))
            return finish(buildUrl(*be, bangQuery.isEmpty() ? s : bangQuery), bangEngine);
        return finish(buildUrl(*current(), bangQuery.isEmpty() ? s : bangQuery), engineId());
    }

    // 2b. Bang inconnu (typo) : on retire le "!" et on cherche le reste
    if (s.startsWith(QLatin1Char('!'))) {
        const int sp = s.indexOf(QLatin1Char(' '));
        const QString cleaned = (sp < 0) ? QString() : trim(s.mid(sp + 1));
        if (!cleaned.isEmpty())
            return finish(buildUrl(*current(), cleaned), engineId());
    }

    // 3. Prefixe court "so malloc" -> site direct
    QString preEngine, preQuery;
    if (parsePrefix(s, &preEngine, &preQuery)) {
        const QString direct = siteUrl(preEngine, preQuery);
        if (!direct.isEmpty()) return finish(QUrl(direct), preEngine);
        if (SearchEngineManager::byId(preEngine))
            return finish(buildUrl(*byId(preEngine), preQuery), preEngine);
    }

    // 4. URL implicite (domaine, IP, localhost:port)
    // Attention : QUrl interprete "localhost:8080" comme scheme="localhost".
    // On ne respecte le scheme que s'il est un vrai schema connu.
    if (looksLikeHost(s)) {
        static const QStringList realSchemes = {
            QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("ftp"),
            QStringLiteral("ftps"), QStringLiteral("file"), QStringLiteral("sftp")
        };
        const QUrl probe(s);
        if (!realSchemes.contains(probe.scheme()))
            return finish(QUrl(QStringLiteral("https://") + s), engineId());
        return finish(probe, engineId());
    }

    // 5. Recherche
    return finish(buildUrl(*current(), s), engineId());
}

// ---------------------------------------------------------------------------
// Suggestions : historique local (gratuit) + API DDG (~100 octets)
// ---------------------------------------------------------------------------
void SearchEngineManager::requestSuggestions(const QString &text, quint64 token)
{
    m_pendingText = text;
    m_token = token;
    m_debounce.start();
}

void SearchEngineManager::cancelSuggestions()
{
    m_debounce.stop();
    if (!m_reply) return;
    QNetworkReply *r = m_reply;
    m_reply = nullptr;   // le slot ignorera cette reponse et la liberera
    r->abort();
}

void SearchEngineManager::onDebounce()
{
    const QString text = trim(m_pendingText);
    const quint64 tok = m_token;
    if (text.length() < 2) { emit suggestionsReady(text, QStringList(), tok); return; }
    if (!m_remoteSuggestions) { emit suggestionsReady(text, QStringList(), tok); return; }

    // Annulation propre : abort() declenche finished(), et c'est le slot qui fait
    // le deleteLater UNE SEULE FOIS (un double deleteLater sur le meme objet
    // provoquerait un use-after-free).
    if (m_reply) {
        QNetworkReply *prev = m_reply;
        m_reply = nullptr;
        prev->abort();
    }

    // API DuckDuckGo "ac" : ~100 octets pour 8 propositions, aucun JS, aucun tracker.
    // Configurable (DuckDuckGo par defaut) car l'API n'est pas documentee comme stable.
    QNetworkRequest req{QUrl(m_providerUrl + QStringLiteral("?q=%1&type=list&kl=fr-fr").arg(pct(text)))};
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", QByteArrayLiteral("DataSaverBrowser/1.1"));
    req.setRawHeader("Save-Data", "on");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    // Sans delai maximal, un endpoint qui ne repond pas laisse m_reply occupe
    // indefiniment et chaque frappe> 170 ms relance une requete.
    req.setTransferTimeout(8000);
    m_reply = m_nam.get(req);
    // IMPORTANT : on capture le pointeur du reply dans le slot.
    // Si on lisait m_reply depuis le slot, un abort() entre-temps aurait
    // mis le membre a null et on appelerait des methodes sur nullptr.
    QNetworkReply *reply = m_reply;
    // Le texte et le jeton sont captures AU MOMENT DE LA REQUETE : les relire
    // depuis le membre au retour faisait passer les resultats de la requete N
    // pour ceux de la requete N+1 (frappe rapide).
    connect(reply, &QNetworkReply::finished, this, [this, reply, text, tok]{
        // Seul le slot courant traite la reponse ; les reponses annulees sont
        // simplement liberees. deleteLater est appele exactement une fois.
        if (reply != m_reply) { reply->deleteLater(); return; }
        onSuggestionsReply(reply, text, tok);
    });
}

void SearchEngineManager::onSuggestionsReply(QNetworkReply *reply, const QString &text, quint64 tok)
{
    if (!reply) return;
    m_reply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool httpOk = (status == 0) || (status >= 200 && status < 300);
    if (reply->error() != QNetworkReply::NoError || !httpOk) {
        // 4xx/5xx : on ne tente pas de lire un corps qui n'est pas du JSON.
        emit suggestionsReady(text, QStringList(), tok);
        return;
    }

    const QByteArray data = reply->readAll();

    QStringList out;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isArray()) {
        // Format observe : [ "requete", [ "s1", "s2", ... ] ]
        // Format tolere aussi : [ [ "requete", [ ... ] ], [ "requete2", [ ... ] ] ]
        const QJsonArray arr = doc.array();
        auto takeList = [&out](const QJsonArray &list) {
            for (const QJsonValue &s : list) {
                if (out.size() >= 8) return;
                const QString item = s.toString().trimmed();
                if (!item.isEmpty()) out << item;
            }
        };
        // Format reel : [ "requete", [ "s1", "s2", ... ] ]  -> on prend le 2e element
        if (arr.size() >= 2 && arr.at(0).isString() && arr.at(1).isArray()) {
            takeList(arr.at(1).toArray());
        // Le tableau racine est lui-meme une liste de propositions
        } else if (!arr.isEmpty() && arr.at(0).isString()) {
            takeList(arr);
        } else {
            for (const QJsonValue &v : arr) {
                if (out.size() >= 8) break;
                if (!v.isArray()) continue;
                const QJsonArray e = v.toArray();
                if (e.isEmpty()) continue;
                // v = paire [requete, liste]
                if (e.size() >= 2 && e.at(1).isArray())
                    takeList(e.at(1).toArray());
                // v = liste de propositions
                else if (e.at(0).isString())
                    takeList(e);
            }
        }
    }
    emit suggestionsReady(text, out, tok);
}
