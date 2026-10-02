/*
 * URL du serveur d'images : aller-retour, et surtout résistance à la forge.
 *
 * L'URL de notre serveur local est une vraie URL http://127.0.0.1 : une page
 * pourrait l'appeler elle-même. Sans signature, ce serait un SSRF (récupérer
 * une URL interne) doublé d'une fuite de contenu (lire la réponse par fetch()).
 * Ces tests vérifient que ces URL forgées sont rejetées.
 */
#include <QtTest>
#include <QUrl>
#include <QUrl>
#include <QUrlQuery>

#include "services/ecoimageurl.h"
#include "services/imagecodec.h"

// Notre serveur, tel qu'il tourne réellement.
static QUrl baseServeur()
{
    return QUrl(QStringLiteral("http://127.0.0.1:39421/i"));
}

class TestEcoImageUrl : public QObject {
    Q_OBJECT
private slots:
    void allerRetourSimple();
    void allerRetourUrlLongue();          // au-delà de la limite de 255 d'un hôte
    void encodeRefuseLesSchemesNonHttp();
    void urlSansSignatureEstRejetee();
    void signatureFalsifieeEstRejetee();  // une page qui tente de forger
    void parametresAlteresSontRejetes(); // w/q modifiés après signature
    void chargeUtileAltereeEstRejetee(); // charge utile d'une autre URL
    void autreServeurEstRejete();
    void parametresManquantsSontRejetes();
    void bornesAppliquees();
    void reniflageTypeMime();
};

void TestEcoImageUrl::allerRetourSimple()
{
    const QUrl original(QStringLiteral("https://cdn.exemple.fr/photo.jpg"));
    const QUrl eco = EcoImageUrl::encode(baseServeur(), original, 800, 65);
    QVERIFY(!eco.isEmpty());
    QCOMPARE(eco.host(), QStringLiteral("127.0.0.1"));
    QCOMPARE(eco.port(), 39421);

    const EcoImageUrl::Target t = EcoImageUrl::decode(eco, baseServeur());
    QVERIFY(t.valid);
    QCOMPARE(t.original, original);
    QCOMPARE(t.targetWidth, 800);
    QCOMPARE(t.quality, 65);
}

void TestEcoImageUrl::allerRetourUrlLongue()
{
    // Une URL d'image avec ses paramètres dépasse facilement 400 caractères :
    // elle doit survivre au aller-retour (elle part dans la requête, pas l'hôte).
    QUrl original(QStringLiteral("https://images.exemple.fr/very/long/path/"));
    original.setPath(original.path() + QString(600, QLatin1Char('a')) + QStringLiteral(".jpg"));
    original.setQuery(QStringLiteral("w=1600&h=900&fit=crop&q=80&sig=%1").arg(QString(120, QLatin1Char('f'))));
    QVERIFY(original.toEncoded().size() > 400);

    const EcoImageUrl::Target t =
        EcoImageUrl::decode(EcoImageUrl::encode(baseServeur(), original, 1600, 70), baseServeur());
    QVERIFY(t.valid);
    QCOMPARE(t.original.toEncoded(), original.toEncoded());
    QCOMPARE(t.quality, 70);
}

void TestEcoImageUrl::encodeRefuseLesSchemesNonHttp()
{
    // Ni file: (disque local), ni data:, ni schéma interne : rien de ce qui
    // n'est pas déjà sur le réseau ne doit passer par le codec.
    for (const QString &s : {QStringLiteral("file:///etc/passwd"),
                             QStringLiteral("data:image/png;base64,AAAA"),
                             QStringLiteral("ftp://a.fr/x.png"),
                             QStringLiteral("chrome://settings"),
                             QStringLiteral("qrc:/x.png")})
        QVERIFY2(EcoImageUrl::encode(baseServeur(), QUrl(s), 800, 65).isEmpty(), qPrintable(s));
    QVERIFY(EcoImageUrl::encode(baseServeur(), QUrl(), 800, 65).isEmpty());
    QVERIFY(EcoImageUrl::encode(QUrl(), QUrl(QStringLiteral("https://a.fr/x.png")), 800, 65)
                .isEmpty());
}

