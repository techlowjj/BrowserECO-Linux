/*
 * En-têtes de requête : Sec-GPC et Save-Data.
 *
 * 1. EcoInterceptor::privacyHeaders() : fonction pure, aucun réseau.
 * 2. Preuve de bout en bout : un serveur HTTP local sert une page et une
 *    image, le navigateur les charge, et le serveur MÉMORISE les en-têtes
 *    qu'il reçoit. « Le code pose l'en-tête » ne prouve pas que l'en-tête
 *    part ; seul le serveur qui le reçoit le prouve.
 */
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QBuffer>
#include <QImage>
#include <QWebEngineView>
#include <QTabWidget>

#include "mainwindow.h"
#include "ecointerceptor.h"

class TestHeaders : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void enTetesPures();
    void secGpcPartSurLaPage();
    void secGpcPartSurLesImages();
    void secGpcDesactiveNePartPlus();
    void saveDataIndependantDeSecGpc();

private:
    void chargerPage();
    QList<QMap<QByteArray, QByteArray>> requetesPour(const QString &prefixe);

    QTcpServer *m_server = nullptr;
    QTemporaryDir *m_dir = nullptr;
    MainWindow *m_win = nullptr;
    int m_port = 0;
    // En-têtes reçus, dans l'ordre : chemin -> en-têtes
    QVector<QPair<QString, QMap<QByteArray, QByteArray>>> m_recus;
};

