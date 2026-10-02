/*
 * Cache mémoire des images compressées + garde anti-boucle.
 *
 * Le cache doit être borné en OCTETS (une photo de 1,5 Mo ne doit pas occuper la
 * même place qu'une vignette), évictor en LRU, et être sûr sous accès
 * concurrent (le gestionnaire travaille sur le thread réseau pendant que l'IHM
 * lit les compteurs).
 *
 * La garde anti-boucle de réécriture, elle, est testée dans test_images.cpp
 * (EcoInterceptor::wouldRewriteImage).
 */
#include <QtTest>
#include <QByteArray>
#include <QThread>
#include <atomic>

#include "services/imagecache.h"

class TestImageCache : public QObject {
    Q_OBJECT
private slots:
    void missPuisHit();
    void laCleInclutLaTransformation();
    void entreeVideIgnoree();
    void budgetEnOctetsEtEvictionLru();
    void compteursEtOctetsEconomises();
    void effacer();
    void threadedSansCourse();
};

void TestImageCache::missPuisHit()
{
    ImageCache c(1 << 20);
    ImageCache::Entry out;
    QVERIFY2(!c.get("k", out), "un cache vide ne doit jamais répondre");
    QCOMPARE(c.misses(), 1);
    QCOMPARE(c.hits(), 0);

    c.put("k", QByteArray("octets"), QByteArray("image/jpeg"));
    QVERIFY(c.get("k", out));
    QCOMPARE(out.data, QByteArray("octets"));
    QCOMPARE(out.contentType, QByteArray("image/jpeg"));
    QCOMPARE(c.hits(), 1);
    QCOMPARE(c.count(), 1);
}

void TestImageCache::laCleInclutLaTransformation()
{
    // Une même URL servie en 800 px puis en 1600 px doit donner deux entrées,
    // sinon la seconde renverrait la première (image trop petite pour l'écran).
    const QString u = QStringLiteral("https://exemple.fr/a.jpg");
    const QString k800 = ImageCache::keyFor(u, 800, 65, QByteArrayLiteral("JPEG"));
    const QString k1600 = ImageCache::keyFor(u, 1600, 65, QByteArrayLiteral("JPEG"));
    const QString kWebp = ImageCache::keyFor(u, 800, 65, QByteArrayLiteral("WEBP"));
    const QString kQ80 = ImageCache::keyFor(u, 800, 80, QByteArrayLiteral("JPEG"));
    QVERIFY(k800 != k1600);
    QVERIFY(k800 != kWebp);
    QVERIFY(k800 != kQ80);
    QCOMPARE(k800, ImageCache::keyFor(u, 800, 65, QByteArrayLiteral("JPEG")));

    ImageCache c(1 << 20);
    ImageCache::Entry e;
    c.put(k800, QByteArray("petite"), QByteArray("image/jpeg"));
    c.put(k1600, QByteArray("grande"), QByteArray("image/jpeg"));
    QVERIFY(c.get(k800, e)); QCOMPARE(e.data, QByteArray("petite"));
    QVERIFY(c.get(k1600, e)); QCOMPARE(e.data, QByteArray("grande"));
    QCOMPARE(c.count(), 2);
}

void TestImageCache::entreeVideIgnoree()
{
    ImageCache c(1 << 20);
    c.put("vide", QByteArray(), QByteArray("image/jpeg"));
    QCOMPARE(c.count(), 0);
    ImageCache::Entry e;
    QVERIFY(!c.get("vide", e));
}

void TestImageCache::budgetEnOctetsEtEvictionLru()
{
    // Budget de 120 Ko, deux entrées de 100 Ko : une seule doit tenir à la fois.
    ImageCache c(120 * 1024);
    c.put("a", QByteArray(100 * 1024, 'a'), QByteArray("image/jpeg"));
    QCOMPARE(c.count(), 1);
    QCOMPARE(c.usedBytes(), qint64(100 * 1024));

    c.put("b", QByteArray(100 * 1024, 'b'), QByteArray("image/jpeg"));
    ImageCache::Entry e;
    QVERIFY2(!c.get("a", e), "l'éviction LRU a échoué : « a » devait sortir");
    QVERIFY(c.get("b", e));

    // « a » est de nouveau insérable après éviction (le cache reste vivant).
    c.put("a", QByteArray(10 * 1024, 'a'), QByteArray("image/jpeg"));
    QVERIFY(c.get("a", e));

    // Le coût suit les OCTETS, pas le nombre d'entrées : 20 entrées de 10 Ko
    // dans 400 Ko ne doivent pas dépasser le budget.
    ImageCache petit(400 * 1024);
    for (int i = 0; i < 20; ++i)
        petit.put(QStringLiteral("k%1").arg(i), QByteArray(10 * 1024, 'x'), QByteArray("image/png"));
    QVERIFY2(petit.usedBytes() <= petit.budget(),
             qPrintable(QStringLiteral("budget dépassé : %1 > %2")
                            .arg(petit.usedBytes()).arg(petit.budget())));
}

void TestImageCache::compteursEtOctetsEconomises()
{
    ImageCache c(1 << 20);
    c.put("k", QByteArray(2048, 'z'), QByteArray("image/webp"));
    ImageCache::Entry e;
    c.get("k", e);
    c.get("k", e);
    c.get("absent", e);
    QCOMPARE(c.hits(), 2);
    QCOMPARE(c.misses(), 1);
    // Deux réponses servies depuis le cache = 4 Ko non retéléchargés.
    QCOMPARE(c.savedBytes(), qint64(4096));
}

void TestImageCache::effacer()
{
    ImageCache c(1 << 20);
    c.put("k", QByteArray("x"), QByteArray("image/jpeg"));
    QCOMPARE(c.count(), 1);
    c.clear();
    QCOMPARE(c.count(), 0);
    QCOMPARE(c.usedBytes(), qint64(0));
}

void TestImageCache::threadedSansCourse()
{
    // Le gestionnaire d'images travaille sur le thread réseau pendant que l'IHM
    // lit les compteurs : le mutex doit tenir. On vérifie la cohérence logique
    // (compteurs, budget) ; l'absence de course elle-même se mesure avec
    // ThreadSanitizer, pas ici.
    ImageCache c(512 * 1024);
    constexpr int threads = 4;
    constexpr int tours = 300;
    constexpr int clesParThread = 20;
    std::atomic<int> lus{0};
    QVector<QThread *> ts;
    for (int t = 0; t < threads; ++t) {
        ts << QThread::create([&c, t, tours, clesParThread, &lus]{
            for (int i = 0; i < tours; ++i) {
                const QString k = QStringLiteral("t%1-%2").arg(t).arg(i % clesParThread);
                c.put(k, QByteArray(1024, 'a'), QByteArray("image/jpeg"));
                ImageCache::Entry e;
                if (c.get(k, e)) ++lus;
                Q_UNUSED(c.hits());
                Q_UNUSED(c.misses());
                Q_UNUSED(c.usedBytes());
            }
        });
    }
    for (QThread *th : ts) th->start();
    for (QThread *th : ts) th->wait();
    for (QThread *th : ts) delete th;

    // 4 fils x 20 clés x 1 Ko = 80 Ko, très en dessous du budget : chaque put
    // est donc suivi d'un get réussi.
    QCOMPARE(lus.load(), threads * tours);
    QCOMPARE(c.hits() + c.misses(), qint64(threads * tours));
    QVERIFY2(c.usedBytes() <= c.budget(), "budget dépassé après écriture concurrente");
}


QTEST_MAIN(TestImageCache)
#include "test_imagecache.moc"