void TestEcoImageUrl::urlSansSignatureEstRejetee()
{
    // Exactement ce qu'une page pourrait fabriquer elle-même.
    QUrl forged = baseServeur();
    const QUrl source(QStringLiteral("http://127.0.0.1:8080/admin/secret.png"));
    const QByteArray payload = source.toEncoded().toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    forged.setQuery(QStringLiteral("u=%1&s=%2&w=800&q=65")
                        .arg(QString::fromLatin1(payload), QStringLiteral("deadbeef")));
    const EcoImageUrl::Target t = EcoImageUrl::decode(forged, baseServeur());
    QVERIFY2(!t.valid, "une URL écrite à la main ne doit jamais être servie");
}

void TestEcoImageUrl::signatureFalsifieeEstRejetee()
{
    const QUrl eco = EcoImageUrl::encode(baseServeur(), QUrl(QStringLiteral("https://a.fr/x.png")),
                                         800, 65);
    const QString bonne = EcoImageUrl::sign(QUrlQuery(eco).queryItemValue(QStringLiteral("u"), QUrl::FullyDecoded).toLatin1(),
                                            800, 65);
    for (const QByteArray &bad : {QByteArrayLiteral("00000000"), QByteArrayLiteral("deadbeef"),
                                  bonne.toLatin1() + '0', QByteArray()}) {
        QUrl forged = eco;
        QUrlQuery q(forged);
        q.removeQueryItem(QStringLiteral("s"));
        q.addQueryItem(QStringLiteral("s"), QString::fromLatin1(bad));
        forged.setQuery(q);
        QVERIFY2(!EcoImageUrl::decode(forged, baseServeur()).valid,
                 "signature falsifiée acceptée");
    }
}

void TestEcoImageUrl::parametresAlteresSontRejetes()
{
    const QUrl eco = EcoImageUrl::encode(baseServeur(), QUrl(QStringLiteral("https://a.fr/x.png")),
                                         800, 65);
    // w et q sont signés : les modifier après coup doit invalider l'URL (sinon
    // une page pourrait demander une largeur énorme, ou la qualité 0).
    const QString u = QUrlQuery(eco).queryItemValue(QStringLiteral("u"), QUrl::FullyDecoded);
    const QString s = QUrlQuery(eco).queryItemValue(QStringLiteral("s"), QUrl::FullyDecoded);
    for (const QString &q : {QStringLiteral("w=1600&q=65"), QStringLiteral("w=800&q=10"),
                             QStringLiteral("w=0&q=0"), QStringLiteral("w=1&q=1")}) {
        QUrl forged = baseServeur();
        forged.setQuery(QStringLiteral("u=%1&s=%2&%3").arg(u, s, q));
        QVERIFY2(!EcoImageUrl::decode(forged, baseServeur()).valid,
                 qPrintable(QStringLiteral("query accepté : ") + q));
    }
}

void TestEcoImageUrl::chargeUtileAltereeEstRejetee()
{
    // On prend une URL bien signée, puis on remplace l'image demandée par une
    // autre (typiquement une adresse interne) : la signature ne suit pas.
    const QUrl eco = EcoImageUrl::encode(baseServeur(), QUrl(QStringLiteral("https://a.fr/photo.jpg")),
                                         800, 65);
    const QByteArray interne =
        QUrl(QStringLiteral("http://127.0.0.1:8080/admin/secret.png")).toEncoded()
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrl forged = baseServeur();
    // On garde la signature de photo.jpg, on remplace la cible : la signature ne
    // suit pas, la requête doit être refusée SANS toucher au réseau.
    forged.setQuery(QStringLiteral("u=%1&s=%2&w=800&q=65")
                        .arg(QString::fromLatin1(interne),
                             QUrlQuery(eco).queryItemValue(QStringLiteral("s"), QUrl::FullyDecoded)));
    QVERIFY2(!EcoImageUrl::decode(forged, baseServeur()).valid,
             "une charge utile remplacée doit être rejetée (sinon SSRF)");
}

