#pragma once
#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QUrl>

#include "ecoimageurl.h"
#include "imagecache.h"

class QNetworkAccessManager;

/*
 * EcoImageServer — serveur HTTP local qui sert les images compressées.
 *
 * L'intercepteur réécrit l'URL de chaque image vers
 * http://127.0.0.1:<port>/i?u=…&s=…&w=…&q=… ; ce serveur récupère l'original,
 * le compresse (ImageCodec), met le résultat en cache, puis répond.
 *
 * Écoute UNIQUEMENT sur 127.0.0.1, port éphémère, et ne répond qu'aux URL
 * signées qu'il a lui-même produites (voir EcoImageUrl) : même si une page
 * découvre l'adresse du serveur, elle ne peut lui faire récupérer que des
 * images déjà demandées par l'utilisateur.
 *
 * Trois décisions :
 *  1. JAMAIS de seconde requête. Quand la compression ne sert à rien (GIF, SVG,
 *     image trop petite, encodage impossible), on répond avec les octets déjà
 *     reçus : une redirection ferait télécharger l'image deux fois.
 *  2. ÉCHEC OUVERT. Erreur réseau ou délai dépassé : on répond 502, le
 *     navigateur retente l'URL d'origine et l'image s'affiche malgré tout.
 *  3. UNE SEULE IMPLÉMENTATION. Toute la politique de compression vit dans
 *     ImageCodec ; ce fichier ne fait que du transport HTTP.
 *
 * Une requête par connexion (« Connection: close ») : sans état de connexion
 * à gérer, il n'y a ni tampon partagé entre sockets, ni requête qui reste en
 * suspens si la page abandonne. Sur la boucle locale, le coût est nul.
 */
class EcoImageServer : public QObject {
    Q_OBJECT
public:
    explicit EcoImageServer(QObject *parent = nullptr);
    ~EcoImageServer() override;

    bool start();
    void stop();
    bool isRunning() const { return m_server->isListening(); }
    QUrl base() const { return m_base; }

    // URL signée à donner à l'intercepteur.
    QUrl imageUrl(const QUrl &original, int targetWidth, int quality) const
    { return EcoImageUrl::encode(m_base, original, targetWidth, quality); }

    // Statistiques réelles, pour le panneau Eco.
    qint64 originalBytes() const { return m_originalBytes; }
    qint64 servedBytes() const { return m_servedBytes; }
    int compressedCount() const { return m_compressed; }
    int passthroughCount() const { return m_passthrough; }
    int failedCount() const { return m_failed; }
    ImageCache &cache() { return m_cache; }

    /* Remet les compteurs à zéro, SANS vider le cache réseau : l'utilisateur
     * veut repartir de zéro dans l'affichage, pas retélécharger des images déjà
     * en cache (cela gaspillerait de la bande passante). */
    void resetStats();

signals:
    void imageServed();

private:
    void onNewConnection();
    void onReadyRead();
    void fetchAndServe(QTcpSocket *socket, const EcoImageUrl::Target &target,
                       const QMap<QByteArray, QByteArray> &entetes);
    void respond(QTcpSocket *socket, int code, const QByteArray &contentType,
                 const QByteArray &body, const QByteArray &extraHeaders = QByteArray());

    QTcpServer *m_server = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    ImageCache m_cache;
    QUrl m_base;
    qint64 m_originalBytes = 0;
    qint64 m_servedBytes = 0;
    int m_compressed = 0;
    int m_passthrough = 0;
    int m_failed = 0;
};