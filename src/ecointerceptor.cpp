#include "ecointerceptor.h"
#include <QUrl>
#include <QDebug>
#include <QRegularExpression>

/* Correspondance « site » / liste d'exceptions, avec frontieres de label :
   "example.com" couvre "www.example.com" et "cdn.example.com", mais pas
   "notexample.com" (une simple recherche de sous-chaine le ferait). */
bool EcoInterceptor::hostMatchesAllowList(const QString &host, const QSet<QString> &allowed)
{
    if (host.isEmpty() || allowed.isEmpty()) return false;
    const QString h = host.toLower();
    if (allowed.contains(h)) return true;
    // Remonte les sous-domaines un label a la fois.
    int dot = h.indexOf(QLatin1Char('.'));
    while (dot >= 0) {
        if (allowed.contains(h.mid(dot + 1))) return true;
        dot = h.indexOf(QLatin1Char('.'), dot + 1);
    }
    return false;
}

void EcoInterceptor::setImageHostAllowed(const QString &siteHost, bool allowed)
{
    const QString host = siteHost.trimmed().toLower();
    if (host.isEmpty()) return;
    {
        QWriteLocker lock(&m_imageLock);
        if (allowed) m_imageAllowed.insert(host);
        else m_imageAllowed.remove(host);
    }
    // Les compteurs sont remis a jour : la page affichee n'a plus d'images
    // bloquees a l'ecran, laisser l'ancien total induirait en erreur.
    resetStats();
}

bool EcoInterceptor::isImageHostAllowed(const QString &siteHost) const
{
    QReadLocker lock(&m_imageLock);
    return hostMatchesAllowList(siteHost, m_imageAllowed);
}

QStringList EcoInterceptor::imageAllowedHosts() const
{
    QReadLocker lock(&m_imageLock);
    QStringList out(m_imageAllowed.constBegin(), m_imageAllowed.constEnd());
    out.sort();
    return out;
}

void EcoInterceptor::clearImageAllowedHosts()
{
    {
        QWriteLocker lock(&m_imageLock);
        m_imageAllowed.clear();
    }
    resetStats();
}

int EcoInterceptor::imageAllowedCount() const
{
    QReadLocker lock(&m_imageLock);
    return int(m_imageAllowed.size());
}

QList<QPair<QByteArray, QByteArray>> EcoInterceptor::privacyHeaders(bool dataSaver, bool secGpc)
{
    QList<QPair<QByteArray, QByteArray>> h;
    if (dataSaver) h.append({ QByteArrayLiteral("Save-Data"), QByteArrayLiteral("on") });
    if (secGpc)   h.append({ QByteArrayLiteral("Sec-GPC"), QByteArrayLiteral("1") });
    return h;
}

EcoInterceptor::EcoInterceptor(AdBlocker *adblocker, QObject *parent)
    : QWebEngineUrlRequestInterceptor(parent), m_adblocker(adblocker)
{}

void EcoInterceptor::resetStats()
{
    m_blockedCount.storeRelaxed(0);
    m_allowedCount.storeRelaxed(0);
    for (int i = 0; i < kEcoCategoryCount; ++i) m_byCat[i].storeRelaxed(0);
}

qint64 EcoInterceptor::estimateBytes(int category)
{
    // Estimations prudentes (octets) — uniquement pour l'affichage "estimation"
    switch (static_cast<EcoCategory>(category)) {
    case EcoCategory::Pubs:     return 45  * 1024;
    case EcoCategory::Trackers: return 12  * 1024;
    case EcoCategory::Fonts:    return 40  * 1024;
    case EcoCategory::Media:    return 900 * 1024;
    case EcoCategory::Images:   return 60  * 1024;
    case EcoCategory::Prefetch: return 2   * 1024;
    case EcoCategory::Stream:   return 1500 * 1024;
    case EcoCategory::Other:    return 20  * 1024;
    }
    return 0;
}

