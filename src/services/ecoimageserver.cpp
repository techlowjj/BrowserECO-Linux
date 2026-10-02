#include "ecoimageserver.h"
#include "imagecodec.h"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace {

// Un CDN qui met plus de 15 s à répondre est de toute façon inrattrapable, et
// bloquer l'image serait pire que de la servir telle quelle.
constexpr int kFetchTimeoutMs = 15000;
constexpr int kMaxRequestBytes = 16 * 1024;

// En-têtes que l'on ne rejoue pas vers l'original : ils décrivent la connexion
// avec le navigateur, pas la ressource. Les renvoyer casse la requête (Host en
// double, corps sans Content-Length, Accept-Encoding gzip alors qu'on veut les
// octets bruts…). Le jeton d'authentification est surtout à ne PAS renvoyer : une
// redirection du serveur vers un autre hôte le lui transmettrait.
const QSet<QByteArray> kDroppedHeaders = {
    "host", "connection", "content-length", "transfer-encoding", "accept-encoding",
    "cookie", "range", "if-range", "if-modified-since", "if-none-match",
    "content-type", "origin", "sec-fetch-dest", "sec-fetch-mode", "sec-fetch-site",
    "authorization", "proxy-authorization",
};

QByteArray statusText(int code)
{
    switch (code) {
    case 200: return QByteArrayLiteral("OK");
    case 403: return QByteArrayLiteral("Forbidden");
    case 413: return QByteArrayLiteral("Payload Too Large");
    case 502: return QByteArrayLiteral("Bad Gateway");
    default:  return QByteArrayLiteral("Error");
    }
}

} // namespace

EcoImageServer::EcoImageServer(QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &EcoImageServer::onNewConnection);
}

EcoImageServer::~EcoImageServer() = default;

bool EcoImageServer::start()
{
    if (m_server->isListening()) return true;
    // 127.0.0.1 uniquement, port éphémère : aucune exposition sur le réseau et
    // pas de collision avec un autre service.
    if (!m_server->listen(QHostAddress::LocalHost, 0)) return false;
    m_base = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server->serverPort()));
    return true;
}

void EcoImageServer::stop()
{
    m_server->close();
}

void EcoImageServer::onNewConnection()
{
    while (QTcpSocket *s = m_server->nextPendingConnection()) {
        connect(s, &QTcpSocket::readyRead, this, &EcoImageServer::onReadyRead);
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    }
}

void EcoImageServer::onReadyRead()
{
    auto *s = qobject_cast<QTcpSocket *>(sender());
    if (!s) return;

    QByteArray requete = s->property("ecoRequete").toByteArray() + s->readAll();
    if (requete.size() > kMaxRequestBytes) {
        s->setProperty("ecoRequete", QByteArray());
        respond(s, 413, "text/plain", QByteArrayLiteral("requête trop grande"));
        return;
    }
    // Une requête se termine par une ligne vide : on n'agit qu'une fois le
    // message complet, jamais sur une réponse à moitié lue.
    const int fin = requete.indexOf("\r\n\r\n");
    if (fin < 0) {
        s->setProperty("ecoRequete", requete);
        return;
    }
    s->setProperty("ecoRequete", QByteArray());

    const QList<QByteArray> lignes = requete.left(fin).split('\n');
    const QList<QByteArray> parts = lignes.value(0).trimmed().split(' ');
    const QByteArray methode = parts.value(0);
    const QByteArray chemin = parts.value(1);
    if (chemin.isEmpty()) {
        respond(s, 403, "text/plain", QByteArrayLiteral("requête illisible"));
        return;
    }
    QUrl url = QUrl(QString::fromLatin1(chemin));
    if (url.scheme().isEmpty())   // certains clients n'envoient que le chemin
        url = m_base.resolved(url);

    QMap<QByteArray, QByteArray> entetes;
    for (const QByteArray &l : lignes) {
        const int i = l.indexOf(':');
        if (i > 0) entetes.insert(l.left(i).trimmed().toLower(), l.mid(i + 1).trimmed());
    }

    if (methode == "HEAD") {          // sondes et outils de diagnostic
        respond(s, 200, "image/jpeg", QByteArray());
        return;
    }
    if (methode != "GET") {
        respond(s, 403, "text/plain", QByteArrayLiteral("méthode non autorisée"));
        return;
    }

    const EcoImageUrl::Target target = EcoImageUrl::decode(url, m_base);
    if (!target.valid) {
        // Non signé, ou aimed at autre chose : AUCUNE requête ne part vers le
        // réseau. C'est exactement le but de la signature.
        respond(s, 403, "text/plain", QByteArrayLiteral("requête refusée"));
        return;
    }

    const QString key = ImageCache::keyFor(target.original.toEncoded(), target.targetWidth,
                                           target.quality, ImageCodec::preferredFormat());
    ImageCache::Entry cached;
    if (m_cache.get(key, cached)) {
        ++m_passthrough;
        m_servedBytes += cached.data.size();
        emit imageServed();
        respond(s, 200, cached.contentType, cached.data,
                "Cache-Control: private, max-age=3600\r\n");
        return;
    }
    fetchAndServe(s, target, entetes);
}

