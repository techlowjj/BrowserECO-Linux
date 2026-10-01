#include "serpguard.h"

#include <QWebEngineView>
#include <QWebEnginePage>
#include <QPointer>
#include <QDebug>
#include <memory>

SerpGuard::SerpGuard(QObject *parent)
    : QObject(parent)
{}

bool SerpGuard::isSearchPage(const QUrl &url)
{
    static const QStringList hosts = {
        QStringLiteral("html.duckduckgo.com"), QStringLiteral("lite.duckduckgo.com"),
        QStringLiteral("duckduckgo.com"), QStringLiteral("www.mojeek.com"),
        QStringLiteral("mojeek.com"), QStringLiteral("searx.be"),
        QStringLiteral("search.inetol.net"), QStringLiteral("wiby.me"),
        QStringLiteral("search.marginalia.nu"), QStringLiteral("www.startpage.com"),
        QStringLiteral("yep.com"), QStringLiteral("search.brave.com"),
        QStringLiteral("www.google.com"), QStringLiteral("www.qwant.com"),
    };
    const QString host = url.host();
    if (hosts.contains(host)) return true;
    // Instance SearXNG personnalisee : c'est une page de resultats meme si
    // son hote n'est pas dans la liste (fonctionnalite du navigateur).
    return host.contains(QLatin1String("searx")) || host.contains(QLatin1String("search"));
}

QString SerpGuard::probeScript()
{
    // ~700 octets, execute une fois par page de resultats.
    return QStringLiteral(R"JS((function(){
  var t = (document.title || '').toLowerCase();
  var head = (document.body ? document.body.innerText.slice(0, 500) : '').toLowerCase();
  var all = t + ' ' + head;
  var wall = /captcha|anomaly|security check|verify|are you a robot|unusual traffic|just a moment|access denied|substation|rate limit|javascript (is )?(required|disabled)/.test(all);
  var res = document.querySelectorAll('a.result__a,.result__title,.result-title,.result-header,.result,.serp-result,article.result,.web-result,.result_content,.results .result,.result-url,#links .result,.result-body').length;
  var links = document.querySelectorAll('#links a,.results a,.result a,.result-body a').length;
  return { res: res, links: links, wall: wall };
})())JS");
}

bool SerpGuard::looksBlocked(const QVariantMap &data)
{
    if (data.isEmpty()) return false;
    const int res = data.value(QStringLiteral("res")).toInt();
    const int links = data.value(QStringLiteral("links")).toInt();
    const bool wall = data.value(QStringLiteral("wall")).toBool();
    if (wall) return true;
    return res == 0 && links == 0;
}

void SerpGuard::probe(QWebEngineView *view, const QUrl &url, const QString &engineId)
{
    if (!view || !view->page()) return;
    if (!isSearchPage(url)) return;

    // Le drapeau « deja traite » est LOCAL a cette sonde : un membre partage
    // faisait que la reponse de la deuxieme page de recherche ouverte en
    // parallele etait silencieusement ignoree.
    QPointer<QWebEnginePage> page(view->page());
    QPointer<SerpGuard> self(this);
    auto fired = std::make_shared<bool>(false);   // un par sonde
    view->page()->runJavaScript(probeScript(), [self, page, engineId, fired](const QVariant &v) {
        const QVariantMap m = v.toMap();
        if (*fired) return;
        *fired = true;
        // La fenetre peut avoir ete fermee pendant l'execution du script.
        if (self.isNull() || page.isNull()) return;
        if (looksBlocked(m)) {
            emit self->blocked(engineId, QString());
        } else {
            // Le moteur a repondu : on peut l'adopter comme moteur par defaut.
            emit self->succeeded(engineId);
        }
    });
}
