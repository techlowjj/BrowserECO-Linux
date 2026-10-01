#include "services/searchengine.h"
#include "services/serpguard.h"
#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>

static int fails = 0;
static void chkB(const QString &label, bool got, bool want) {
    const bool ok = (got == want);
    if (!ok) ++fails;
    QTextStream(stdout) << (ok ? "  ok   " : "  FAIL ") << label
                        << "  got=" << (got ? "true" : "false")
                        << (ok ? "" : QString(" want=%1").arg(want ? "true" : "false")) << "\n";
}

static void chk(const QString &label, const QString &got, const QString &want) {
    const bool ok = (got == want);
    if (!ok) ++fails;
    QTextStream(stdout) << (ok ? "  ok   " : "  FAIL ") << label
                        << "  got=[" << got << "]"
                        << (ok ? "" : " want=[" + want + "]") << "\n";
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    SearchEngineManager m;

    QTextStream out(stdout);

    out << "--- Resolution des saisies ---\n";
    struct Case { const char *in; const char *expectUrl; };
    const Case cases[] = {
        { "exemple.fr",                 "https://exemple.fr" },
        { "www.wikipedia.org/wiki/Linux","https://www.wikipedia.org/wiki/Linux" },
        { "http://linux.org",           "http://linux.org" },
        { "localhost:8080",             "https://localhost:8080" },
        { "192.168.1.1",                "https://192.168.1.1" },
        { "127.0.0.1:3000",             "https://127.0.0.1:3000" },
        { "about:home",                 "about:home" },
        { "qui est linus torvalds",     "https://html.duckduckgo.com/html/?q=qui%20est%20linus%20torvalds&kl=fr-fr" },
        { "malloc memory",              "https://html.duckduckgo.com/html/?q=malloc%20memory&kl=fr-fr" },
        { "!w linux kernel",            "https://fr.wikipedia.org/w/index.php?search=linux%20kernel" },
        { "!so lambda python",          "https://stackoverflow.com/search?q=lambda%20python" },
        { "!gh qt6",                    "https://github.com/search?q=qt6&type=repositories" },
        { "!mdn array",                 "https://developer.mozilla.org/fr/search?q=array" },
        { "so lambda",                  "https://stackoverflow.com/search?q=lambda" },
        { "gh qt6 webengine",           "https://github.com/search?q=qt6%20webengine&type=repositories" },
        { "w kernel linux",             "https://fr.wikipedia.org/w/index.php?search=kernel%20linux" },
    };
    for (const Case &c : cases) {
        QString used;
        const QUrl u = m.resolve(QString::fromLatin1(c.in), &used);
        chk(QString::fromLatin1(c.in), QString::fromUtf8(u.toEncoded()), QString::fromLatin1(c.expectUrl));
    }

    out << "\n--- Bangs vers un moteur (pas un site) ---\n";
    {
        QString used;
        const QUrl u = m.resolve(QStringLiteral("!sxiu test"), &used);
        chk("!sxiu", QString::fromUtf8(u.toEncoded()), QStringLiteral("https://html.duckduckgo.com/html/?q=test&kl=fr-fr"));
        chk("!sxiu engine", used, QStringLiteral("ddg"));
    }

    out << "\n--- Chaine de repli : jamais de moteur JS ---\n";
    for (const char *start : { "ddg", "searx", "mojeek", "google", "brave", "wiby" }) {
        const QStringList chain = m.fallbackChain(QString::fromLatin1(start));
        for (const QString &id : chain) {
            const SearchEngine *e = SearchEngineManager::byId(id);
            if (e && e->needsJs) {
                out << "  FAIL repli vers moteur JS : " << start << " -> " << id << "\n";
                ++fails;
            }
        }
        out << "  " << start << " -> " << chain.join(QStringLiteral(", ")) << "\n";
    }

    out << "\n--- Instance SearXNG personnalisee ---\n";
    {
        SearchEngineManager m2;
        m2.setSearxUrl(QStringLiteral("mon-serveur.fr/searx"));
        const QUrl u = m2.resolve(QStringLiteral("test query"));
        Q_UNUSED(u)
        m2.setEngineId(QStringLiteral("searx"));
        const QUrl u2 = m2.resolve(QStringLiteral("test query"));
        chk("searx custom", QString::fromUtf8(u2.toEncoded()),
            QStringLiteral("https://mon-serveur.fr/searx?q=test%20query&language=fr-FR&safesearch=0"));
        chk("engineForHost searx.be", SearchEngineManager::engineForHost(QUrl("https://searx.be/search?q=a")), QStringLiteral("searx"));
    }

    out << "\n--- Detection moteur depuis URL ---\n";
    chk("html.duckduckgo.com", SearchEngineManager::engineForHost(QUrl("https://html.duckduckgo.com/html/?q=x")), QStringLiteral("ddg"));
    chk("lite.duckduckgo.com", SearchEngineManager::engineForHost(QUrl("https://lite.duckduckgo.com/lite/?q=x")), QStringLiteral("ddg"));
    chk("www.mojeek.com",      SearchEngineManager::engineForHost(QUrl("https://www.mojeek.com/search?q=x")), QStringLiteral("mojeek"));
    chk("wiby.me",             SearchEngineManager::engineForHost(QUrl("https://wiby.me/?q=x")), QStringLiteral("wiby"));
    chk("example.com",         SearchEngineManager::engineForHost(QUrl("https://example.com/")), QString());

    out << "\n--- Accessibilite clavier : au plus 9 moteurs (Alt+1..9) ---\n";
    chkB("registre <= 9 (Alt+1..9)", SearchEngineManager::registry().size() <= 9, true);
    chkB("registre >= 5", SearchEngineManager::registry().size() >= 5, true);
    for (int i = 0; i < SearchEngineManager::registry().size(); ++i) {
        const SearchEngine &e = SearchEngineManager::registry().at(i);
        chkB(QStringLiteral("%1 : badge").arg(e.id), !e.shortLabel.isEmpty(), true);
        chkB(QStringLiteral("%1 : poids declare").arg(e.id), !e.weightNote.isEmpty(), true);
        chkB(QStringLiteral("%1 : hote").arg(e.id), !e.host.isEmpty(), true);
        chkB(QStringLiteral("%1 : label").arg(e.id), !e.label.isEmpty(), true);
    }

    out << "\n--- Detection anti-bot (pages de resultats) ---\n";
    chkB("ddg", SerpGuard::isSearchPage(QUrl("https://html.duckduckgo.com/html/?q=a")), true);
    chkB("wikipedia", SerpGuard::isSearchPage(QUrl("https://fr.wikipedia.org/wiki/A")), false);

    out << "\n--- Poids data des moteurs (valeurs declarees) ---\n";
    for (const SearchEngine &e : SearchEngineManager::registry())
        out << "  " << e.shortLabel << "  " << e.weightNote
            << (e.needsJs ? "  [JS]" : "  [0 JS]") << "\n";

    out << "\n";
    if (fails == 0) out << "TOUS LES TESTS PASSENT\n";
    else out << fails << " ECHEC(S)\n";
    return fails == 0 ? 0 : 1;
}
