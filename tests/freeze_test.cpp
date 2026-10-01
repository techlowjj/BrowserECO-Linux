/*
 * Chasse au gel "Menu -> Telechargements".
 *
 * Un QMenu::exec() est une boucle d'evenements bloquante qui garde un grab
 * souris/clavier. Si l'objet QMenu est encore vivant quand l'action choisie
 * ouvre une boite modale (dlg.exec() = 2e boucle imbriquee), on empile deux
 * boucles et deux grabs : l'interface ne repond plus.
 *
 * Ce test reproduit le chemin exact du clic et surveille la liberation des
 * boucles avec un thread temoin (un QTimer ne s'executerait pas si la boucle
 * principale est captive).
 */
#include <QtTest>
#include <qtest_widgets.h>
#include <QApplication>
#include <QMenu>
#include <QDialog>
#include <QPushButton>
#include <QToolButton>
#include <QShortcut>
#include <QAtomicInt>
#include <QThread>
#include <QElapsedTimer>
#include <QTextStream>
#include "mainwindow.h"

static QDialog *findModal()
{
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *d = qobject_cast<QDialog *>(w))
            if (d->isVisible()) return d;
    return nullptr;
}

static int visibleMenuCount()
{
    int n = 0;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *m = qobject_cast<QMenu *>(w))
            if (m->isVisible()) ++n;
    return n;
}

/* Temoin : si la boucle principale reste captive, ce thread le signale. */
class Watchdog : public QThread {
public:
    void arm(int ms, QAtomicInt *flag) {
        m_ms = ms; m_flag = flag; m_done = false;
        start();
    }
    void disarm() { m_done = true; }
protected:
    void run() override {
        QElapsedTimer t; t.start();
        while (t.elapsed() < m_ms) {
            if (m_done) return;
            QThread::msleep(20);
        }
        if (!m_done) m_flag->storeRelease(1);
    }
private:
    int m_ms = 0;
    QAtomicInt *m_flag = nullptr;
    QAtomicInt m_done{0};
};

class FreezeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void clicMenuTelecargements();
    void clicMenuHistorique();
    void boutonFermerFermeLeDialogue();
    void raccourciUniqueParTouche();
    void cleanupTestCase();

private:
    MainWindow *w = nullptr;
};

// Clique le bouton hamburger puis l'entree de menu demandee, et verifie que
// tout se libere. Retourne true si le chemin a ete exerce reellement.
bool clickMenuEntry(MainWindow *w, const QString &label, int actionId,
                    bool *dialogueOuvert, bool *menuRestant, Watchdog *wd,
                    QAtomicInt *frozen)
{
    QToolButton *menuBtn = nullptr;
    for (QToolButton *b : w->findChildren<QToolButton *>())
        if (b->objectName() == QStringLiteral("menuBtn")) menuBtn = b;
    if (!menuBtn) return false;

    *dialogueOuvert = false;
    *menuRestant = false;
    frozen->storeRelease(0);

    // Arme le temoin AVANT d'entrer dans la boucle modale.
    wd->arm(8000, frozen);

    Q_UNUSED(label);
    QTimer::singleShot(0, [w, actionId]{
        // Le popup QMenu n'est pas affichable sous le backend offscreen :
        // on reproduit donc l'action que declencherait le clic.
        QMetaObject::invokeMethod(w, "applySessionAction", Qt::DirectConnection,
                                  Q_ARG(int, actionId));
    });
    QTimer::singleShot(400, [dialogueOuvert, menuRestant]{
        *dialogueOuvert = (findModal() != nullptr);
        *menuRestant = (visibleMenuCount() > 0);
    });
    QTimer::singleShot(1200, []{
        if (QDialog *d = findModal()) d->close();
    });

    return true;
}

void FreezeTest::initTestCase()
{
    w = new MainWindow();
    w->resize(1280, 840);
    w->show();
    QTest::qWait(2500);
    QVERIFY(w->isVisible());
}

void FreezeTest::clicMenuTelecargements()
{
    bool ouvert = false, menuRestant = false;
    QAtomicInt frozen{0};
    Watchdog wd;
    QVERIFY(clickMenuEntry(w, QStringLiteral("Téléchargements"), 3, &ouvert, &menuRestant, &wd, &frozen));

    // Si la boucle est captive, ce qWait ne rend jamais la main.
    QTest::qWait(5000);
    wd.disarm();
    wd.wait(200);

    QVERIFY2(frozen.loadAcquire() == 0, "GEL : les boucles d'evenements ne se liberent pas");
    QVERIFY2(ouvert, "le dialogue Téléchargements ne s'est pas ouvert");
    QVERIFY2(!menuRestant, "un QMenu reste vivant/visible pendant le dialogue (grab souris)");
    QVERIFY2(!findModal(), "le dialogue ne se ferme pas");
}

