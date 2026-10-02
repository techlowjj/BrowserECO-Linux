#pragma once
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include "services/adblocker.h"
#include "services/imagecodec.h"
#include <QAtomicInt>
#include <QAtomicInteger>
#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QSet>
#include <QMutex>
#include <QReadWriteLock>
#include <QStringList>
#include <QVector>

/*
 * Port de Services/RequestInterceptor.cs
 * Niveau 1 : bloque pubs/trackers/fonts/media/prefetch + ajoute Save-Data
 * QWebEngineUrlRequestInterceptor ne peut pas modifier le body (limitation Qt),
 * donc EcoImageServer sert une image compressee (voir wouldRewriteImage).
 *
 * Ajout :compteurs par categorie pour afficher une economie de data chiffree.
 */

/* Jeu d'en-tetes de requete, a TAILLE FIXE.
 *
 * Volontairement pas une QList : celle-ci allouait son tableau a chaque
 * requete, sur le thread reseau, ou une page peut declencher 200 requetes.
 * Ici tout tient dans la pile ; les QByteArray proviennent de QByteArrayLiteral
 * (donnees statiques) donc leur copie ne fait qu'un increment de compteur.
 * La taille 2 correspond aux deux seuls signaux emis (aucune allocation ni
 * verification de type a chaque requete).
 */
struct EcoRequestHeaders {
    static constexpr int kMax = 2;
    int count = 0;
    QByteArray name[kMax];
    QByteArray value[kMax];

    void add(QByteArray n, QByteArray v) {
        if (count < kMax) { name[count] = std::move(n); value[count] = std::move(v); ++count; }
    }
    bool contains(QByteArrayView n, QByteArrayView v) const {
        for (int i = 0; i < count; ++i)
            if (name[i] == n && value[i] == v) return true;
        return false;
    }
};

/* Categories de blocage (ordre stable = index dans EcoInterceptor::m_byCat) */
enum class EcoCategory {
    Pubs      = 0, // regles AdBlock
    Trackers  = 1, // analytics / beacons
    Fonts     = 2,
    Media     = 3, // audio/video/autoplay
    Images    = 4,
    Prefetch  = 5, // preconnect/dns-prefetch/spec. hints
    Stream    = 6, // chunks video (mode Ultra)
    Other     = 7
};
static constexpr int kEcoCategoryCount = 8;

class EcoInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit EcoInterceptor(AdBlocker *adblocker, QObject *parent = nullptr);

    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

    void setDataSaverEnabled(bool e) { m_dataSaver = e; }
    void setImagesOff(bool off) { m_imagesOff = off; }

    /* Compression des images : réécrit l'URL de chaque image vers notre serveur
     * local (EcoImageServer), qui la sert compressée. La réécriture passe par
     * markIfRewritten(), sans quoi la demande revient dans cet intercepteur et
     * se réécrit indéfiniment. */
    void setImageCompression(bool on, int quality, const QUrl &serverBase) {
        m_compressImages = on;
        m_imageQuality = ImageCodec::clampQuality(quality);
        m_imageServer = serverBase;
    }
    bool imageCompression() const { return m_compressImages; }

    /* Règle de réécriture, extraite pour être TESTÉE seule : c'est la barrière
     * qui empêche la boucle. Sans elle, l'URL de notre serveur local (qui est
     * aussi en http) se réécrivait à l'infini — mesuré à 99 itérations en un
     * chargement de page, l'image n'arrivant jamais. */
    bool wouldRewriteImage(const QUrl &url,
                           QWebEngineUrlRequestInfo::ResourceType type) const;
    void setUltraEcoEnabled(bool e) { m_ultraEco = e; }
    // Sec-GPC ("Global Privacy Control") : le signal normalise qui demande aux
    // sites de limiter les donnees. 1 octet par requete, aucune contrepartie
    // mesurable sur les ressources statiques (voir README) mais c'est le seul
    // signal qui exprime le consentement de l'utilisateur a tous les sites.
    void setSecGpcEnabled(bool on) { m_secGpc = on; }
    bool secGpcEnabled() const { return m_secGpc; }

    // En-tetes de requete : fonction PURE (testable sans navigateur).
    // Save-Data : demande de mode economie. Sec-GPC : demande de protection.
    static EcoRequestHeaders privacyHeaders(bool dataSaver, bool secGpc);
    /* ---- Exceptions d'images par site -------------------------------------
     * « Images OFF » est le reglage le plus rentable (-90 % de moyenne mesuree),
     * mais il rend les sites photos/recettes illisibles. L'utilisateur peut
     * donc autoriser les images site par site : la decision porte sur le site
     * VISITE (firstPartyUrl), pas sur l'hote de l'image, car les images sont
     * presque toujours servies par un CDN (i.ytimg.com, cloudfront...).
     * Ces exceptions sont memorisees (reglage imageAllow=).
     */
    void setImageHostAllowed(const QString &siteHost, bool allowed);
    bool isImageHostAllowed(const QString &siteHost) const;
    QStringList imageAllowedHosts() const;
    void clearImageAllowedHosts();
    int imageAllowedCount() const;
    // Fonction pure : correspondance site/exception sur frontieres de label,
    // donc « notexample.com » ne beneficie PAS de l'exception « example.com ».
    static bool hostMatchesAllowList(const QString &host, const QSet<QString> &allowed);

    // Autorise les images des pages de verification anti-bot (captcha).
    // Sans cela, l'utilisateur voit "challenge" mais ne peut pas le resoudre :
    // le blocage d'images rend la page insoluble.
    void setAllowChallengeImages(bool on) { m_allowChallengeImages = on; }
    bool allowChallengeImages() const { return m_allowChallengeImages; }
    bool isDataSaverEnabled() const { return m_dataSaver; }
    bool isUltraEcoEnabled() const { return m_ultraEco; }
    bool imagesOff() const { return m_imagesOff; }

    // Hotes servant une page de verification anti-bot. Sur ces pages, le
    // blocage d'images doit etre leve : sans l'image du captcha, la
    // verification est impossible a resoudre.
    static bool isChallengeHost(const QString &host);

    /*
     * Decision pure, extraite pour etre testable sans navigateur.
     * true = la page courante est une verification anti-bot, donc on ne
     * bloque RIEN (le captcha doit rester visible et résolvable).
     * false = site ordinaire, les bloqueurs s'appliquent normalement.
     */
    static bool isExemptFromBlocking(bool allowChallenge,
                                    const QString &firstPartyHost);

    qint64 blockedCount() const { return m_blockedCount.loadRelaxed(); }
    qint64 allowedCount() const { return m_allowedCount.loadRelaxed(); }
    qint64 categoryCount(int i) const {
        return (i >= 0 && i < kEcoCategoryCount) ? m_byCat[i].loadRelaxed() : 0;
    }
    void resetStats();
    // Vrai si des statistiques ont bouge depuis le dernier takeStatsDirty().
    bool statsDirty() const { return m_statsDirty.loadAcquire() != 0; }
    // Consomme le drapeau (l'appelant a rafraichi l'affichage).
    bool takeStatsDirty() { return m_statsDirty.fetchAndStoreAcquire(0) != 0; }

    // Estimation conservative des octets economises.
    // Moyennes mesurees typiques par categorie (publicite ~45 Ko, image ~60 Ko...).
    static qint64 estimateBytes(int category);
    qint64 estimatedSavedBytes() const;
    // 0-100 : part des requetes bloquees sur le total (indicateur, pas une verite)
    int savedRatio() const;

signals:
    void statsChanged(qint64 blocked);

private:
    bool isNonEssential(const QUrl &url, QWebEngineUrlRequestInfo::ResourceType type, EcoCategory *cat) const;
    // true si l'URL appartient a une page de verification anti-bot connue
    static bool isChallengeUrl(const QUrl &url);

    void noteBlock(EcoCategory cat);

    AdBlocker *m_adblocker = nullptr;
    bool m_dataSaver = true;
    bool m_imagesOff = true;
    bool m_compressImages = false;
    QUrl m_imageServer;                // racine du serveur d'images local
    int m_imageQuality = ImageCodec::kDefaultQuality;
    bool m_secGpc = true;
    // URL déjà réécrites. Plafonnées : au-delà on repart de zéro, sinon la
    // mémoire grossirait sans borne pendant une longue session.
    static constexpr int kMaxRewrittenUrls = 4096;
    mutable QMutex m_rewrittenLock;
    QSet<QString> m_rewritten;
    bool m_ultraEco = false;
    bool m_allowChallengeImages = true;
    QAtomicInt m_statsDirty = 0;
    // Liste des sites ou les images sont autorisees. Relue sur le thread IO a
    // chaque requete d'image et modifiee depuis l'IHM : acces verrouille.
    QSet<QString> m_imageAllowed;
    mutable QReadWriteLock m_imageLock;
    QAtomicInt m_blockedCount = 0;
    QAtomicInt m_allowedCount = 0;
    QAtomicInteger<qint64> m_byCat[kEcoCategoryCount];
};
