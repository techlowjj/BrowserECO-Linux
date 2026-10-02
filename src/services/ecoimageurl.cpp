#include "ecoimageurl.h"
#include "imagecodec.h"

#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <QUrlQuery>
#include <QUuid>

namespace {

// Clé aléatoire par processus : jamais sur disque, jamais transmise.
QByteArray sessionKey()
{
    static const QByteArray key = QUuid::createUuid().toRfc4122();
    return key;
}

QByteArray toBase64Url(const QByteArray &in)
{
    return in.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray fromBase64Url(const QByteArray &in)
{
    return QByteArray::fromBase64(in, QByteArray::Base64UrlEncoding);
}

} // namespace

QByteArray EcoImageUrl::sign(const QByteArray &payload, int width, int quality)
{
    // 8 octets d'empreinte suffisent : le but est de distinguer « écrit par
    // l'application » de « écrit par une page », pas de résister à une attaque
    // contre le processus lui-même.
    const QByteArray message = payload + '|' + QByteArray::number(width) + '|'
                               + QByteArray::number(quality);
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256, sessionKey());
    mac.addData(message);
    return mac.result().toHex().left(8);
}

QUrl EcoImageUrl::encode(const QUrl &base, const QUrl &original, int targetWidth, int quality)
{
    if (!base.isValid() || base.isEmpty() || !original.isValid() || original.isEmpty())
        return {};
    // Seules les URL http(s) ont un sens ici : cela exclut file:, data: et les
    // schémas internes, donc rien de ce que l'on compresse ne vient du disque.
    const QString s = original.scheme().toLower();
    if (s != "http" && s != "https") return {};

    const int w = qBound(0, targetWidth, ImageCodec::kMaxWidth);
    const int q = ImageCodec::clampQuality(quality);
    const QByteArray payload = toBase64Url(original.toEncoded());

    QUrl url = base;
    url.setPath(QLatin1String(path));
    url.setQuery(QStringLiteral("u=%1&s=%2&w=%3&q=%4")
                     .arg(QString::fromLatin1(payload),
                          QString::fromLatin1(sign(payload, w, q)))
                     .arg(w).arg(q));
    return url;
}

EcoImageUrl::Target EcoImageUrl::decode(const QUrl &requestUrl, const QUrl &base)
{
    Target t;
    // Une URL qui n'est pas la nienne n'est pas pour nous, même si sa signature
    // est valide (par exemple un serveur qui aurait le même port).
    if (requestUrl.scheme() != base.scheme() || requestUrl.host() != base.host()
        || requestUrl.port() != base.port())
        return t;

    const QUrlQuery q(requestUrl);
    const QByteArray payload = q.queryItemValue(QStringLiteral("u"), QUrl::FullyDecoded).toLatin1();
    const QByteArray signature = q.queryItemValue(QStringLiteral("s"), QUrl::FullyDecoded).toLatin1();
    bool okW = false, okQ = false;
    const int w = q.queryItemValue(QStringLiteral("w")).toInt(&okW);
    const int qual = q.queryItemValue(QStringLiteral("q")).toInt(&okQ);
    if (payload.isEmpty() || signature.isEmpty() || !okW || !okQ) return t;

    // Vérification AVANT tout accès réseau.
    if (signature != sign(payload, w, qual)) return t;

    const QUrl original(fromBase64Url(payload));
    const QString s = original.scheme().toLower();
    if (!original.isValid() || original.isEmpty()) return t;
    if (s != "http" && s != "https") return t;    // seconde barrière

    t.original = original;
    t.targetWidth = qBound(0, w, ImageCodec::kMaxWidth);
    t.quality = ImageCodec::clampQuality(qual);
    t.valid = true;
    return t;
}