void TestEcoImageUrl::autreServeurEstRejete()
{
    const QUrl eco = EcoImageUrl::encode(baseServeur(), QUrl(QStringLiteral("https://a.fr/x.png")),
                                         800, 65);
    // URL parfaitement signée, mais envoyée à quelqu'un d'autre : on ne la sert
    // que si l'hôte ET le port sont les nôtres.
    QVERIFY(!EcoImageUrl::decode(eco, QUrl(QStringLiteral("http://127.0.0.1:9999/i"))).valid);
    QVERIFY(!EcoImageUrl::decode(eco, QUrl(QStringLiteral("http://localhost:39421/i"))).valid);

    const EcoImageUrl::Target t = EcoImageUrl::decode(eco, baseServeur());
    QVERIFY(t.valid);
    QVERIFY(!EcoImageUrl::decode(QUrl(), baseServeur()).valid);
    QVERIFY(!EcoImageUrl::decode(eco, QUrl()).valid);
}

void TestEcoImageUrl::parametresManquantsSontRejetes()
{
    const QUrl eco = EcoImageUrl::encode(baseServeur(), QUrl(QStringLiteral("https://a.fr/x.png")),
                                         800, 65);
    const QString u = QUrlQuery(eco).queryItemValue(QStringLiteral("u"), QUrl::FullyDecoded);
    const QString s = QUrlQuery(eco).queryItemValue(QStringLiteral("s"), QUrl::FullyDecoded);
    for (const QString &q : {QString(), QStringLiteral("w=&q="),
                             QStringLiteral("w=abc&q=def"),
                             QStringLiteral("s=%1&w=800&q=65").arg(s)}) {
        QUrl forged = baseServeur();
        forged.setQuery(q);
        QVERIFY2(!EcoImageUrl::decode(forged, baseServeur()).valid,
                 qPrintable(QStringLiteral("query accepté : ") + q));
    }
    QVERIFY(EcoImageUrl::decode(eco, baseServeur()).valid);   // sanction : l'URL valide passe
}

void TestEcoImageUrl::bornesAppliquees()
{
    const QUrl u(QStringLiteral("https://a.fr/x.png"));
    // Valeurs folles : elles sont ramenées dans les bornes du codec, sinon une
    // URL signée pourrait demander une largeur de 10 millions de pixels.
    auto t = EcoImageUrl::decode(EcoImageUrl::encode(baseServeur(), u, 10'000'000, 5000),
                                 baseServeur());
    QVERIFY(t.valid);
    QCOMPARE(t.targetWidth, ImageCodec::kMaxWidth);
    QCOMPARE(t.quality, ImageCodec::kMaxQuality);

    t = EcoImageUrl::decode(EcoImageUrl::encode(baseServeur(), u, -50, -20), baseServeur());
    QVERIFY(t.valid);
    QCOMPARE(t.targetWidth, 0);          // 0 = taille d'origine
    QCOMPARE(t.quality, 0);              // 0 = pas de compression
}

void TestEcoImageUrl::reniflageTypeMime()
{
    const auto png = QByteArray::fromHex("89504e470d0a1a0a0000000d49484452");
    const auto jpeg = QByteArray::fromHex("ffd8ffe000104a464946");
    QCOMPARE(ImageCodec::guessContentType(png), QStringLiteral("image/png"));
    QCOMPARE(ImageCodec::guessContentType(jpeg), QStringLiteral("image/jpeg"));
    QCOMPARE(ImageCodec::guessContentType(QByteArray("GIF89a......")), QStringLiteral("image/gif"));
    QCOMPARE(ImageCodec::guessContentType(QByteArray("BM.......")), QStringLiteral("image/bmp"));
    QCOMPARE(ImageCodec::guessContentType(QByteArray("RIFF1234WEBPVP8 ")), QStringLiteral("image/webp"));
    QCOMPARE(ImageCodec::guessContentType(QByteArray("<svg xmlns=")), QStringLiteral("image/svg+xml"));
    // Du HTML et du vide ne doivent surtout pas être annoncés comme une image.
    QCOMPARE(ImageCodec::guessContentType(QByteArray("<!DOCTYPE html><html>")), QString());
    QCOMPARE(ImageCodec::guessContentType(QByteArray()), QString());
}

QTEST_MAIN(TestEcoImageUrl)
#include "test_ecoimageurl.moc"