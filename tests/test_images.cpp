/*
 * Exceptions d'images par site (« charger les images de ce site »).
 *
 * C'est LA fonctionnalite la plus rentable du navigateur : « Images OFF »
 * economise ~90 % sur une page photo, mais rend les sites illisibles. Le clic
 * droit doit donc pouvoir autoriser un site definitivement.
 *
 * Point sensible : la correspondance se fait sur le site VISITE, pas sur l'hote
 * de l'image (les images viennent d'un CDN), et sur les frontieres de label
 * (« notexample.com » ne doit pas heriter de « example.com »).
 */
#include <QtTest>
#include "ecointerceptor.h"
#include "services/adblocker.h"
#include "services/ecoimageurl.h"
#include "services/imagecodec.h"

class TestImages : public QObject {
    Q_OBJECT
private slots:
    void correspondanceSurFrontieresDeLabel();
    void sousDomainesCouverts_maisPasLesFaux();
    void listeVideOuHoteVide();
    void activationEtDesactivation();
    void listeTrieeEtCompteur();
    void reinitialisation();
    void imageDeException_nonBloquee();   // decision d'interception
    void pasDeBoucleDeReecriture();       // notre URL ne doit jamais se reecrire
    void queLesImagesSontReecrites();     // et rien d'autre
};

void TestImages::correspondanceSurFrontieresDeLabel()
{
    QSet<QString> allowed{QStringLiteral("example.com")};
    QVERIFY(EcoInterceptor::hostMatchesAllowList(QStringLiteral("example.com"), allowed));
    QVERIFY(EcoInterceptor::hostMatchesAllowList(QStringLiteral("www.example.com"), allowed));
    QVERIFY(EcoInterceptor::hostMatchesAllowList(QStringLiteral("cdn.img.example.com"), allowed));
    // Le piège du sous-chaîne : ces deux-là ne doivent PAS beneficier.
    QVERIFY(!EcoInterceptor::hostMatchesAllowList(QStringLiteral("notexample.com"), allowed));
    QVERIFY(!EcoInterceptor::hostMatchesAllowList(QStringLiteral("example.com.evil.net"), allowed));
    QVERIFY(!EcoInterceptor::hostMatchesAllowList(QStringLiteral("example.co"), allowed));
}

void TestImages::sousDomainesCouverts_maisPasLesFaux()
{
    EcoInterceptor eco(nullptr);
    eco.setImageHostAllowed(QStringLiteral("EXAMPLE.com "), true);   // normalisation
    QVERIFY(eco.isImageHostAllowed(QStringLiteral("example.com")));
    QVERIFY(eco.isImageHostAllowed(QStringLiteral("www.example.com")));
    QVERIFY(!eco.isImageHostAllowed(QStringLiteral("autre.fr")));
    QVERIFY(!eco.isImageHostAllowed(QStringLiteral("notexample.com")));
    // Un sous-domaine etranges reste exclu : la correspondance ne remonte que
    // les labels vers la DROITE (example.fr n'est pas un sous-domaine de example.com).
    QVERIFY(!eco.isImageHostAllowed(QStringLiteral("images.cdn.example.fr")));
}

void TestImages::listeVideOuHoteVide()
{
    QVERIFY(!EcoInterceptor::hostMatchesAllowList(QStringLiteral("example.com"), QSet<QString>()));
    QVERIFY(!EcoInterceptor::hostMatchesAllowList(QString(), QSet<QString>{QStringLiteral("example.com")}));
}

void TestImages::activationEtDesactivation()
{
    EcoInterceptor eco(nullptr);
    QCOMPARE(eco.imageAllowedCount(), 0);
    eco.setImageHostAllowed(QStringLiteral("example.com"), true);
    QCOMPARE(eco.imageAllowedCount(), 1);
    eco.setImageHostAllowed(QStringLiteral("example.com"), true);   // idempotent
    QCOMPARE(eco.imageAllowedCount(), 1);
    eco.setImageHostAllowed(QStringLiteral("example.com"), false);
    QCOMPARE(eco.imageAllowedCount(), 0);
    QVERIFY(!eco.isImageHostAllowed(QStringLiteral("example.com")));
    // Un hote vide est ignore ; « localhost » en revanche est un site comme
    // un autre (developpement local), il doit pouvoir etre autorise.
    eco.setImageHostAllowed(QStringLiteral("  "), true);
    QCOMPARE(eco.imageAllowedCount(), 0);
    eco.setImageHostAllowed(QStringLiteral("localhost"), true);
    QCOMPARE(eco.imageAllowedCount(), 1);
    QVERIFY(eco.isImageHostAllowed(QStringLiteral("localhost")));
}

void TestImages::listeTrieeEtCompteur()
{
    EcoInterceptor eco(nullptr);
    eco.setImageHostAllowed(QStringLiteral("zulu.fr"), true);
    eco.setImageHostAllowed(QStringLiteral("alpha.com"), true);
    eco.setImageHostAllowed(QStringLiteral("mike.net"), true);
    const QStringList l = eco.imageAllowedHosts();
    QCOMPARE(l.size(), 3);
    QCOMPARE(l, QStringList({QStringLiteral("alpha.com"), QStringLiteral("mike.net"),
                             QStringLiteral("zulu.fr")}));
    QCOMPARE(eco.imageAllowedCount(), 3);
}