qint64 EcoInterceptor::estimatedSavedBytes() const
{
    qint64 total = 0;
    for (int i = 0; i < kEcoCategoryCount; ++i)
        total += m_byCat[i].loadRelaxed() * estimateBytes(i);
    return total;
}

int EcoInterceptor::savedRatio() const
{
    const qint64 blocked = m_blockedCount.loadRelaxed();
    const qint64 allowed = m_allowedCount.loadRelaxed();
    const qint64 total = blocked + allowed;
    if (total <= 0) return 0;
    return int(blocked * 100 / total);
}

void EcoInterceptor::noteBlock(EcoCategory cat)
{
    m_byCat[static_cast<int>(cat)].fetchAndAddRelaxed(1);
    m_blockedCount.fetchAndAddRelaxed(1);
    // Un signal par requete bloquee, emis depuis le thread IO, sature la file
    // d'evenements sur une page lourde. On ne signale que la PREMIERE requete
    // d'une rafale ; l'IHM regroupe les suivantes dans son timer.
    if (m_statsDirty.fetchAndStoreAcquire(1) == 0)
        emit statsChanged(m_blockedCount.loadRelaxed());
}

bool EcoInterceptor::isChallengeHost(const QString &host)
{
    // Pages de verification anti-bot des moteurs de recherche.
    static const QStringList hosts = {
        QStringLiteral("duckduckgo.com"), QStringLiteral("html.duckduckgo.com"),
        QStringLiteral("lite.duckduckgo.com"), QStringLiteral("searx.be"),
        QStringLiteral("www.mojeek.com"), QStringLiteral("mojeek.com"),
        QStringLiteral("search.inetol.net"), QStringLiteral("priv.au"),
    };
    for (const QString &h : hosts)
        if (host == h || host.endsWith(QLatin1Char('.') + h)) return true;
    return false;
}

bool EcoInterceptor::isChallengeUrl(const QUrl &url)
{
    const QString path = url.path();
    if (path.contains(QStringLiteral("anomaly")) || path.contains(QStringLiteral("captcha"))
        || path.contains(QStringLiteral("challenge")) || path.contains(QStringLiteral("verify")))
        return true;
    // Un fichier imageCharge d'un moteur sert rarement a autre chose qu'au captcha
    return false;
}

bool EcoInterceptor::isExemptFromBlocking(bool allowChallenge, const QString &firstPartyHost)
{
    // Une page de verification anti-bot DOIT rester lisible : sans les images
    // du captcha, l'utilisateur ne peut pas la resoudre et le navigateur
    // devient inutilisable. C'est le seul cas ou l'eco passe au second plan.
    return allowChallenge && isChallengeHost(firstPartyHost);
}