void EcoImageServer::fetchAndServe(QTcpSocket *socket, const EcoImageUrl::Target &target,
                                   const QMap<QByteArray, QByteArray> &entetes)
{
    if (!m_nam) m_nam = new QNetworkAccessManager(this);

    QNetworkRequest req(target.original);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
    // On veut les octets BRUTS : sans cela un CDN nous enverrait du gzip et le
    // codec tenterait de décoder une image compressée.
    req.setRawHeader("Accept-Encoding", "identity");
    req.setRawHeader("Accept", "image/webp,image/apng,image/*,*/*;q=0.8");

    // Rejeu des en-têtes utiles du navigateur : User-Agent, Accept-Language…
    // (certains CDN refusent une requête sans User-Agent).
    for (auto it = entetes.cbegin(); it != entetes.cend(); ++it)
        if (!kDroppedHeaders.contains(it.key()))
            req.setRawHeader(it.key(), it.value());

    QNetworkReply *reply = m_nam->get(req);
    // Si la page abandonne (changement de page, fermeture d'onglet), on arrête de
    // télécharger une image que plus personne n'attend.
    connect(socket, &QObject::destroyed, reply, &QNetworkReply::abort);

    // QPointer, PAS un pointeur brut : la page peut abandonner et le socket être
    // supprimé AVANT la fin de la requête. Tester « socket != nullptr » sur un
    // pointeur déjà libéré ne protège de rien (use-after-free : c'était un
    // segfault aléatoire en fin de test).
    const QPointer<QTcpSocket> socketSurveillant(socket);
    connect(reply, &QNetworkReply::finished, this, [this, socketSurveillant, reply, target] {
        reply->deleteLater();
        QTcpSocket *s = socketSurveillant.data();
        if (!s) return;

        if (reply->error() != QNetworkReply::NoError) {
            ++m_failed;
            emit imageServed();
            respond(s, 502, "text/plain", QByteArrayLiteral("image non récupérée"));
            return;
        }

        const QByteArray data = reply->readAll();
        const QByteArray contentType = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
        m_originalBytes += data.size();

        ImageCodec::Request creq;
        creq.data = data;
        creq.contentType = QString::fromLatin1(contentType);
        creq.targetWidth = target.targetWidth;
        creq.quality = target.quality;
        const ImageCodec::Result res = ImageCodec::compress(creq);

        QByteArray body = data;
        QByteArray type = contentType;
        bool compressee = false;
        if (res.saves()) {
            body = res.data;
            type = res.contentType;
            compressee = true;
            const QString key = ImageCache::keyFor(target.original.toEncoded(), target.targetWidth,
                                                   target.quality, res.contentType);
            m_cache.put(key, res.data, res.contentType);
            ++m_compressed;
        } else {
            // Rien à gagner : on sert les octets déjà téléchargés (une
            // redirection ferait télécharger l'image une seule fois de plus).
            ++m_passthrough;
        }
        m_servedBytes += body.size();
        emit imageServed();

        // Le type de sortie doit être exact : c'est lui qui décide si le
        // navigateur décode l'image. S'il est absent ou non image, on le déduit
        // des octets — sinon le navigateur afficherait du texte.
        QByteArray finalType = type.split(';').first().trimmed().toLower();
        if (finalType.isEmpty() || !finalType.startsWith("image/")) {
            const QString devine = ImageCodec::guessContentType(body);
            finalType = devine.isEmpty() ? QByteArrayLiteral("application/octet-stream")
                                         : devine.toLatin1();
        }
        respond(s, 200, finalType, body,
                compressee ? "Cache-Control: private, max-age=3600\r\nX-Eco-Compressed: 1\r\n"
                           : "Cache-Control: private, max-age=300\r\n");
    });

    QTimer::singleShot(kFetchTimeoutMs, reply, [reply] {
        if (reply->isRunning()) reply->abort();
    });
}

void EcoImageServer::resetStats()
{
    m_originalBytes = 0;
    m_servedBytes = 0;
    m_compressed = 0;
    m_passthrough = 0;
    m_failed = 0;
    emit imageServed();     // rafraîchit le panneau sur le champ
}

void EcoImageServer::respond(QTcpSocket *socket, int code, const QByteArray &contentType,
                             const QByteArray &body, const QByteArray &extraHeaders)
{
    if (!socket) return;
    QByteArray reponse =
        "HTTP/1.1 " + QByteArray::number(code) + " " + statusText(code) + "\r\n"
        + "Server: BrowserECO\r\n"
        + "Content-Type: " + contentType + "\r\n"
        + "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
        + "Connection: close\r\n";
    if (!extraHeaders.isEmpty()) reponse += extraHeaders;
    // PAS de Access-Control-Allow-Origin : une page ne doit pas pouvoir lire la
    // réponse de notre serveur, même pour une URL signée qu'elle a vue passer.
    reponse += "\r\n";
    socket->write(reponse + body);
    socket->flush();
    socket->disconnectFromHost();
}