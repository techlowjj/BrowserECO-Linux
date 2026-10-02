/*
 * ImageCodec — politique de compression, testée seule (aucun réseau, aucun
 * navigateur, aucun QtWebEngine).
 *
 * Ce que ces tests verrouillent :
 *  - les refus (seuil, qualité 0, GIF/SVG, données illisibles) ;
 *  - le redimensionnement (plafond, jamais d'agrandissement, aspect) ;
 *  - le choix de format (WebP si le plugin existe, JPEG sinon) ;
 *  - l'invariant qui compte vraiment : **quand on dit compressé, c'est plus
 *    léger**. Jamais de dégradation, jamais de perte d'information.
 */
#include <QtTest>
#include <QBuffer>
#include <QRandomGenerator>

#include "services/imagecodec.h"

using namespace ImageCodec;

namespace {

// Image de test DETERMINISTE (le seed est fixe : le test doit etre reproductible).
// Une image a degrade bruite est la pire pour un encodeur : c'est ce qui garantit
// que les tests ne passent pas par hasard.
QImage bruit(int w, int h, quint32 seed = 12345)
{
    QImage img(w, h, QImage::Format_RGB32);
    QRandomGenerator g(seed);
    for (int y = 0; y < h; ++y) {
        QRgb *l = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
            l[x] = qRgb(g.bounded(256), g.bounded(256), g.bounded(256));
    }
    return img;
}

QByteArray encode(const QImage &img, const char *format, int quality = 90)
{
    QByteArray out;
    QBuffer b(&out);
    b.open(QIODevice::WriteOnly);
    img.save(&b, format, quality);
    b.close();
    return out;
}

} // namespace

class TestImageCodec : public QObject {
    Q_OBJECT
private slots:
    void refuseDonneesVides();
    void refuseTypeNonImage();
    void refusSousLeSeuil();
    void seuilNegatifNeContournePasLaGarde();
    void refuseQualiteZero();
    void refuseGifEtSvg();
    void refuseDonneesIllisibles();
    void compresseUneGrosseImage();
    void jamaisDeDegradation();
    void largeurBorneeAuPlafond();
    void jamaisDAgrandissement();
    void aspectRapportConserve();
    void formatChoisiSelonLePlugin();
    void typeMimeConformeAuFormat();
    void bornesDeQualite();
};

void TestImageCodec::refuseDonneesVides()
{
    Request r;
    r.data = QByteArray();
    r.contentType = QStringLiteral("image/png");
    QCOMPARE(compress(r).verdict, Verdict::NotAnImage);

    Request r2;
    r2.contentType = QStringLiteral("image/png");
    QCOMPARE(compress(r2).verdict, Verdict::NotAnImage);
}

void TestImageCodec::refuseTypeNonImage()
{
    const QByteArray png = encode(bruit(64, 64), "PNG");
    Request r;
    r.data = png;
    r.contentType = QStringLiteral("text/html");     // un site qui ment sur le type
    QCOMPARE(compress(r).verdict, Verdict::NotAnImage);
}

void TestImageCodec::refusSousLeSeuil()
{
    Request r;
    r.data = encode(bruit(16, 16), "PNG");          // quelques centaines d'octets
    r.contentType = QStringLiteral("image/png");
    r.minBytes = 10 * 1024;
    const Result res = compress(r);
    QCOMPARE(res.verdict, Verdict::TooSmall);
    QVERIFY(res.data.isEmpty());
}

void TestImageCodec::seuilNegatifNeContournePasLaGarde()
{
    // minBytes < 0 : il ne doit pas inhiber le test « données vides » ni
    // provoquer un comportement indéfini.
    Request r;
    r.data = QByteArray("pas une image");
    r.contentType = QStringLiteral("image/png");
    r.minBytes = -500;
    QCOMPARE(compress(r).verdict, Verdict::NotAnImage);

    Request r2;
    r2.data = encode(bruit(8, 8), "PNG");
    r2.contentType = QStringLiteral("image/png");
    r2.minBytes = -1;                                // seuil ramené à 0 : on tente
    const Result res = compress(r2);
    QVERIFY(res.verdict == Verdict::Compressed || res.verdict == Verdict::NotSmaller);
}

void TestImageCodec::refuseQualiteZero()
{
    Request r;
    r.data = encode(bruit(400, 300), "PNG");
    r.contentType = QStringLiteral("image/png");
    r.quality = 0;                                   // 0 = compression désactivée
    QCOMPARE(compress(r).verdict, Verdict::QualityDisabled);
}

