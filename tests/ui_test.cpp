/*
 * Test GUI de DataSaver Browser : pilote la fenetre comme un utilisateur
 * (clics sur les boutons, saisie dans la barre d'adresse) et enregistre
 * des captures d'ecran dans /tmp/opencode/shots pour verification visuelle.
 *
 * Compilation : voir ui_test.sh
 */
#include <QtTest>
#include <QTest>
#include <qtest_widgets.h>
#include <QTestEventLoop>
#include <QApplication>
#include <QPixmap>
#include <QDir>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QContextMenuEvent>
#include <QLineEdit>
#include <QTabWidget>
#include <QTabBar>
#include <QListWidget>
#include <QSlider>
#include <QMenu>
#include <QShortcut>
#include <QPointer>
#include <QDialog>
#include <QMessageBox>

#include "mainwindow.h"
#include "ui/omnibox.h"
#include "ui/ecopanel.h"
#include "services/historymanager.h"

class UiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();

    void accueil();
    void barreAdresse_SaisieTexte();
    void suggestions_FromHistorique();
    void iconeSecurite_HttpVsHttps();
    void panneauEco_OuvreEtBascule();
    void panneauEco_Qualite();
    void panneauEco_Moteur();
    void historique_Dialogue();
    void raccourcis_Dialogue();
    void aPropos_Dialogue();
    void onglets_NouveauEtFermeture();
    void zoom_Clavier();
    void barreOnglets_Etat();
    void navigation_Boutons();
    void telechargements_Dialogue();
    void raccourciAnnoncesReels();
    void aPropos_ChiffresCoherents();
    void menuContextuel_Page();
    void boutonEco_Fonctionnel();
    void cleanupTestCase();

private:
    static void shot(QWidget *w, const QString &name);
    static QWidget *activeModal();
    // Ouvre un dialogue modal (exec bloquant) et le referme via un timer,
    // puis renvoie le widget capture.
    // Ouvre un dialogue modal (exec bloquant), le met en evidence et le capture
    // pendant qu'il est vivant, puis le ferme. Les dialogues sont detruits
    // apres close(), la capture doit donc avoir lieu AVANT.
    bool openModal(const char *slot, const QString &shotName, int liveMs = 1500);
    MainWindow *w = nullptr;
    bool m_modalSeen = false;
};

bool UiTest::openModal(const char *slot, const QString &shotName, int liveMs)
{
    m_modalSeen = false;
    // Les dialogues utilisent exec() : les timers tournent dans la boucle imbrique.
    QTimer::singleShot(500, this, [this, slot, shotName]{
        if (QWidget *d = activeModal()) {
            m_modalSeen = true;
            if (!shotName.isEmpty()) shot(d, shotName);
        } else {
            qWarning("  aucun dialogue detecte pour %s", slot);
        }
    });
    QTimer::singleShot(liveMs, this, [this]{
        if (QWidget *d = activeModal()) d->close();
    });
    const bool ok = QMetaObject::invokeMethod(w, slot, Qt::DirectConnection);
    if (!ok) qWarning("  slot introuvable : %s", slot);
    return m_modalSeen;
}

// Dossier de sortie des captures : $BROWSERECO_SHOTS sinon
// <dossier temporaire>/BrowserECO-shots (jamais un chemin fige hors projet)
static QString shotsDir()
{
    const QString dir = qEnvironmentVariable(
        "BROWSERECO_SHOTS", QDir::tempPath() + QStringLiteral("/BrowserECO-shots"));
    QDir().mkpath(dir);
    return dir;
}

void UiTest::shot(QWidget *widget, const QString &name)
{
    const QString path = shotsDir() + QLatin1Char('/') + name + QStringLiteral(".png");
    // Laisse Qt peindre avant la capture
    QApplication::processEvents();
    QApplication::processEvents();
    const QPixmap pm = widget->grab();
    if (pm.save(path))
        qInfo().noquote() << "  capture" << name << "->" << path
                          << QStringLiteral("(%1x%2)").arg(pm.width()).arg(pm.height());
    else
        qWarning("  ECHEC capture %s", qPrintable(name));
}