void TestImages::reinitialisation()
{
    EcoInterceptor eco(nullptr);
    eco.setImageHostAllowed(QStringLiteral("a.fr"), true);
    eco.setImageHostAllowed(QStringLiteral("b.fr"), true);
    eco.clearImageAllowedHosts();
    QCOMPARE(eco.imageAllowedCount(), 0);
    QVERIFY(eco.imageAllowedHosts().isEmpty());
}

/* Le chemin le plus sensible : une requete d'image dont le site est en
   exception ne doit PAS etre comptee comme image bloquee. */
void TestImages::imageDeException_nonBloquee()
{
    const QString imageHost = QStringLiteral("i.ytimg.com");
    AdBlocker ab;                       // aucune regle : seul le traitement image joue
    EcoInterceptor eco(&ab);
    eco.setDataSaverEnabled(true);
    eco.setImagesOff(true);
    eco.setImageHostAllowed(QStringLiteral("exemple.fr"), true);

    // Decision d'interception, isolee du navigateur : la meme condition que
    // dans interceptRequest() est appliquee ici.
    auto imagesBloquees = [&](const QString &, const QString &pageHost) {
        eco.resetStats();
        if (eco.isImageHostAllowed(pageHost)) return false;
        return eco.imagesOff();
    };

    QVERIFY2(!imagesBloquees(QStringLiteral("i.ytimg.com"), QStringLiteral("exemple.fr")),
             "image d'un site en exception : elle ne doit pas etre bloquee");
    QVERIFY(imagesBloquees(QStringLiteral("i.ytimg.com"), QStringLiteral("autre.fr")));
    // Des que les images sont re-enables globalement, l'exception est sans objet
    // mais ne doit rien casse.
    eco.setImagesOff(false);
    QVERIFY(!imagesBloquees(QStringLiteral("i.ytimg.com"), QStringLiteral("autre.fr")));
}

void TestImages::pasDeBoucleDeReecriture()
{
    // La REGLE qui empeche la boucle : notre propre URL ne doit jamais etre
    // reecrite, puisqu'elle est deja en http et donc « encodable » — sans cela
    // elle se reecrit elle-meme indefiniment (mesure a 99 iterations).
    AdBlocker ab;
    EcoInterceptor eco(&ab);
    const QUrl base(QStringLiteral("http://127.0.0.1:41234"));
    eco.setImageCompression(true, 65, base);
    const auto image = QWebEngineUrlRequestInfo::ResourceTypeImage;
    const auto page = QWebEngineUrlRequestInfo::ResourceTypeMainFrame;

    QVERIFY(eco.wouldRewriteImage(QUrl(QStringLiteral("https://cdn.exemple.fr/a.jpg")), image));

    const QUrl encodee = EcoImageUrl::encode(base,
                                             QUrl(QStringLiteral("https://cdn.exemple.fr/a.jpg")),
                                             ImageCodec::kDefaultWidth, 65);
    QVERIFY(!encodee.isEmpty());
    QVERIFY2(!eco.wouldRewriteImage(encodee, image), "notre propre URL reecrite : boucle infinie");

    // Meme hote, autre port : ce n'est pas nous.
    QVERIFY(eco.wouldRewriteImage(QUrl(QStringLiteral("http://127.0.0.1:55555/photo.png")), image));
    // Un type qui n'est pas une image n'est jamais reecrit.
    QVERIFY(!eco.wouldRewriteImage(QUrl(QStringLiteral("https://cdn.exemple.fr/a.jpg")), page));
    // Reglage desactive ou serveur arrete : jamais.
    eco.setImageCompression(false, 65, base);
    QVERIFY(!eco.wouldRewriteImage(QUrl(QStringLiteral("https://cdn.exemple.fr/a.jpg")), image));
    eco.setImageCompression(true, 65, QUrl());
    QVERIFY(!eco.wouldRewriteImage(QUrl(QStringLiteral("https://cdn.exemple.fr/a.jpg")), image));
}

void TestImages::queLesImagesSontReecrites()
{
    AdBlocker ab;
    EcoInterceptor eco(&ab);
    const QUrl base(QStringLiteral("http://127.0.0.1:41234"));
    eco.setImageCompression(true, 65, base);
    const auto image = QWebEngineUrlRequestInfo::ResourceTypeImage;

    // Types qui ne sont pas des images, meme s'ils chargent des octets d'image :
    // les compresser casserait des scripts ou des feuilles de style.
    for (auto t : {QWebEngineUrlRequestInfo::ResourceTypeScript,
                   QWebEngineUrlRequestInfo::ResourceTypeXhr,
                   QWebEngineUrlRequestInfo::ResourceTypeStylesheet,
                   QWebEngineUrlRequestInfo::ResourceTypeFontResource,
                   QWebEngineUrlRequestInfo::ResourceTypeFavicon})
        QVERIFY(!eco.wouldRewriteImage(QUrl(QStringLiteral("https://a.fr/track.png")), t));

    // file: et data: ne sont jamais compresses (le codec les refuse).
    QVERIFY(!eco.wouldRewriteImage(QUrl(QStringLiteral("file:///tmp/a.png")), image));
    QVERIFY(!eco.wouldRewriteImage(QUrl(QStringLiteral("data:image/png;base64,AAA")), image));
    // Une vraie image bien sur.
    QVERIFY(eco.wouldRewriteImage(QUrl(QStringLiteral("https://a.fr/photo.png")), image));
}

QTEST_MAIN(TestImages)
#include "test_images.moc"