/*
 * Chargement d'images a la demande (lazy loading) — test de bout en bout.
 *
 * Verifie le comportement REEL dans un vrai navigateur contre un vrai serveur :
 *  1. avec le reglage actif, le serveur ne voit AUCUNE requete d'image
 *     (les images sont differees) ;
 *  2. un clic sur un placeholder declenche bien le chargement de l'image
 *     reelle (c'est le point le plus risque : avec AutoLoadImages desactive,
 *     restaurer src par script suffit-il ?) ;
 *  3. le compteur de la page est correct ;
 *  4. une page sans images ne casse pas ;
 *  5. le reglage desactive = comportement normal (les images se chargent).
 */
#include <QtTest>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QWebEngineView>
#include <QElapsedTimer>

#include "mainwindow.h"
#include "browserpage.h"
#include <QSignalSpy>

class TestLazyImages : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void imagesDiffereesSansTelechargement();
    void clicChargeLImageReelle();
    void pageSansImagesNeBougePas();
    void compteurCorrect();
    void ressourceScriptEstDisponible();
    void reglageDesactiveChargeNormalement();

private:
    QWebEngineView *vue();
    void activer(bool on);
    void attendreDocument();   // attend le document de la version courante
    QVariant attendre(const QString &js,
                      const std::function<bool(const QVariant &)> &ok, int msMax = 20000);
    int requetesImage() const;

    QTemporaryDir *m_dir = nullptr;
    QTcpServer *m_server = nullptr;
    MainWindow *m_win = nullptr;
    int m_port = 0;
    QByteArray m_photo;
    QStringList m_requetes;
    int m_version = 0;
};

QWebEngineView *TestLazyImages::vue()
{
    auto *tabs = m_win->findChild<QTabWidget *>();
    return tabs ? qobject_cast<QWebEngineView *>(tabs->currentWidget()) : nullptr;
}

int TestLazyImages::requetesImage() const
{
    int n = 0;
    for (const QString &r : m_requetes)
        if (r.startsWith(QStringLiteral("/photo.png"))) ++n;
    return n;
}

/* Attend le document de la version courante (sinon on lit l'image reportee du
 * chargement precedent et le test passe ou echoue pour la mauvaise raison). */
void TestLazyImages::attendreDocument()
{
    attendre(QStringLiteral("document.body && document.body.dataset.v === '%1' ? 1 : -1")
                 .arg(m_version),
             [](const QVariant &v) { return v.toInt() == 1; });
}

void TestLazyImages::activer(bool on)
{
    QMetaObject::invokeMethod(m_win, "toggleLazyImages", Qt::DirectConnection, Q_ARG(bool, on));
}

QVariant TestLazyImages::attendre(const QString &js,
                                  const std::function<bool(const QVariant &)> &ok, int msMax)
{
    QVariant dernier;
    QElapsedTimer t;
    t.start();
    bool obtenu = false;
    while (!obtenu && t.elapsed() < msMax) {
        QVariant v;
        bool recu = false;
        vue()->page()->runJavaScript(js, [&](const QVariant &r) { v = r; recu = true; });
        for (int i = 0; i < 6 && !recu; ++i) QTest::qWait(50);
        if (!recu) continue;
        dernier = v;
        if (ok(v)) obtenu = true;
        else QTest::qWait(100);
    }
    return dernier;
}

/* GARDE-FOU contre l'echec SILENCIEUX.
 *
 * La ressource Qt est compilee dans une bibliotheque statique : si le linker
 * jette l'objet qui la contient, le script est introuvable, aucune erreur n'est
 * levee, et la fonctionnalite ne fait RIEN. Ce test echoue bruyamment dans ce
 * cas, ce qu'un test fonctionnel seul ne garantit pas (il peut passer pour une
 * autre raison).
 *
 * On ne cherche PAS le contenu avec `strings` : Qt stocke les noms de ressources
 * en UTF-16, que `strings` en ASCII ne voit pas (faux negatif deja observe).
 */
void TestLazyImages::ressourceScriptEstDisponible()
{
    QFile f(QStringLiteral(":/lazyload.js"));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             "la ressource :/lazyload.js est absente : le chargement d'images "
             "serait silencieusement inoperant");
    const QByteArray contenu = f.readAll();
    QVERIFY(contenu.size() > 500);
    QVERIFY(contenu.contains("data-browsereco-src"));
    QVERIFY(contenu.contains("MutationObserver"));
    QVERIFY(contenu.contains("_eco"));   // le marqueur que l'intercepteur retire
}