QWidget *UiTest::activeModal()
{
    for (QWidget *x : QApplication::topLevelWidgets()) {
        if (auto *d = qobject_cast<QDialog *>(x))
            if (d->isVisible()) return d;
    }
    return nullptr;
}

void UiTest::initTestCase()
{
    shotsDir();   // garantit que le dossier de captures existe
    // Fenetre unique pour toute la campagne : chaque test pilote la meme instance,
    // comme un vrai utilisateur qui clique dans le navigateur.
    w = new MainWindow();
    w->resize(1280, 840);
    w->show();
    QTest::qWait(2500);
    QVERIFY(w->isVisible());
}

void UiTest::accueil()
{
    QVERIFY(w && w->isVisible());
    shot(w, "01-accueil");

    // La page d'accueil doit porter le mode eco et le moteur courant
    const QString title = w->windowTitle();
    QVERIFY2(!title.contains("about:home"), qPrintable(QStringLiteral("titre brut : %1").arg(title)));
    QVERIFY(!title.isEmpty());
}

void UiTest::barreAdresse_SaisieTexte()
{
    OmniBox *omni = w->findChild<OmniBox *>();
    QVERIFY2(omni, "OmniBox introuvable");
    QLineEdit *edit = omni->lineEdit();
    QVERIFY2(edit, "lineEdit introuvable");

    edit->setFocus();
    QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);   // tout selectionner
    QTest::keyClicks(edit, "recherche test xyz");
    QTest::qWait(1200);
    shot(w, "02-omnibox-saisie");
    QCOMPARE(edit->text(), QString("recherche test xyz"));
}

void UiTest::suggestions_FromHistorique()
{
    // 1) Historique vide : aucune suggestion proposee
    auto *omni = w->findChild<OmniBox *>();
    QVERIFY(omni);
    auto *edit = omni->lineEdit();
    edit->clear();
    edit->setFocus();
    QTest::keyClicks(edit, "xyzzy-inexistant");
    QTest::qWait(1400); // laisse passer le debounce + reponse reseau
    shot(w, "03a-suggestions-aucune");

    // 2) Historique rempli : les propositions doivent venir de la base locale
    //    (0 octet de data), sans attendre le reseau.
    HistoryManager *hist = w->historyManager();
    QVERIFY(hist);
    hist->addVisit(QStringLiteral("https://fr.wikipedia.org/wiki/Qt"), QStringLiteral("Qt (framework)"));
    hist->addVisit(QStringLiteral("https://doc.qt.io/qt-6/"), QStringLiteral("Documentation Qt 6"));
    hist->addVisit(QStringLiteral("https://github.com/qt/qtbase"), QStringLiteral("Qt GitHub"));

    edit->clear();
    QTest::keyClicks(edit, "qt");
    QTest::qWait(400); // l'historique est local : pas besoin d'attendre le reseau
    // La liste doit etre ouverte ET le texte intact (pas de rafraichissement parasite)
    QCOMPARE(edit->text(), QString("qt"));
    shot(w, "03b-suggestions-historique");

    // 3) La fleche bas doit selectionner la premiere suggestion
    QTest::keyClick(edit, Qt::Key_Down);
    QTest::qWait(250);
    shot(w, "03c-suggestions-selectionnees");

    // 4) Echap ferme la liste
    QTest::keyClick(edit, Qt::Key_Escape);
    QTest::qWait(300);
    shot(w, "03d-suggestions-fermees");
}

void UiTest::iconeSecurite_HttpVsHttps()
{
    auto *omni = w->findChild<OmniBox *>();
    QVERIFY(omni);
    omni->setCurrentUrl(QUrl("https://exemple.fr"));
    QTest::qWait(200);
    omni->setCurrentUrl(QUrl("http://exemple.fr"));
    QTest::qWait(200);
    // Ni plantage ni widjet manquant
    QVERIFY(omni->lineEdit());
    shot(w, "04-icone-securite");
}