void TestHeaders::initTestCase()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());

    m_server = new QTcpServer(this);
    QVERIFY(m_server->listen(QHostAddress::LocalHost));
    m_port = m_server->serverPort();

    connect(m_server, &QTcpServer::newConnection, this, [this]{
        while (QTcpSocket *s = m_server->nextPendingConnection()) {
            connect(s, &QTcpSocket::readyRead, this, [this, s]{
                const QByteArray brut = s->readAll();
                if (brut.isEmpty()) { s->deleteLater(); return; }
                const QList<QByteArray> lignes = brut.split('\n');
                const QList<QByteArray> parts = lignes.first().trimmed().split(' ');
                const QString chemin = QString::fromLatin1(parts.value(1));

                QMap<QByteArray, QByteArray> entetes;
                for (const QByteArray &l : lignes) {
                    const int i = l.indexOf(':');
                    if (i > 0)
                        entetes.insert(l.left(i).trimmed().toLower(), l.mid(i + 1).trimmed());
                }
                m_recus.append({ chemin, entetes });   // <- la preuve

                QByteArray corps;
                QByteArray type = "text/html; charset=utf-8";
                if (chemin.startsWith(QStringLiteral("/page"))) {
                    corps = "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body>"
                            "<h1>entetes</h1><img src='http://127.0.0.1:"
                            + QByteArray::number(m_port) + "/image.png' width='16'>"
                            "</body></html>";
                } else if (chemin.startsWith(QStringLiteral("/image.png"))) {
                    QImage img(16, 16, QImage::Format_ARGB32);
                    img.fill(Qt::darkRed);
                    QByteArray png;
                    QBuffer buf(&png);
                    buf.open(QIODevice::WriteOnly);
                    img.save(&buf, "PNG");
                    buf.close();
                    corps = png;
                    type = "image/png";
                } else {
                    corps = "ok";
                    type = "text/plain";
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
    // Le blocage d'images est actif par defaut : les tests qui observent la
    // requete d'image doivent le desactiver explicitement.
    QVERIFY(m_win->interceptor()->imagesOff());
}

/* Bascule les images par le VRAI chemin de l'application (le slot applique
   aussi le reglage WebEngine AutoLoadImages : sans cela Chromium ne demande
   aucune image et le test ne mesurerait rien). */
static void setImages(MainWindow *w, bool on)
{
    QMetaObject::invokeMethod(w, "toggleImagesOff", Qt::DirectConnection, Q_ARG(bool, on));
}

void TestHeaders::cleanupTestCase()
{
    delete m_win;
    m_win = nullptr;
    delete m_dir;
    m_dir = nullptr;
}

void TestHeaders::chargerPage()
{
    auto *tabs = m_win->findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto *view = qobject_cast<QWebEngineView *>(tabs->currentWidget());
    QVERIFY(view);
    m_recus.clear();
    view->load(QUrl(QStringLiteral("http://127.0.0.1:%1/page").arg(m_port)));
    QTest::qWait(2500);
}

QList<QMap<QByteArray, QByteArray>> TestHeaders::requetesPour(const QString &prefixe)
{
    QList<QMap<QByteArray, QByteArray>> out;
    for (const auto &r : m_recus)
        if (r.first.startsWith(prefixe)) out << r.second;
    return out;
}

void TestHeaders::enTetesPures()
{
    QVERIFY(EcoInterceptor::privacyHeaders(false, false).isEmpty());

    auto h = EcoInterceptor::privacyHeaders(true, false);
    QCOMPARE(h.size(), 1);
    QCOMPARE(h.first().first, QByteArray("Save-Data"));
    QCOMPARE(h.first().second, QByteArray("on"));

    h = EcoInterceptor::privacyHeaders(false, true);
    QCOMPARE(h.size(), 1);
    QCOMPARE(h.first().first, QByteArray("Sec-GPC"));
    QCOMPARE(h.first().second, QByteArray("1"));

    h = EcoInterceptor::privacyHeaders(true, true);
    QCOMPARE(h.size(), 2);
    QVERIFY(h.contains({ QByteArray("Save-Data"), QByteArray("on") }));
    QVERIFY(h.contains({ QByteArray("Sec-GPC"), QByteArray("1") }));
}

void TestHeaders::secGpcPartSurLaPage()
{
    chargerPage();
    const auto reqs = requetesPour(QStringLiteral("/page"));
    QVERIFY2(!reqs.isEmpty(), "le serveur local n'a recu aucune requete de page");
    QVERIFY2(reqs.first().value("sec-gpc") == QByteArray("1"),
             qPrintable(QStringLiteral("Sec-GPC absent des en-tetes recus : %1")
                            .arg(QString::fromLatin1(reqs.first().value("sec-gpc")))));
    QCOMPARE(reqs.first().value("save-data"), QByteArray("on"));
}

void TestHeaders::secGpcPartSurLesImages()
{
    // Les images sont bloquees par defaut : on les autorise par le vrai
    // chemin de l'IHM pour observer la requete de sous-ressource.
    setImages(m_win, false);
    chargerPage();
    const auto reqs = requetesPour(QStringLiteral("/image.png"));
    const bool ok = !reqs.isEmpty();
    const bool gpc = ok && reqs.first().value("sec-gpc") == QByteArray("1");
    setImages(m_win, true);           // toujours remettre dans l'etat initial
    QVERIFY2(ok, "l'image n'a pas ete demandee");
    QVERIFY2(gpc, "Sec-GPC absent sur la requete d'image");
}

void TestHeaders::secGpcDesactiveNePartPlus()
{
    m_win->interceptor()->setSecGpcEnabled(false);
    chargerPage();
    const auto reqs = requetesPour(QStringLiteral("/page"));
    QVERIFY(!reqs.isEmpty());
    QVERIFY2(!reqs.first().contains("sec-gpc"),
             "Sec-GPC est encore parti alors qu'il est desactive");
    // Save-Data, lui, doit toujours partir : les deux sont indépendants.
    QCOMPARE(reqs.first().value("save-data"), QByteArray("on"));
    m_win->interceptor()->setSecGpcEnabled(true);
}

void TestHeaders::saveDataIndependantDeSecGpc()
{
    m_win->interceptor()->setDataSaverEnabled(false);
    m_win->interceptor()->setUltraEcoEnabled(false);
    m_win->interceptor()->setSecGpcEnabled(true);
    chargerPage();
    const auto reqs = requetesPour(QStringLiteral("/page"));
    QVERIFY(!reqs.isEmpty());
    QVERIFY2(!reqs.first().contains("save-data"),
             "Save-Data est encore parti alors que l'economie est desactivee");
    QCOMPARE(reqs.first().value("sec-gpc"), QByteArray("1"));

    // Et l'inverse : Sec-GPC coupé, économie activée.
    m_win->interceptor()->setSecGpcEnabled(false);
    m_win->interceptor()->setDataSaverEnabled(true);
    chargerPage();
    const auto reqs2 = requetesPour(QStringLiteral("/page"));
    QVERIFY(!reqs2.isEmpty());
    QCOMPARE(reqs2.first().value("save-data"), QByteArray("on"));
    QVERIFY(!reqs2.first().contains("sec-gpc"));
    m_win->interceptor()->setSecGpcEnabled(true);
}

QTEST_MAIN(TestHeaders)
#include "test_headers.moc"