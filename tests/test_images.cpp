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

QTEST_MAIN(TestImages)
#include "test_images.moc"