void TestLazyImages::initTestCase()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());

    QImage img(400, 300, QImage::Format_RGB32);
    for (int y = 0; y < 300; ++y) {
        QRgb *l = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < 400; ++x)
            l[x] = qRgb(QRandomGenerator::global()->bounded(256),
                        QRandomGenerator::global()->bounded(256),
                        QRandomGenerator::global()->bounded(256));
    }
    QBuffer b(&m_photo);
    b.open(QIODevice::WriteOnly);
    img.save(&b, "PNG");
    b.close();
    QVERIFY(m_photo.size() > 2000);

    m_server = new QTcpServer(this);
    QVERIFY(m_server->listen(QHostAddress::LocalHost));
    m_port = m_server->serverPort();

    connect(m_server, &QTcpServer::newConnection, this, [this]{
        while (QTcpSocket *s = m_server->nextPendingConnection()) {
            connect(s, &QTcpSocket::readyRead, this, [this, s]{
                const QByteArray req = s->readAll();
                const QByteArray premiere = req.split('\n').first().trimmed();
                const QString chemin = QString::fromLatin1(premiere.split(' ').value(1));
                m_requetes << chemin;

                QByteArray corps;
                QByteArray type = "text/html; charset=utf-8";
                if (chemin.startsWith(QStringLiteral("/vide"))) {
                    const int i = chemin.indexOf(QStringLiteral("?v="));
                    const QByteArray v = i >= 0 ? chemin.mid(i + 3).toUtf8() : QByteArray("0");
                    corps = "<!DOCTYPE html><html><body data-v='" + v
                            + "'><h1>aucune image ici</h1><p>texte seul</p></body></html>";
                } else if (chemin.startsWith(QStringLiteral("/page"))) {
                    const int i = chemin.indexOf(QStringLiteral("?v="));
                    const QByteArray v = i >= 0 ? chemin.mid(i + 3).toUtf8() : QByteArray("0");
                    corps = "<!DOCTYPE html><html><body data-v='" + v
                            + "'><h1>lazy</h1>"
                              "<img id='i' src='http://127.0.0.1:" + QByteArray::number(m_port)
                            + "/photo.png?v=" + v + "'></body></html>";
                } else if (chemin.startsWith(QStringLiteral("/photo.png"))) {
                    corps = m_photo; type = "image/png";
                } else {
                    corps = "rien"; type = "text/plain";
                }
                s->write("HTTP/1.1 200 OK\r\nContent-Type: " + type
                         + "\r\nContent-Length: " + QByteArray::number(corps.size())
                         + "\r\nCache-Control: no-store\r\n\r\n" + corps);
                s->flush();
            });
        }
    });

    m_win = new MainWindow(nullptr, m_dir->path());
    QVERIFY(m_win);
    // Images autorisees (sinon elles ne seraient jamais demandees, meme differees).
    QMetaObject::invokeMethod(m_win, "toggleImagesOff", Qt::DirectConnection, Q_ARG(bool, false));
    m_win->profile()->setHttpCacheType(QWebEngineProfile::NoCache);
}

void TestLazyImages::cleanupTestCase()
{
    delete m_win; m_win = nullptr;
    delete m_dir; m_dir = nullptr;
}

void TestLazyImages::imagesDiffereesSansTelechargement()
{
    activer(true);
    m_requetes.clear();
    ++m_version;
    vue()->load(QUrl(QStringLiteral("http://127.0.0.1:%1/page?v=%2").arg(m_port).arg(m_version)));
    attendreDocument();

    // Le DOM doit avoir un placeholder ET l'URL reelle en data-browsereco-src.
    attendreDocument();   // ne pas lire l'ancien document
    QVariant src;
    src = attendre(QStringLiteral("document.getElementById('i') "
                                  "? document.getElementById('i').getAttribute('data-browsereco-src') : ''"),
                   [&](const QVariant &v) { return !v.toString().isEmpty(); });
    QVERIFY2(src.toString().contains(QStringLiteral("photo.png")),
             qPrintable(QStringLiteral("URL reelle non sauvegardée : ") + src.toString()));

    // L'image en attente doit porter la bordure pointillee (c'est le signalement
    // visuel ; le src reste reel, le blocage est fait au reseau).
    const QString bordure = attendre(QStringLiteral("document.getElementById('i') "
                                                     "? document.getElementById('i').style.border : ''"),
                                     [&](const QVariant &v) { return v.toString().contains("dashed"); })
                                .toString();
    QVERIFY2(bordure.contains(QStringLiteral("dashed")),
             qPrintable(QStringLiteral("image non signalee comme en attente : ") + bordure));

    // LE POINT CLEF : le serveur ne doit avoir recu AUCUNE requete d'image.
    QTest::qWait(800);
    QCOMPARE(requetesImage(), 0);
}

