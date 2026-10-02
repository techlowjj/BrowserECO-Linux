#include "ecolazyurl.h"

#include <QUrlQuery>

bool EcoLazyUrl::removeMarker(const QUrl &url, QUrl *propre)
{
    QUrlQuery q(url);
    if (!q.hasQueryItem(QString::fromLatin1(markerKey))) return false;

    q.removeQueryItem(QString::fromLatin1(markerKey));
    QUrl u = url;
    // Si la query devient vide, on la retire entierement : sinon il resterait un
    // '?' et l'URL « propre » ne serait plus identique a celle d'origine (elle ne
    // serait donc plus reconnue comme deja chargee).
    u.setQuery(q.isEmpty() ? QString() : QString(q.query(QUrl::FullyEncoded)));
    if (propre) *propre = u;
    return true;
}

QUrl EcoLazyUrl::mark(const QUrl &url)
{
    QUrlQuery q(url);
    q.addQueryItem(QString::fromLatin1(markerKey), QStringLiteral("1"));
    QUrl u = url;
    u.setQuery(q);
    return u;
}