void EcoInterceptor::interceptRequest(QWebEngineUrlRequestInfo &info) {
    QUrl url = info.requestUrl();
    QString urlStr = url.toString();
    auto type = info.resourceType();

    // En-tetes de requete poses UNE seule fois, avant toute decision : ils
    // s'appliquent aussi aux pages de verification anti-bot (a qui on laisse
    // tous les autres bloqueurs). Un seul endroit a maintenir, et les deux
    // signaux sont testes isolement (fonction pure privacyHeaders()).
    const bool dataSaverOrUltra = m_dataSaver || m_ultraEco;
    if (dataSaverOrUltra || m_secGpc) {
        // Un en-tete HTTP n'a de sens que sur un vrai schema reseau : poser
        // Save-Data sur file: ou data: provoque un avertissement inutile.
        const QString scheme = url.scheme();
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
            const auto headers = privacyHeaders(dataSaverOrUltra, m_secGpc);
            for (const auto &kv : headers) info.setHttpHeader(kv.first, kv.second);
        }
    }

    // Page de verification anti-bot : on ne bloque RIEN. Sans les images du
    // captcha, la page est insoluble et l'utilisateur est piege.
    if (isExemptFromBlocking(m_allowChallengeImages, info.firstPartyUrl().host())) {
        m_allowedCount.fetchAndAddRelaxed(1);
        return;
    }

    // Ne jamais bloquer la navigation principale (sinon clic résultat semble "ne marche pas")
    // Seule exception : Ultra bloque les flux vidéo directs en MainFrame si URL = fichier stream
    // (laisse HTML passer, bloque .m3u8/.mpd en navigation directe pour éviter 500Mo involontaires)
    bool isMainFrame = (type == QWebEngineUrlRequestInfo::ResourceTypeMainFrame);

    // 1. AdBlock — mais pas pour MainFrame ni pour téléchargements
    // Ne bloque pas les fichiers téléchargeables (.zip, .exe, .pdf, etc.) même si EasyList les marquerait.
    //
    // Deux corrections importantes :
    //  - l'extension est cherchee dans le CHEMIN (url.path()), pas dans l'URL
    //    entière : chercher « .pdf » dans la query string permettait de contourner
    //    les 114 000 règles avec « .../ads.js?a=.pdf ».
    //  - la regex est statique : la reconstruire coutait 156 µs par requête
    //    (1 µs une fois compilee), sur le thread IO de QtWebEngine.
    QString lowerUrl = urlStr.toLower();
    static const QRegularExpression downloadExt(
        QStringLiteral(R"(\.(zip|rar|7z|exe|msi|pdf|mp4|mkv|mp3|avi|iso|tar|gz|bz2|dmg|apk|appimage|deb|rpm)$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QString lowerPath = url.path().toLower();
    bool isDownloadFile = downloadExt.match(lowerPath).hasMatch();
    if (isDownloadFile) {
        qDebug() << "EcoInterceptor: allow download file" << urlStr.left(120);
        m_allowedCount.fetchAndAddRelaxed(1);
    } else if (m_adblocker && m_adblocker->shouldBlock(urlStr)) {
        if (isMainFrame) {
            // Log mais laisse passer la navigation principale
            qDebug() << "EcoInterceptor: mainFrame adblock would block but allowed:" << urlStr;
            m_allowedCount.fetchAndAddRelaxed(1);
        } else {
            qDebug() << "EcoInterceptor: blocked (adblock) type" << type << urlStr.left(120);
            info.block(true);
            // Pubs ou trackers selon le nom d'hote (estimation d'affichage)
            EcoCategory cat = EcoCategory::Pubs;
            if (lowerUrl.contains("analytics") || lowerUrl.contains("track")
                || lowerUrl.contains("pixel") || lowerUrl.contains("beacon")
                || lowerUrl.contains("metrics") || lowerUrl.contains("telemetry"))
                cat = EcoCategory::Trackers;
            noteBlock(cat);
            return;
        }
    } else {
        m_allowedCount.fetchAndAddRelaxed(1);
    }

    // 2. Non-essentiel si DataSaver ou Ultra
    if ((m_dataSaver || m_ultraEco)) {
        EcoCategory cat = EcoCategory::Other;
        if (isNonEssential(url, type, &cat)) {
            if (isMainFrame) {
                qDebug() << "EcoInterceptor: mainFrame non-essential would block but allowed:" << urlStr;
            } else {
                qDebug() << "EcoInterceptor: blocked (non-essential:" << int(cat) << ") type" << type << urlStr.left(120);
                info.block(true);
                noteBlock(cat);
                return;
            }
        }
    }

    // 2bis. Ultra-éco : bloque chunks vidéo/streaming (XHR/fetch) + beacons
    // Ne casse pas HTML/JS/CSS, bloque seulement flux lourds même si AdBlock les rate.
    if (m_ultraEco && !isMainFrame && !isDownloadFile) {
        // Chemin + query : les marqueurs de flux (mime=video, range=) sont
        // des parametres, l'extension est dans le chemin.
        const QString ls = lowerPath + QLatin1Char('?') + url.query().toLower();
        bool isStreamUrl = ls.contains(".m3u8") || ls.contains(".mpd")
            || ls.contains("videoplayback") || ls.contains("googlevideo.com")
            || ls.contains(".m4s") || ls.contains(".ts?") || ls.contains("seg-")
            || ls.contains("range=") || ls.contains("mime=video") || ls.contains("mime=audio");
        bool isHeavyStreamHost = ls.contains("googlevideo.com") || ls.contains("ytimg.com")
            || ls.contains(".twitch.tv") || ls.contains("ttvnw.net")
            || ls.contains("netflix") || ls.contains("tiktokcdn");
        // Ping / beacon (HyperlinkAuditing déjà off, mais bloque au cas où)
        bool isBeacon = (type == QWebEngineUrlRequestInfo::ResourceTypePing);
        if (isStreamUrl || isHeavyStreamHost || isBeacon) {
            qDebug() << "EcoInterceptor: blocked (ultra-stream) type" << type << urlStr.left(120);
            info.block(true);
            noteBlock(isBeacon ? EcoCategory::Trackers : EcoCategory::Stream);
            return;
        }
    }

    // 3. Images OFF — sauf si l'utilisateur a autorise ce site (exception
    //    memorisee). La decision porte sur le site visite, pas sur l'hote de
    //    l'image : les images viennent presque toujours d'un autre domaine.
    if (m_imagesOff && type == QWebEngineUrlRequestInfo::ResourceTypeImage
        && !isImageHostAllowed(info.firstPartyUrl().host())) {
        info.block(true);
        noteBlock(EcoCategory::Images);
        return;
    }
    // Debug navigation principale
    if (isMainFrame) {
        qDebug() << "EcoInterceptor: allow mainFrame type" << type << urlStr.left(150);
    }
}

bool EcoInterceptor::isNonEssential(const QUrl &url, QWebEngineUrlRequestInfo::ResourceType type, EcoCategory *cat) const {
    auto set = [cat](EcoCategory c){ if (cat) *cat = c; };

    // Fonts
    if (type == QWebEngineUrlRequestInfo::ResourceTypeFontResource) { set(EcoCategory::Fonts); return true; }
    // Media autoplay (audio/vidéo <video>/<audio> — YouTube MSE/XHR géré en plus par bloc ultra)
    if (type == QWebEngineUrlRequestInfo::ResourceTypeMedia) { set(EcoCategory::Media); return true; }
    // Ping / beacon = pure télémétrie, jamais essentiel
    if (type == QWebEngineUrlRequestInfo::ResourceTypePing) { set(EcoCategory::Trackers); return true; }

    QString s = url.toString().toLower();
    if (s.contains("dns-prefetch")) { set(EcoCategory::Prefetch); return true; }
    if (s.contains("preconnect") || s.contains("prefetch")) {
        if (type != QWebEngineUrlRequestInfo::ResourceTypeMainFrame) { set(EcoCategory::Prefetch); return true; }
    }
    // Google Fonts / TypeKit / social widgets si DataSaver
    if (s.contains("fonts.googleapis.com") || s.contains("fonts.gstatic.com") || s.contains("use.typekit.net")) { set(EcoCategory::Fonts); return true; }
    if (s.contains("connect.facebook.net") || s.contains("platform.twitter.com") || s.contains("linkedin.com/px")) { set(EcoCategory::Trackers); return true; }
    // Favicons : petit mais 1 requête par site/onglet — bloqué seulement en Ultra
    if (m_ultraEco && (s.endsWith("/favicon.ico") || s.contains("favicon") || s.contains("apple-touch-icon"))) { set(EcoCategory::Other); return true; }
    // Analytics lourds ratés par EasyList
    if (s.contains("google-analytics.com/collect") || s.contains("hotjar.com") || s.contains("fullstory.com")) { set(EcoCategory::Trackers); return true; }
    // pixels 1x1 souvent tracking mais on laisse AdBlocker gérer, pas bloquer tout 1x1
    return false;
}