void TestImageCodec::refuseGifEtSvg()
{
    // Le refus est une décision de FORMAT, fondé sur le seul content-type :
    // il doit être la raison rapportée même pour une petite image (sinon on
    // dirait à tort « trop petite »).
    // GIF 1x1 valide en dur : cette installation de Qt n'a pas d'encodeur GIF
    // (QImageWriter ne propose que png/jpeg/bmp/ico/xpm), on ne peut donc pas
    // en fabriquer un.
    const QByteArray gif1px = QByteArray::fromBase64(
        "R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7");
    QVERIFY(!gif1px.isEmpty());

    Request petitGif;
    petitGif.data = gif1px;
    petitGif.contentType = QStringLiteral("image/gif");
    QCOMPARE(compress(petitGif).verdict, Verdict::PasRecode);

    Request gif;
    gif.data = gif1px;
    gif.contentType = QStringLiteral("image/gif");   // QImage n'écrit pas de WebP animé
    gif.minBytes = 0;                                // seuil désactivé : le refus tient
    QCOMPARE(compress(gif).verdict, Verdict::PasRecode);

    Request svg;
    svg.data = QByteArray("<svg xmlns='http://www.w3.org/2000/svg'><rect/></svg>");
    svg.contentType = QStringLiteral("image/svg+xml");
    QCOMPARE(compress(svg).verdict, Verdict::PasRecode);
}

void TestImageCodec::refuseDonneesIllisibles()
{
    // Sans seuil, des octets arbitraires annoncés comme image doivent être
    // refusés au décodage.
    Request r;
    r.data = QByteArray("ceci n'est pas une image, juste des octets arbitraires");
    r.contentType = QStringLiteral("image/png");
    r.minBytes = 0;
    QCOMPARE(compress(r).verdict, Verdict::NotAnImage);

    // Avec le seuil par défaut, la raison remontée est l'économie de CPU :
    // c'est l'ordre voulu (décoder une image pour la jeter serait absurde).
    Request avecSeuil;
    avecSeuil.data = r.data;
    avecSeuil.contentType = QStringLiteral("image/png");
    QCOMPARE(compress(avecSeuil).verdict, Verdict::TooSmall);
}

void TestImageCodec::compresseUneGrosseImage()
{
    Request r;
    r.data = encode(bruit(2000, 1400), "PNG");      // bruité : incompressible en PNG
    r.contentType = QStringLiteral("image/png");
    r.targetWidth = 800;
    r.quality = 65;
    const Result res = compress(r);

    QCOMPARE(res.verdict, Verdict::Compressed);
    QCOMPARE(res.originalWidth, 2000);
    QCOMPARE(res.originalHeight, 1400);
    QCOMPARE(res.width, 800);
    QCOMPARE(res.height, 560);                        // 1400 * 800/2000
    QVERIFY2(res.data.size() < res.originalSize,
             qPrintable(QStringLiteral("la compression n'a pas réduit : %1 -> %2")
                            .arg(res.originalSize).arg(res.data.size())));
    QVERIFY(res.saves());
}

void TestImageCodec::jamaisDeDegradation()
{
    // Invariant central, sur plusieurs images et plusieurs réglages :
    // si le résultat est annoncé compressé, il est forcément plus léger.
    for (int i = 0; i < 6; ++i) {
        const int w = 200 + i * 260;
        const int h = 150 + i * 170;
        for (int q : {30, 55, 65, 85}) {
            for (int tw : {0, 320, 900, 1600, 5000}) {
                Request r;
                r.data = encode(bruit(w, h, quint32(i + 1)), "PNG");
                r.contentType = QStringLiteral("image/png");
                r.targetWidth = tw;
                r.quality = q;
                const Result res = compress(r);
                if (res.compressed()) {
                    QVERIFY2(res.data.size() < res.originalSize,
                             qPrintable(QStringLiteral("dégradation : %1x%2 q=%3 w=%4 : %5 -> %6")
                                            .arg(w).arg(h).arg(q).arg(tw)
                                            .arg(res.originalSize).arg(res.data.size())));
                    QVERIFY(res.contentType.startsWith("image/"));
                    QVERIFY(res.width > 0 && res.height > 0);
                }
            }
        }
    }
}