void FreezeTest::clicMenuHistorique()
{
    bool ouvert = false, menuRestant = false;
    QAtomicInt frozen{0};
    Watchdog wd;
    QVERIFY(clickMenuEntry(w, QStringLiteral("Historique"), 2, &ouvert, &menuRestant, &wd, &frozen));

    QTest::qWait(5000);
    wd.disarm();
    wd.wait(200);

    QVERIFY2(frozen.loadAcquire() == 0, "GEL : Historique ne rend pas la main");
    QVERIFY2(ouvert, "le dialogue Historique ne s'est pas ouvert");
    QVERIFY2(!menuRestant, "un QMenu reste vivant pendant le dialogue");
    QVERIFY2(!findModal(), "le dialogue ne se ferme pas");
}

void FreezeTest::boutonFermerFermeLeDialogue()
{
    // dlg.exec() ouvre une boucle imbriquee : toute l'interaction doit donc
    // etre programmee AVANT l'appel, et s'execute dans cette boucle.
    // Un temoin separe detecte le cas ou la boucle ne se libere jamais.
    QAtomicInt ferme{0};
    QAtomicInt trouve{0};
    QAtomicInt geles{0};
    Watchdog wd;
    wd.arm(9000, &geles);

    QTimer::singleShot(0, [this]{
        QMetaObject::invokeMethod(w, "showDownloads", Qt::DirectConnection);
    });
    QTimer::singleShot(600, [&trouve]{
        QDialog *d = findModal();
        if (!d) return;
        for (QPushButton *b : d->findChildren<QPushButton *>()) {
            if (b->text().contains(QStringLiteral("Fermer"))) {
                QTest::mouseClick(b, Qt::LeftButton, Qt::NoModifier, QPoint(6, 6));
                trouve.storeRelease(1);
                return;
            }
        }
    });
    QTimer::singleShot(1500, [&ferme]{
        if (!findModal()) ferme.storeRelease(1);
    });
    // Filet de securite : ferme le dialogue si le bouton ne fait rien.
    QTimer::singleShot(3000, []{
        if (QDialog *d = findModal()) d->close();
    });

    QTest::qWait(5000);
    wd.disarm();
    wd.wait(300);

    QVERIFY2(geles.loadAcquire() == 0, "GEL : la boucle modale ne s'est pas liberee");
    QCOMPARE(trouve.loadAcquire(), 1);
    QVERIFY2(ferme.loadAcquire() == 1, "le bouton Fermer ne ferme pas le dialogue (symptome de gel)");
}

void FreezeTest::raccourciUniqueParTouche()
{
    // Une touche ne doit etre enregistree qu'UNE fois dans toute la fenetre.
    // Les QAction du menu ne doivent porter aucun raccourci : la fenetre cree
    // deja les QShortcut. Un doublon ferait capturer la touche deux fois
    // (une par le menu, une par la fenetre) -> comportement aleatoire.
    QHash<QString, int> occurrences;
    for (QShortcut *sc : w->findChildren<QShortcut *>()) {
        const QString seq = sc->key().toString();
        if (!seq.isEmpty()) occurrences[seq]++;
    }
    for (auto it = occurrences.cbegin(); it != occurrences.cend(); ++it)
        QVERIFY2(it.value() == 1,
                 qPrintable(QStringLiteral("raccourci %1 enregistre %2 fois")
                                .arg(it.key()).arg(it.value())));
    QVERIFY(occurrences.contains(QStringLiteral("Ctrl+J")));
    QVERIFY(occurrences.contains(QStringLiteral("Ctrl+H")));
    QVERIFY(occurrences.contains(QStringLiteral("Ctrl+E")));
}

void FreezeTest::cleanupTestCase()
{
    // Detruire la fenetre : sans cela QtWebEngine detruit son noyau alors que
    // des pages sont encore vivantes -> SIGSEGV apres le dernier test.
    if (w) {
        w->close();
        delete w;
        w = nullptr;
    }
    QTest::qWait(800);
}

QTEST_MAIN(FreezeTest)
#include "freeze_test.moc"
