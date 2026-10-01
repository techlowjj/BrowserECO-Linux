#pragma once
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include "services/adblocker.h"
#include <QAtomicInt>
#include <QAtomicInteger>
#include <QVector>

/*
 * Port de Services/RequestInterceptor.cs
 * Niveau 1 : bloque pubs/trackers/fonts/media/prefetch + ajoute Save-Data
 * QWebEngineUrlRequestInterceptor ne peut pas modifier le body (limitation Qt),
 * donc ImageOptimizer sera branche en Niveau 2 via UrlSchemeHandler.
 *
 * Ajout :compteurs par categorie pour afficher une economie de data chiffree.
 */

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
    void setUltraEcoEnabled(bool e) { m_ultraEco = e; }
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
    bool m_ultraEco = false;
    bool m_allowChallengeImages = true;
    QAtomicInt m_statsDirty = 0;
    QAtomicInt m_blockedCount = 0;
    QAtomicInt m_allowedCount = 0;
    QAtomicInteger<qint64> m_byCat[kEcoCategoryCount];
};
