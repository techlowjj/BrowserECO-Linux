#pragma once
#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSemaphore>
#include <atomic>
#include <QTcpServer>
#include <QUrl>

#include "ecoimageurl.h"
#include "imagecache.h"

class QNetworkAccessManager;
class QThreadPool;

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

    /* Pool de compression DEDIE (pas le pool global de Qt) : le nombre de
     * compressions simultanees devient explicite et isole du reste de Qt.
     * Chaque compression concurrente retient une image decodee entiere (~33 Mo
     * en 4K) : baisser maxThreadCount reduit le pic memoire, au detriment du
     * debit sur les pages riches en images. */
    QThreadPool *compressionPool() const { return m_pool; }

    /* Nombre maximal de téléchargements simultanés observé. Compteur de
     * monitoring (et de test) : il prouve que le plafond est respecté. */
    int maxSimultaneousFetches() const { return m_maxEnV.load(); }

    /* Ajuste le budget mémoire du cache d'images (curseur de l'IHM). */
    void setCacheBudget(qint64 bytes) { m_cache.setBudget(bytes); }

    /* Remet les compteurs à zéro, SANS vider le cache réseau : l'utilisateur
     * veut repartir de zéro dans l'affichage, pas retélécharger des images déjà
     * en cache (cela gaspillerait de la bande passante). Vide aussi la file
     * d'attente : des requêtes en file après une remise à zéro seraient
     * incohérentes. */
    void resetStats();

signals:
    void imageServed();

private:
    void onNewConnection();
    void onReadyRead();
    void fetchAndServe(QTcpSocket *socket, const EcoImageUrl::Target &target,
                       const QMap<QByteArray, QByteArray> &entetes);
    void fetchAndCompress(QTcpSocket *socket, const EcoImageUrl::Target &target,
                          const QMap<QByteArray, QByteArray> &entetes);
    void processQueue();
    /* Le compteur en vol et le sémaphore ne font qu'un : ces deux helpers
     * garantissent qu'ils restent cohérents (sinon le plafond pourrait être
     * dépassé sans qu'on le voie). */
    bool acquireSlot();
    void releaseSlot();
    void compressAndRespond(const QPointer<QTcpSocket> &socket, const QByteArray &data,
                           const QByteArray &contentType, const EcoImageUrl::Target &target);
    void respond(QTcpSocket *socket, int code, const QByteArray &contentType,
                 const QByteArray &body, const QByteArray &extraHeaders = QByteArray());

    /* Plafond de concurrence : une page avec 200 photos ne doit pas ouvrir 200
     * connexions simultanées vers les CDN. Chromium plafonne à 6 par hôte ;
     * nous allons vers plusieurs hôte différents, donc un plafond global de
     * 16 est raisonnable. */
    static constexpr int kMaxConcurrentFetches = 16;
    struct Pending {
        QPointer<QTcpSocket> socket;
        EcoImageUrl::Target target;
        QMap<QByteArray, QByteArray> entetes;
    };

    QTcpServer *m_server = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QThreadPool *m_pool = nullptr;
    ImageCache m_cache;
    QUrl m_base;
    QSemaphore m_inFlight{kMaxConcurrentFetches};
    QQueue<Pending> m_queue;
    std::atomic<int> m_enV{0};          // téléchargements en cours
    std::atomic<int> m_maxEnV{0};       // max observé (monitoring + tests)
    qint64 m_originalBytes = 0;
    qint64 m_servedBytes = 0;
    int m_compressed = 0;
    int m_passthrough = 0;
    int m_failed = 0;
};