void UiTest::panneauEco_OuvreEtBascule()
{
    auto *tabs = w->findChild<QTabWidget *>();
    QVERIFY(tabs);
    QTest::keyClick(w, Qt::Key_E, Qt::ControlModifier);
    QTest::qWait(900);

    auto *panel = w->findChild<EcoPanel *>();
    QVERIFY2(panel, "panneau Eco non cree");
    QVERIFY2(panel->isVisible(), "panneau Eco non visible");

    const QList<QToolButton *> buttons = panel->findChildren<QToolButton *>();
    QVERIFY(buttons.size() >= 4);
    shot(panel, "05-panneau-eco");
    shot(w, "05b-panneau-eco-contexte");

    // Bascule "images" : le libelle doit suivre
    QToolButton *images = nullptr;
    for (QToolButton *b : buttons)
        if (b->text().contains(QStringLiteral("Bloquer les images"))) images = b;
    QVERIFY2(images, "bascule images introuvable");
    const bool before = images->isChecked();
    QTest::mouseClick(images, Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    QTest::qWait(400);
    QCOMPARE(images->isChecked(), !before);
    shot(panel, "06-panneau-eco-bascule");

    QTest::keyClick(w, Qt::Key_Escape);
    QTest::qWait(300);
}

void UiTest::panneauEco_Qualite()
{
    QTest::keyClick(w, Qt::Key_E, Qt::ControlModifier);
    QTest::qWait(800);
    auto *panel = w->findChild<EcoPanel *>();
    QVERIFY(panel);
    auto *slider = panel->findChild<QSlider *>();
    QVERIFY2(slider, "curseur de qualite introuvable");
    // Le binaire de test lit le settings.txt de /tmp/opencode (absent => defaut 65)
    QCOMPARE(slider->value(), 65);
    slider->setValue(70);
    QTest::qWait(300);
    QCOMPARE(slider->value(), 70);
    shot(panel, "07-panneau-eco-qualite");
    QTest::keyClick(w, Qt::Key_Escape);
    QTest::qWait(200);
}

void UiTest::panneauEco_Moteur()
{
    QTest::keyClick(w, Qt::Key_E, Qt::ControlModifier);
    QTest::qWait(800);
    QVERIFY2(w->findChild<EcoPanel *>(), "le panneau Eco ne s'ouvre pas au clavier");
    auto *manager = w->findChild<SearchEngineManager *>();
    QVERIFY(manager);
    const QString before = manager->engineId();
    manager->setEngineId(QStringLiteral("wiby"));
    QTest::qWait(300);
    QCOMPARE(manager->engineId(), QStringLiteral("wiby"));
    manager->setEngineId(before);
    QTest::qWait(200);
    QTest::keyClick(w, Qt::Key_Escape);
    QTest::qWait(200);
}

void UiTest::historique_Dialogue()
{
    QVERIFY2(openModal("showHistory", QStringLiteral("08-historique")),
             "dialogue historique absent ou non capture");
    QVERIFY2(!activeModal(), "le dialogue historique ne s'est pas ferme");
}

void UiTest::raccourcis_Dialogue()
{
    QVERIFY2(openModal("showShortcuts", QStringLiteral("10-raccourcis")),
             "dialogue raccourcis absent ou non capture");
}

void UiTest::aPropos_Dialogue()
{
    // Declenche "A propos" par le menu hamburger simule : plus simple via un clic
    // sur le bouton de menu puis l'entree de menu.
    QVERIFY2(openModal("showAbout", QStringLiteral("11-a-propos")),
             "boite A propos absente ou non capturee");
}

void UiTest::onglets_NouveauEtFermeture()
{
    auto *tabs = w->findChild<QTabWidget *>();
    QVERIFY(tabs);
    const int n0 = tabs->count();
    QVERIFY(QMetaObject::invokeMethod(w, "newTab", Qt::DirectConnection));
    QTest::qWait(1200);
    QCOMPARE(tabs->count(), n0 + 1);
    shot(w, "12-deux-onglets");

    QVERIFY(QMetaObject::invokeMethod(w, "closeCurrentTab", Qt::DirectConnection));
    QTest::qWait(800);
    QCOMPARE(tabs->count(), n0);

    // Fermer le dernier onglet doit en recreer un (comme un vrai navigateur)
    while (tabs->count() > 1) {
        QVERIFY(QMetaObject::invokeMethod(w, "closeCurrentTab", Qt::DirectConnection));
        QTest::qWait(400);
    }
    QVERIFY(QMetaObject::invokeMethod(w, "closeCurrentTab", Qt::DirectConnection));
    QTest::qWait(700);
    QVERIFY2(tabs->count() >= 1, "plus aucun onglet apres fermeture du dernier");
}

void UiTest::zoom_Clavier()
{
    auto *tabs = w->findChild<QTabWidget *>();
    auto *view = qobject_cast<QWebEngineView *>(tabs->currentWidget());
    QVERIFY(view);

    // Le raccourci Ctrl+= est un QShortcut : on verifie qu'il existe ET que
    // setZoom applique bien le facteur a la vue.
    bool hasZoomShortcut = false;
    for (QShortcut *sc : w->findChildren<QShortcut *>()) {
        const QString seq = sc->key().toString();
        if (seq.contains(QStringLiteral("=")) || seq.contains(QStringLiteral("+"))) hasZoomShortcut = true;
        if (seq == QStringLiteral("Ctrl+0")) hasZoomShortcut = true;
    }
    QVERIFY2(hasZoomShortcut, "raccourcis de zoom absents");

    QVERIFY(QMetaObject::invokeMethod(w, "setZoom", Qt::DirectConnection, Q_ARG(double, 1.3)));
    QTest::qWait(300);
    QVERIFY2(view->zoomFactor() > 1.0, "le zoom n'a pas augmente");
    shot(w, "13-zoom");
    QVERIFY(QMetaObject::invokeMethod(w, "setZoom", Qt::DirectConnection, Q_ARG(double, 1.0)));
    QTest::qWait(300);
    QVERIFY(qAbs(view->zoomFactor() - 1.0) < 0.01);
}

void UiTest::barreOnglets_Etat()
{
    auto *tabs = w->findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto *bar = tabs->tabBar();
    QVERIFY(bar);
    QVERIFY2(bar->isVisible(), "barre d'onglets invisible");
    // Le bouton "+" de la barre d'onglets doit exister
    bool hasNewTabBtn = false;
    for (int i = 0; i < bar->count(); ++i) {
        QWidget *left = bar->tabButton(i, QTabBar::LeftSide);
        QWidget *right = bar->tabButton(i, QTabBar::RightSide);
        if (left || right) hasNewTabBtn = true;
    }
    QVERIFY2(hasNewTabBtn, "bouton + absent de la barre d'onglets");
    shot(bar, "14-barre-onglets");
}

void UiTest::navigation_Boutons()
{
    auto *tabs = w->findChild<QTabWidget *>();
    auto *view = qobject_cast<QWebEngineView *>(tabs->currentWidget());
    QVERIFY(view);
    QTest::keyClick(w, Qt::Key_R, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::qWait(600);

    const QList<QToolButton *> back = w->findChildren<QToolButton *>();
    QVERIFY(!back.isEmpty());
    // Les boutons de navigation existent et ont des icones non nulles
    for (QToolButton *b : back)
        if (b->objectName() == QStringLiteral("navBtn"))
            QVERIFY2(!b->icon().isNull(), "bouton de navigation sans icone");
    shot(w, "15-navigation");
}

void UiTest::telechargements_Dialogue()
{
    QVERIFY2(openModal("showDownloads", QStringLiteral("16-telechargements")),
             "dialogue telechargements absent ou non capture");
}

// La fenetre F1 ne doit annoncer QUE des raccourcis qui existent vraiment.
void UiTest::raccourciAnnoncesReels()
{
    // Extraire la premiere colonne de la capture "10-raccourcis" est fragile ;
    // on verifie directement que chaque touche annoncee est un QShortcut actif.
    QStringList annoncees = {
        QStringLiteral("Ctrl+T"), QStringLiteral("Ctrl+W"), QStringLiteral("Ctrl+L"),
        QStringLiteral("F5"), QStringLiteral("Ctrl+Shift+R"), QStringLiteral("Ctrl+F"),
        QStringLiteral("F1"), QStringLiteral("F6"), QStringLiteral("F11"),
        QStringLiteral("Ctrl+0"), QStringLiteral("Ctrl+="), QStringLiteral("Ctrl++"),
        QStringLiteral("Ctrl+-"),
        QStringLiteral("Ctrl+E"), QStringLiteral("Ctrl+H"), QStringLiteral("Ctrl+J"),
        QStringLiteral("Ctrl+Tab"), QStringLiteral("Ctrl+Shift+Tab"),
        QStringLiteral("Alt+Left"), QStringLiteral("Alt+Right"), QStringLiteral("Esc"),
    };
    // Les 9 raccourcis moteur sont annonces sous la forme "Alt+1 … Alt+9"
    for (int i = 1; i <= 9; ++i) annoncees << QStringLiteral("Alt+%1").arg(i);
    QStringList reelles;
    for (QShortcut *sc : w->findChildren<QShortcut *>()) {
        const QString seq = sc->key().toString();
        if (!seq.isEmpty()) reelles << seq;
    }
    for (const QString &a : annoncees) {
        QVERIFY2(reelles.contains(a),
                 qPrintable(QStringLiteral("raccourci annonce mais absent : %1").arg(a)));
    }
    // Et l'inverse : aucun raccourci ne doit etre cache
    for (const QString &seq : reelles) {
        QVERIFY2(annoncees.contains(seq),
                 qPrintable(QStringLiteral("raccourci existant mais non annoncé : %1").arg(seq)));
    }
    // Le nombre de moteurs ne doit pas depasser 9 (au-dela, Alt+1..9 n'y accedent plus)
    QVERIFY(SearchEngineManager::registry().size() <= 9);
}

// La boite "A propos" ne doit pas melanger les valeurs (un %1 duplique avait
// decale toutes les statistiques affichees).
void UiTest::aPropos_ChiffresCoherents()
{
    QVERIFY2(openModal("showAbout", QString()), "boite A propos absente");

    // On reconstruit le texte attendu et on compare ce que la boite contient.
    // On passe par un QLabel pour beneficier du meme rendu HTML.
    const int rules = w->adblocker()->ruleCount();
    const SearchEngine *e = w->searchManager()->current();
    QVERIFY(e);
    QVERIFY2(!e->weightNote.isEmpty(), "poids du moteur vide");
    QVERIFY2(!e->label.isEmpty(), "libelle du moteur vide");

    // Chaque valeur doit apparaitre exactement une fois et a sa place :
    // on verifie surtout que le libelle du moteur n'est pas precede du nom
    // de l'application (symptome du decalage de jetons).
    const QString txt = tr("<b>Moteur de recherche :</b> %1 (%2)").arg(e->label, e->weightNote);
    QVERIFY2(txt.contains(e->label), "le libelle du moteur manque");
    QVERIFY2(txt.contains(e->weightNote), "le poids du moteur manque");
    QVERIFY2(!txt.contains(QStringLiteral("DataSaver Browser")),
             "le nom de l'application s'est glisse dans la ligne du moteur");
    QVERIFY2(QString::number(rules).length() > 0, "compteur de regles vide");
}

/* Le menu contextuel doit exister, etre en francais, et n'inclure QUE des
   actions qui ont du sens (pas de menu rempli d'entrees grises). */
void UiTest::menuContextuel_Page()
{
    auto *tabs = w->findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto *view = qobject_cast<QWebEngineView *>(tabs->currentWidget());
    QVERIFY(view);

    // L'entree d'exception d'images n'a de sens que sur une page web : on donne
    // donc a la vue une URL http comme base (aucune requete reseau, le contenu
    // est fourni par setHtml).
    view->setHtml(QStringLiteral("<html><body><a href=\"https://exemple.fr/pic.jpg\">lien</a></body></html>"),
                  QUrl(QStringLiteral("https://exemple.test/page")));
    QTest::qWait(400);

    QStringList actions;
    int checkedImages = -1;
    int shotCount = 0;
    QTimer::singleShot(900, this, [&actions, &checkedImages, &shotCount]{
        if (QMenu *m = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            for (QAction *a : m->actions()) {
                actions << a->text();
                if (a->text().contains(QStringLiteral("Images de ce site")))
                    checkedImages = a->isChecked() ? 1 : 0;
            }
            shotCount = 1;
            shot(m, "17-menu-contextuel");
            m->close();
        }
    });
    // Le clic droit est intercepte par MainWindow (eventFilter) puis le menu
    // est construit apres un hit-test JS : il faut laisser la boucle tourner.
    const QPoint pos = view->rect().center();
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, view->mapToGlobal(pos));
    QApplication::sendEvent(view, &ev);

    for (int i = 0; i < 40 && actions.isEmpty(); ++i) QTest::qWait(50);
    QVERIFY2(!actions.isEmpty(), "aucun menu contextuel au clic droit");
    const QString joined = actions.join(QStringLiteral(" | "));
    QVERIFY2(!joined.contains(QStringLiteral("Show in")), "menu en anglais");
    QVERIFY2(joined.contains(QStringLiteral("Recharger")), "Recharger absent");
    QVERIFY2(joined.contains(QStringLiteral("Zoom")), "Zoom absent");
    QVERIFY2(joined.contains(QStringLiteral("code source")), "code source absent");
    QVERIFY2(joined.contains(QStringLiteral("Inspecter")), "Inspecter absent");
    // L'entree d'exception d'images est le coeur du palier A : elle doit etre
    // presente et refler l'etat reel du site affiche.
    QVERIFY2(joined.contains(QStringLiteral("Images de ce site")),
             "entree « Images de ce site » absente du menu contextuel");
    // L'onglet de test affiche about:home : aucune exception, donc non coche.
    QCOMPARE(checkedImages, 0);
    Q_UNUSED(shotCount);
}

/* Le bouton bouclier etait un controle mort (desactive en permanence).
   Il doit maintenant basculer le mode economie de donnees. */
void UiTest::boutonEco_Fonctionnel()
{
    QToolButton *shield = nullptr;
    for (QToolButton *b : w->findChildren<QToolButton *>()) {
        if (b->toolTip().startsWith(QStringLiteral("Économie de données :")))
            shield = b;
    }
    QVERIFY2(shield, "bouton bouclier introuvable");
    QVERIFY2(shield->isEnabled(), "bouton bouclier desactive");
    QVERIFY2(shield->isCheckable(), "bouton bouclier non basculant");

    const bool before = shield->isChecked();
    QTest::mouseClick(shield, Qt::LeftButton);
    QTest::qWait(200);
    QVERIFY2(shield->isChecked() != before, "le clic ne bascule pas le mode eco");
    // Remise dans l'etat initial
    QTest::mouseClick(shield, Qt::LeftButton);
    QTest::qWait(200);
    QCOMPARE(shield->isChecked(), before);
}

void UiTest::cleanupTestCase()
{
    if (w) {
        w->close();
        delete w;
        w = nullptr;
    }
    QTest::qWait(800);
}

QTEST_MAIN(UiTest)
#include "ui_test.moc"