void TestImageCodec::largeurBorneeAuPlafond()
{
    Request r;
    r.data = encode(bruit(3000, 2000), "PNG");
    r.contentType = QStringLiteral("image/png");
    r.targetWidth = 99999;                           // absurdement grand
    const Result res = compress(r);
    QCOMPARE(res.width, kMaxWidth);
    QCOMPARE(res.height, int(qint64(2000) * kMaxWidth / 3000));
    QCOMPARE(clampWidth(5000), kMaxWidth);
    QCOMPARE(clampWidth(800), 800);
    QCOMPARE(clampWidth(0), 0);                      // 0 = pas de redimensionnement
    QCOMPARE(clampWidth(-10), 0);
}

void TestImageCodec::jamaisDAgrandissement()
{
    Request r;
    r.data = encode(bruit(320, 240), "PNG");         // déjà plus petit que la cible
    r.contentType = QStringLiteral("image/png");
    r.targetWidth = kMaxWidth;
    const Result res = compress(r);
    QCOMPARE(res.width, 320);
    QCOMPARE(res.height, 240);
}

void TestImageCodec::aspectRapportConserve()
{
    Request large;
    large.data = encode(bruit(2000, 1000), "PNG");  // 2:1
    large.contentType = QStringLiteral("image/png");
    large.targetWidth = 1000;
    Result res = compress(large);
    QCOMPARE(res.width, 1000);
    QCOMPARE(res.height, 500);

    Request portrait;
    portrait.data = encode(bruit(1000, 2000), "PNG"); // 1:2
    portrait.contentType = QStringLiteral("image/png");
    portrait.targetWidth = 400;
    res = compress(portrait);
    QCOMPARE(res.width, 400);
    QCOMPARE(res.height, 800);
}

void TestImageCodec::formatChoisiSelonLePlugin()
{
    Request r;
    r.data = encode(bruit(1600, 1200), "PNG");
    r.contentType = QStringLiteral("image/png");
    r.targetWidth = 800;
    r.quality = 65;

    const Result auto_ = compress(r);
    const QByteArray attendu = webpAvailable() ? QByteArrayLiteral("WEBP") : QByteArrayLiteral("JPEG");
    QCOMPARE(preferredFormat(), attendu);
    QCOMPARE(auto_.contentType, contentTypeForFormat(attendu));

    // Format forcé : le codec ne doit pas ignore l'ordre demande.
    const Result force = compress(r, QByteArrayLiteral("JPEG"));
    QCOMPARE(force.contentType, QByteArrayLiteral("image/jpeg"));
    if (force.compressed())
        QVERIFY(force.data.size() < force.originalSize);
}

void TestImageCodec::typeMimeConformeAuFormat()
{
    QCOMPARE(contentTypeForFormat(QByteArrayLiteral("WEBP")), QByteArrayLiteral("image/webp"));
    QCOMPARE(contentTypeForFormat(QByteArrayLiteral("webp")), QByteArrayLiteral("image/webp"));
    QCOMPARE(contentTypeForFormat(QByteArrayLiteral("JPEG")), QByteArrayLiteral("image/jpeg"));
    // Le résultat annoncé correspond bien à ce qui a été écrit : impossible de
    // servir des octets JPEG en annonceant image/webp (bug de l'ancien code,
    // où le content-type de sortie n'était jamais retourné).
    Request r;
    r.data = encode(bruit(1200, 900), "PNG");
    r.contentType = QStringLiteral("image/png");
    r.targetWidth = 700;
    const Result res = compress(r, QByteArrayLiteral("JPEG"));
    if (res.compressed()) {
        QImage verif;
        QVERIFY(verif.loadFromData(res.data));
        QCOMPARE(res.contentType, QByteArrayLiteral("image/jpeg"));
    }
}

void TestImageCodec::bornesDeQualite()
{
    QCOMPARE(clampQuality(-5), 0);
    QCOMPARE(clampQuality(0), 0);
    QCOMPARE(clampQuality(65), 65);
    QCOMPARE(clampQuality(85), 85);
    QCOMPARE(clampQuality(1000), kMaxQuality);
    QCOMPARE(clampQuality(86), kMaxQuality);

    // Une qualité hors bornes est ramenée, elle ne doit pas faire échouer l'appel.
    Request r;
    r.data = encode(bruit(800, 600), "PNG");
    r.contentType = QStringLiteral("image/png");
    r.targetWidth = 400;
    r.quality = 1000;
    const Result res = compress(r);
    QVERIFY(res.verdict == Verdict::Compressed || res.verdict == Verdict::NotSmaller);
}

QTEST_MAIN(TestImageCodec)
#include "test_imagecodec.moc"