void TestLazyImages::clicChargeLImageReelle()
{
    activer(true);
    m_requetes.clear();
    ++m_version;
    vue()->load(QUrl(QStringLiteral("http://127.0.0.1:%1/page?v=%2").arg(m_port).arg(m_version)));
    attendreDocument();
    attendre(QStringLiteral("document.getElementById('i') "
                            "? document.getElementById('i').getAttribute('data-browsereco-src') : ''"),
             [&](const QVariant &v) { return !v.toString().isEmpty(); });
    QCOMPARE(requetesImage(), 0);

    // Clic sur le placeholder : restaure l'URL reelle, le navigateur la charge
    // normalement (AutoLoadImages n'est pas coupe).
    vue()->page()->runJavaScript(QStringLiteral(
        "document.getElementById('i').dispatchEvent(new MouseEvent('click', {bubbles:true, cancelable:true}))"));

    // L'image doit maintenant etre chargee : naturalWidth = 400.
    const int largeur = attendre(QStringLiteral("document.getElementById('i').complete && "
                                                 "document.getElementById('i').naturalWidth > 0 "
                                                 "? document.getElementById('i').naturalWidth : -1"),
                                 [&](const QVariant &v) { return v.toInt() > 0; }, 20000).toInt();
    QVERIFY2(largeur == 400,
             qPrintable(QStringLiteral("l'image ne s'est pas chargée apres le clic (largeur=%1)").arg(largeur)));
    QCOMPARE(requetesImage(), 1);

    // Ce qui compte vraiment : le CDN d'origine ne doit JAMAIS voir notre
    // marqueur interne (c'est l'intercepteur qui le retire avant de revalider).
    for (const QString &r : m_requetes) {
        QVERIFY2(!r.contains(QStringLiteral("_eco")),
                 qPrintable(QStringLiteral("le marqueur a fuite vers l'origin : ") + r));
    }
    QCOMPARE(requetesImage(), 1);   // une seule requete, pas deux

    // L'attribut de deferrement doit avoir disparu.
    const QString attr = attendre(QStringLiteral("document.getElementById('i').getAttribute('data-browsereco-src')"),
                                 [](const QVariant &) { return true; }).toString();
    QVERIFY2(attr.isEmpty(), "l'attribut de report n'a pas ete retire apres le clic");
}

void TestLazyImages::pageSansImagesNeBougePas()
{
    activer(true);
    m_requetes.clear();
    ++m_version;
    // Une page SANS aucune image : elle ne doit pas etre cassee, et le compteur
    // doit rester a zero.
    vue()->load(QUrl(QStringLiteral("http://127.0.0.1:%1/vide?v=%2").arg(m_port).arg(m_version)));
    attendre(QStringLiteral("document.body && document.body.dataset.v === '%1' ? 1 : -1")
                 .arg(m_version),
             [](const QVariant &v) { return v.toInt() == 1; });
    const QVariant etat = attendre(QStringLiteral("document.readyState"),
                                   [](const QVariant &v) { return v.toString() == "complete"; });
    QCOMPARE(etat.toString(), QStringLiteral("complete"));
    QCOMPARE(attendre(QStringLiteral("document.images.length"),
                      [](const QVariant &v) { return v.toInt() == 0; }).toInt(), 0);

    auto *page = qobject_cast<BrowserPage *>(vue()->page());
    QVERIFY(page);
    QSignalSpy spy(page, &BrowserPage::imagesDeferred);
    page->refreshDeferredImageCount();
    QVERIFY(spy.wait(5000));
    QCOMPARE(page->deferredImageCount(), 0);
}

void TestLazyImages::compteurCorrect()
{
    // La page de test contient UNE image : le compteur doit dire 1, ni 0 (le
    // script ne tourne pas) ni 2 (double comptage).
    activer(true);
    m_requetes.clear();
    ++m_version;
    vue()->load(QUrl(QStringLiteral("http://127.0.0.1:%1/page?v=%2").arg(m_port).arg(m_version)));
    attendreDocument();
    attendre(QStringLiteral("document.getElementById('i') "
                            "? document.getElementById('i').getAttribute('data-browsereco-src') : ''"),
             [](const QVariant &v) { return !v.toString().isEmpty(); });

    auto *page = qobject_cast<BrowserPage *>(vue()->page());
    QVERIFY(page);
    QSignalSpy spy(page, &BrowserPage::imagesDeferred);
    page->refreshDeferredImageCount();
    QVERIFY(spy.wait(5000));
    QCOMPARE(page->deferredImageCount(), 1);
}

void TestLazyImages::reglageDesactiveChargeNormalement()
{
    activer(false);
    m_requetes.clear();
    ++m_version;
    vue()->load(QUrl(QStringLiteral("http://127.0.0.1:%1/page?v=%2").arg(m_port).arg(m_version)));
    attendreDocument();
    const int largeur = attendre(QStringLiteral("document.getElementById('i').complete && "
                                                 "document.getElementById('i').naturalWidth > 0 "
                                                 "? document.getElementById('i').naturalWidth : -1"),
                                 [&](const QVariant &v) { return v.toInt() > 0; }, 20000).toInt();
    QCOMPARE(largeur, 400);
    // Au moins une requete : avec le cache HTTP desactive, Chromium peut
    // re-claimer l'image (scanner speculatif + balise img). Ce qui compte ici,
    // c'est qu'elle soit chargee SANS report.
    QVERIFY(requetesImage() >= 1);
}

QTEST_MAIN(TestLazyImages)
#include "test_lazy_images.moc"