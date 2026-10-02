#include "mainwindow.h"
#include "ui/omnibox.h"
#include "ui/ecopanel.h"
#include "ui/icons.h"
#include "services/settingsstore.h"
#include "services/ecoimageserver.h"

#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>
#include <QTime>
#include <QLocale>
#include <QDesktopServices>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineHistory>
#include <QWebEngineNewWindowRequest>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QShortcut>
#include <QVector>
#include <QListWidget>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMenu>
#include <QAction>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QClipboard>
#include <QStyle>
#include <functional>
#include <QDebug>
#include "browserpage.h"

namespace {

QString humanBytes(qint64 b)
{
    if (b <= 0) return QStringLiteral("0 Ko");
    const char *u[] = { "o", "Ko", "Mo", "Go" };
    double v = double(b);
    int i = 0;
    while (v >= 1024.0 && i < 3) { v /= 1024.0; ++i; }
    return QString::number(v, 'f', v < 10 ? 1 : 0) + QLatin1Char(' ') + QString::fromLatin1(u[i]);
}

// Titre lisible pour un onglet (jamais "about:home" ni vide)
QString tabTitleFor(const QUrl &url, const QString &pageTitle)
{
    const QString u = url.toString();
    if (u.isEmpty() || u == QLatin1String("about:home") || pageTitle == QLatin1String("about:home"))
        return MainWindow::tr("Nouvel onglet");
    if (!pageTitle.trimmed().isEmpty()) return pageTitle.trimmed();
    if (!url.host().isEmpty()) return url.host();
    return MainWindow::tr("Nouvelle page");
}

const char *kAppName = "DataSaver Browser";

// Nombre maximum d'onglets dont l'URL est memorisee dans la session.
constexpr int kMaxSessionTabs = 12;

} // namespace

MainWindow::MainWindow(QWidget *parent, const QString &dataDir, bool privateMode)
    : QMainWindow(parent)
{
    // Dossier de reglages : --data-dir / BROWSERECO_DATA_DIR > dossier du binaire
    // si inscriptible > ~/.local/share/BrowserECO (portabilite conservee).
    const QString appDir = QCoreApplication::applicationDirPath();
    QString settingsDir = dataDir.isEmpty() ? qEnvironmentVariable("BROWSERECO_DATA_DIR")
                                             : dataDir;
    if (!settingsDir.isEmpty()) {
        QDir().mkpath(settingsDir);
        settingsDir = QDir(settingsDir).absolutePath();
    } else {
        QFile probe(appDir + QStringLiteral("/.writetest_settings"));
        if (probe.open(QIODevice::WriteOnly)) {
            probe.remove();
            settingsDir = appDir;
        } else {
            settingsDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
            if (settingsDir.isEmpty()) settingsDir = QDir::homePath() + QStringLiteral("/.config/BrowserECO");
            QDir().mkpath(settingsDir);
        }
    }
    m_settingsPath = QDir(settingsDir).filePath(QStringLiteral("settings.txt"));

    // Mode prive : --private ou appel depuis la ligne de commande.
    m_isPrivate = privateMode
        || QCoreApplication::arguments().contains(QStringLiteral("--private"));

    // Profil : off-the-record en mode prive (rien sur le disque : ni cookies,
    // ni localStorage/IndexedDB, ni cache), persistant sinon. La version
    // precedente ne faisait que desactiver les cookies persistants : le reste
    // des donnees de navigation etait toujours ecrit sur le disque.
    if (m_isPrivate) {
        m_profile = new QWebEngineProfile(this);   // profil hors disque
        m_profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
        m_profile->setHttpCacheMaximumSize(20 * 1024 * 1024);
        // Politique de permissions persistantes (geolocalisation, camera...) :
        // API Qt 6.8. En dessous, le profil etant deja hors disque et le
        // stockage des permissions suivant le profil, cette ligne n'apporte
        // rien de plus — elle est donc simplement absente.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        m_profile->setPersistentPermissionsPolicy(
            QWebEngineProfile::PersistentPermissionsPolicy::StoreInMemory);
#endif
    } else {
        m_profile = new QWebEngineProfile(QStringLiteral("BrowserECO"), this);
        m_profile->setPersistentStoragePath(
            QDir(settingsDir).filePath(QStringLiteral("WebEngineProfile")));
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
        m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
        m_profile->setHttpCacheMaximumSize(100 * 1024 * 1024);
    }

    // Services : les bases suivent le dossier de profil (--data-dir), sinon
    // elles restaient a cote du binaire et se retrouvaient hors du dossier
    // choisi par l'utilisateur.
    m_cache = new CacheManager(QDir(settingsDir).filePath(QStringLiteral("cache.db")),
                               100 * 1024 * 1024);
    m_history = new HistoryManager(QDir(settingsDir).filePath(QStringLiteral("history.db")));
    m_search = new SearchEngineManager(this);
    m_serpGuard = new SerpGuard(this);

    // AdBlocker : les listes de filtres sont cherchees a cote du binaire,
    // puis dans le prefix d'installation, puis via BROWSERECO_FILTERS.
    // Si aucune n'est trouvee, on le DIT (au lieu d'annoncer « Filtres : 0 »
    // indefiniment en laissant croire que le blocage est actif).
    const QStringList filterCandidates = {
        appDir + QStringLiteral("/Filters"),
        appDir + QStringLiteral("/../Filters"),
        settingsDir + QStringLiteral("/Filters"),
        QDir(QStringLiteral("/usr/share/BrowserECO/Filters")).absolutePath(),
        qEnvironmentVariable("BROWSERECO_FILTERS"),
    };
    QString filterDir;
    for (const QString &c : filterCandidates) {
        if (!c.isEmpty() && QDir(c).exists()) { filterDir = c; break; }
    }
    m_adblocker.loadFilters(filterDir.isEmpty() ? QString() : filterDir);

    // Serveur d'images local : c'est lui qui sert les images compressées.
    // Il doit tourner AVANT l'intercepteur, qui a besoin de son adresse.
    m_imageServer = new EcoImageServer(this);
    if (m_imageServer->start())
        connect(m_imageServer, &EcoImageServer::imageServed, this, &MainWindow::updateStats);

    m_interceptor = new EcoInterceptor(&m_adblocker, this);
    m_profile->setUrlRequestInterceptor(m_interceptor);
    applyQuality(m_quality, false);   // avant tout chargement : branche l'état
    connect(m_interceptor, &EcoInterceptor::statsChanged, this, &MainWindow::onBlocked);
    connect(m_serpGuard, &SerpGuard::blocked, this, &MainWindow::onSerpBlocked);
    connect(m_serpGuard, &SerpGuard::succeeded, this, &MainWindow::adoptWorkingEngine);

    // Rafraichissement des compteurs : uniquement quand quelque chose a change
    // (navigation, blocage, ouverture/fermeture d'onglet). Une interrogation
    // SQLite toutes les 350 ms sur le thread GUI faisait grossir le WAL pour rien.
    m_statsTimer = new QTimer(this);
    m_statsTimer->setSingleShot(true);
    m_statsTimer->setInterval(200);
    connect(m_statsTimer, &QTimer::timeout, this, &MainWindow::pollStats);

    // Ecriture groupee des reglages (zoom au molette, curseur de qualite...)
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, &MainWindow::saveSettings);

    // Téléchargements : dossier ext4 safe
    QString dlPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dlPath.isEmpty()) dlPath = QDir::homePath() + "/Téléchargements";
    if (!QDir(dlPath).exists()) {
        dlPath = QDir::homePath() + "/Downloads";
        if (!QDir(dlPath).exists()) dlPath = QDir::homePath();
    }
    QDir().mkpath(dlPath);
    m_profile->setDownloadPath(dlPath);
    connect(m_profile, &QWebEngineProfile::downloadRequested, this, &MainWindow::onDownloadRequested);

    setWindowTitle(kAppName);
    if (QFile::exists(appDir + "/app_icon_512.png")) setWindowIcon(QIcon(appDir + "/app_icon_512.png"));
    else if (QFile::exists(appDir + "/app.ico")) setWindowIcon(QIcon(appDir + "/app.ico"));

    resize(1180, 780);
    setMinimumSize(940, 560);

    setupUi();
    setupConnections();
    loadSettings();
    applyWebSettings();
    updateStats();

    // Premier onglet (ou session restauree)
    if (m_restoreSession && !m_isPrivate && !m_session.isEmpty()) {
        // settings.txt est un fichier texte libre : seule une liste blanche de
        // schémas est acceptee, sinon un fichier altere pourrait faire charger
        // javascript:, file: ou data: au demarrage.
        const QStringList urls = m_session;
        m_session.clear();
        int skipped = 0;
        for (const QString &u : urls) {
            const QUrl q(u);
            if (isWebUrl(q) && m_tabs->count() < 12) {
                createNewTabView()->setUrl(q);
            } else {
                ++skipped;
            }
        }
        if (m_tabs->count() > 0) m_tabs->setCurrentIndex(0);
        else newTab(QUrl());
        if (skipped > 0)
            setStatus(tr("%1 entrée(s) de session ignorée(s) (URL invalide).").arg(skipped), 6000);
    } else {
        newTab(QUrl());
    }
    if (m_zoom != 1.0) setZoom(m_zoom);

    // Centre sur l'ecran de la fenetre (primaryScreen() peut etre null en QPA
    // headless/offscreen ou sur une machine sans sortie -> arret propre).
    if (QScreen *scr = screen()) {
        const QRect avail = scr->availableGeometry();
        move(avail.center() - QPoint(width() / 2, height() / 2));
    }
}

MainWindow::~MainWindow() {
    m_shuttingDown = true;
    if (m_saveTimer) m_saveTimer->stop();
    if (m_statsTimer) m_statsTimer->stop();
    saveSettings();

    // Ordre de destruction indispensable avec QtWebEngine :
    // 1. fermer le popup de suggestions (enfant de la fenetre)
    // 2. detruire les pages/ vues AVANT que le profil ne soit detruit
    // Sinon QtWebEngine detruit son noyau alors qu'il reste des pages vivantes
    // -> "Release of profile requested but WebEnginePage still not deleted" + SIGSEGV.
    if (m_omni) m_omni->dismissSuggestions();
    if (m_ecoPanel) m_ecoPanel->hide();   // deleteLater() serait inoperant ici

    while (m_tabs && m_tabs->count() > 0) {
        QWidget *page = m_tabs->widget(0);
        m_tabs->removeTab(0);
        for (int i = 0; i < m_tabInfos.size(); ++i) {
            if (m_tabInfos[i]->view == page) { delete m_tabInfos.takeAt(i); break; }
        }
        if (page) delete page;   // detruit la QWebEnginePage associee
    }
    qDeleteAll(m_tabInfos);
    m_tabInfos.clear();

    // Pas de clearHttpCache() ici : c'est un E/S bloquante sur le thread GUI,
    // pendant des secondes sur un cache de 100 Mo. Le cache est vide au demarrage
    // du processus suivant de toute façon.
    delete m_cache;
    delete m_history;
    m_cache = nullptr;
    m_history = nullptr;
}

bool MainWindow::isWebUrl(const QUrl &url) const {
    if (!url.isValid() || url.isEmpty()) return false;
    const QString s = url.scheme().toLower();
    return s == QLatin1String("http") || s == QLatin1String("https");
}

EcoTab* MainWindow::tabInfoForView(const QWebEngineView *view) const {
    if (!view) return nullptr;
    for (EcoTab *info : m_tabInfos)
        if (info->view == view) return info;
    return nullptr;
}

int MainWindow::tabCountOfView(const QWebEngineView *view) const {
    return m_tabInfos.count(tabInfoForView(view));
}

void MainWindow::openUrlAtStartup(const QString &urlOrText) {
    if (urlOrText.trimmed().isEmpty()) return;
    const QUrl resolved = m_search->resolve(urlOrText.trimmed());
    if (!isWebUrl(resolved)) return;
    // L'onglet courant encore vierge est reutilise, sinon nouvel onglet.
    if (EcoTab *info = currentTabInfo()) {
        const QString u = info->view ? info->view->url().toString() : QString();
        if (u.isEmpty() || u == QLatin1String("about:home")) {
            navigateCurrent(resolved);
            return;
        }
    }
    newTab(resolved);
}

// ---------------------------------------------------------------------------
// Interface
// ---------------------------------------------------------------------------
void MainWindow::setupUi() {
    m_central = new QWidget(this);
    setCentralWidget(m_central);
    m_mainLayout = new QVBoxLayout(m_central);
    m_mainLayout->setContentsMargins(0,0,0,0);
    m_mainLayout->setSpacing(0);

    setStyleSheet(QStringLiteral(R"(
        QWidget { color:#E8E8F0; }
        QToolButton#navBtn {
            background:transparent; border:none; border-radius:8px; padding:0;
        }
        QToolButton#navBtn:hover { background:#22243C; }
        QToolButton#navBtn:pressed { background:#2A2D4A; }
        QToolButton#navBtn:disabled { background:transparent; }
        QToolButton#ecoBtn {
            background:#0F2A2A; color:#7BFFE4; border:1px solid #0E5F55;
            border-radius:16px; padding:5px 11px; font-size:12px; font-weight:600;
        }
        QToolButton#ecoBtn:hover { background:#13403D; border-color:#00D4AA; }
        QToolButton#ecoBtnOff { background:#1B1B2C; color:#7A7F9E; border:1px solid #2C3150; }
        QToolButton#ecoBtnOff:hover { background:#242740; }
        QToolButton#menuBtn { background:transparent; border:none; border-radius:8px; padding:0; }
        QToolButton#menuBtn:hover { background:#22243C; }
        QLineEdit { background:transparent; border:none; color:#E8E8F0; font-size:13px; padding:0; }
        QLineEdit::placeholder { color:#5A5F80; }
        QTabWidget::pane { border:none; background:#0B0B14; }
        QTabBar { background:#12121F; }
        QTabBar::tab {
            background:#171728; color:#7A7F9E; padding:7px 10px 7px 10px;
            border:1px solid #20223A; border-bottom:none;
            margin-right:2px; border-top-left-radius:9px; border-top-right-radius:9px;
            min-width:60px; max-width:220px;
        }
        QTabBar::tab:hover { background:#1F2036; color:#C6C9DC; }
        QTabBar::tab:selected { background:#0F0F1C; color:#FFFFFF; border-color:#2A2D4A; }
        QTabBar::close-button { subcontrol-position:right; width:0px; height:0px; margin:0; }
        QMenu {
            background:#151525; color:#E8E8F0; border:1px solid #2E3350;
            border-radius:10px; padding:6px;
        }
        QMenu::item { padding:7px 30px 7px 12px; border-radius:6px; }
        QMenu::item:selected { background:#243046; }
        QMenu::item:disabled { color:#5A5F80; }
        QMenu::separator { height:1px; background:#22243A; margin:6px 10px; }
        QDialog { background:#0F0F17; }
        QLabel { font-family:"DejaVu Sans","Noto Sans","Liberation Sans",sans-serif; }
        QLabel#keyHint { font-family:"DejaVu Sans Mono","Liberation Mono","Noto Sans Mono",monospace; color:#00D4AA; }
        QLineEdit#findEdit {
            background:#1A1A2E; border:1px solid #2A2A45; border-radius:7px;
            padding:4px 8px; color:#E8E8F0; selection-background-color:#00D4AA;
        }
        QLineEdit#findEdit:focus { border-color:#00D4AA; }
        QPushButton {
            background:#242440; color:#E8E8F0; border:1px solid #2A2A45;
            border-radius:7px; padding:6px 12px;
        }
        QPushButton:hover { background:#2E2E50; border-color:#00A88A; }
        QPushButton[destructive="true"] { background:#3A1520; border-color:#6E1F32; color:#FF8FA3; }
        QPushButton[destructive="true"]:hover { background:#4E1B29; }
    )"));

    // ---------------------------------------------------------------- toolbar
    m_toolBar = new QWidget(m_central);
    m_toolBar->setFixedHeight(46);
    m_toolBar->setStyleSheet(QStringLiteral("background:#0F0F1A; border-bottom:1px solid #22243A;"));
    m_toolLayout = new QHBoxLayout(m_toolBar);
    m_toolLayout->setContentsMargins(8, 7, 8, 7);
    m_toolLayout->setSpacing(5);

    auto mkNav = [&](const QIcon &ic, const QString &tip){
        auto *b = new QToolButton(m_toolBar);
        b->setObjectName(QStringLiteral("navBtn"));
        b->setIcon(ic);
        b->setIconSize(QSize(17, 17));
        b->setFixedSize(32, 32);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        return b;
    };
    m_backBtn    = mkNav(Icons::back(),    tr("Retour  (Alt+←)"));
    m_forwardBtn = mkNav(Icons::forward(), tr("Avancer  (Alt+→)"));
    m_reloadBtn  = mkNav(Icons::reload(),  tr("Recharger  (F5)"));
    m_homeBtn    = mkNav(Icons::home(),    tr("Page d'accueil  (F6)"));

    m_omni = new OmniBox(m_toolBar);
    m_omni->setMinimumWidth(240);
    m_omni->setFixedHeight(32);
    m_omni->setSearchManager(m_search);
    m_omni->setHistoryManager(m_history);
    m_omni->setStyleSheet(QStringLiteral(
        "OmniBox{background:#1A1A2E;border:1px solid #262A48;border-radius:16px;}"
        "OmniBox:hover{border-color:#31365A;}"
        "OmniBox QLineEdit:focus{background:#1E1E36;}"));

    m_ecoBtn = new QToolButton(m_toolBar);
    m_ecoBtn->setObjectName(QStringLiteral("ecoBtn"));
    m_ecoBtn->setCursor(Qt::PointingHandCursor);
    m_ecoBtn->setIcon(Icons::leaf());
    m_ecoBtn->setIconSize(QSize(15, 15));
    m_ecoBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_ecoBtn->setText(tr("Eco"));
    m_ecoBtn->setToolTip(tr("Panneau Data saver  (Ctrl+E)"));

    // Bouton bouclier : c'etait un controle mort (desactive en permanence, donc
    // inutile et sans effet pour un lecteur d'ecran). Il devient le vrai
    // interrupteur du mode economie de donnees, avec un etat visible.
    m_privacyBtn = mkNav(Icons::shield(), tr("Économie de données"));
    m_privacyBtn->setCheckable(true);
    m_privacyBtn->setAccessibleName(tr("Économie de données"));
    m_privacyBtn->setToolTip(tr("Économie de données : Save-Data + bloqueurs (Ctrl+E)\n"
                                "Cliquer pour activer ou désactiver\n"
                                "Clic droit sur un lien = nouvel onglet, copie, code source…"));

    m_menuBtn = new QToolButton(m_toolBar);
    m_menuBtn->setObjectName(QStringLiteral("menuBtn"));
    m_menuBtn->setIcon(Icons::menu());
    m_menuBtn->setIconSize(QSize(17, 17));
    m_menuBtn->setFixedSize(32, 32);
    m_menuBtn->setCursor(Qt::PointingHandCursor);
    m_menuBtn->setToolTip(tr("Menu"));

    m_toolLayout->addWidget(m_backBtn);
    m_toolLayout->addWidget(m_forwardBtn);
    m_toolLayout->addWidget(m_reloadBtn);
    m_toolLayout->addWidget(m_homeBtn);
    m_toolLayout->addWidget(m_omni, 1);
    m_toolLayout->addWidget(m_privacyBtn);
    m_toolLayout->addWidget(m_ecoBtn);
    m_toolLayout->addWidget(m_menuBtn);
    m_mainLayout->addWidget(m_toolBar);

    // ---------------------------------------------------------------- onglets
    m_tabs = new QTabWidget(m_central);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabBar = m_tabs->tabBar();
    m_tabBar->setExpanding(false);
    m_tabBar->setDrawBase(false);
    m_tabBar->setUsesScrollButtons(true);
    connect(m_tabBar, &QTabBar::tabBarDoubleClicked, this, &MainWindow::onTabBarDoubleClicked);

    // Bouton "+" en fin de barre d'onglets (convention navigateur)
    auto *newTabBtn = new QToolButton(m_tabBar);
    newTabBtn->setObjectName(QStringLiteral("navBtn"));
    newTabBtn->setIcon(Icons::plus());
    newTabBtn->setIconSize(QSize(15, 15));
    newTabBtn->setFixedSize(26, 24);
    newTabBtn->setCursor(Qt::PointingHandCursor);
    newTabBtn->setToolTip(tr("Nouvel onglet  (Ctrl+T)"));
    connect(newTabBtn, &QToolButton::clicked, this, [this]{ newTab(); });
    m_tabBar->setTabButton(m_tabBar->count(), QTabBar::RightSide, newTabBtn);
    m_mainLayout->addWidget(m_tabs, 1);

    // ---------------------------------------------------------------- barre de statut
    QWidget *status = new QWidget(m_central);
    status->setFixedHeight(26);
    status->setStyleSheet(QStringLiteral("background:#0B0B14; border-top:1px solid #22243A;"));
    QHBoxLayout *sl = new QHBoxLayout(status);
    sl->setContentsMargins(10, 0, 10, 0);
    sl->setSpacing(12);

    m_findBar = new QWidget(status);
    auto *fl = new QHBoxLayout(m_findBar);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(4);
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setObjectName(QStringLiteral("findEdit"));
    m_findEdit->setFixedWidth(190);
    m_findEdit->setPlaceholderText(tr("Rechercher dans la page"));
    m_findInfo = new QLabel(m_findBar);
    m_findInfo->setStyleSheet(QStringLiteral("color:#8C90AE;font-size:11px;"));
    auto *prev = new QToolButton(m_findBar);
    prev->setObjectName(QStringLiteral("navBtn"));
    prev->setIcon(Icons::back()); prev->setIconSize(QSize(13,13));
    prev->setFixedSize(22, 22); prev->setCursor(Qt::PointingHandCursor);
    prev->setToolTip(tr("Précédent  (Maj+Entrée)"));
    auto *next = new QToolButton(m_findBar);
    next->setObjectName(QStringLiteral("navBtn"));
    next->setIcon(Icons::forward()); next->setIconSize(QSize(13,13));
    next->setFixedSize(22, 22); next->setCursor(Qt::PointingHandCursor);
    next->setToolTip(tr("Suivant  (Entrée)"));
    auto *closeFind = new QToolButton(m_findBar);
    closeFind->setObjectName(QStringLiteral("navBtn"));
    closeFind->setIcon(Icons::close()); closeFind->setIconSize(QSize(12,12));
    closeFind->setFixedSize(22, 22); closeFind->setCursor(Qt::PointingHandCursor);
    closeFind->setToolTip(tr("Fermer  (Échap)"));
    fl->addWidget(m_findEdit);
    fl->addWidget(m_findInfo);
    fl->addWidget(prev); fl->addWidget(next); fl->addWidget(closeFind);
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this]{ findInPage(false); });
    connect(prev, &QToolButton::clicked, this, [this]{ findInPage(true); });
    connect(next, &QToolButton::clicked, this, [this]{ findInPage(false); });
    connect(closeFind, &QToolButton::clicked, this, &MainWindow::closeFindBar);
    m_findBar->hide();

    m_statusLabel = new QLabel(status);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#7A7F9E;font-size:11px;"));
    m_statsLabel = new QLabel(status);
    m_statsLabel->setStyleSheet(QStringLiteral("color:#00D4AA;font-size:11px;font-weight:600;"));
    m_zoomLabel = new QLabel(status);
    m_zoomLabel->setStyleSheet(QStringLiteral("color:#5A5F80;font-size:11px;"));
    m_zoomLabel->setToolTip(tr("Zoom de la page (Ctrl+molette, Ctrl+0 pour 100 %)"));
    m_progress = new QProgressBar(status);
    m_progress->setFixedSize(110, 10);
    m_progress->setTextVisible(false);
    m_progress->setStyleSheet(QStringLiteral(
        "QProgressBar{background:#1A1A2E;border:1px solid #2A2A45;border-radius:5px;}"
        "QProgressBar::chunk{background:#00D4AA;border-radius:4px;}"));
    m_progress->hide();

    sl->addWidget(m_findBar);
    sl->addWidget(m_statusLabel, 1);
    sl->addWidget(m_statsLabel);
    sl->addWidget(m_zoomLabel);
    sl->addWidget(m_progress);
    m_mainLayout->addWidget(status);
}

void MainWindow::setupConnections() {
    connect(m_backBtn, &QToolButton::clicked, [this]{
        if (auto v = currentView()) v->back();
    });
    connect(m_forwardBtn, &QToolButton::clicked, [this]{
        if (auto v = currentView()) v->forward();
    });
    connect(m_reloadBtn, &QToolButton::clicked, this, [this]{
        auto v = currentView();
        if (!v) return;
        if (v->page()->isLoading()) v->stop();   // l'icone affiche deja « Arreter »
        else v->reload();
    });
    connect(m_homeBtn, &QToolButton::clicked, this, [this]{
        if (auto v = currentView()) v->setHtml(homePageHtml(), QUrl("about:home"));
    });
    connect(m_omni, &OmniBox::urlSubmitted, this, &MainWindow::onUrlEntered);
    connect(m_omni, &OmniBox::engineMenuRequested, this,
            [this](const QPoint &p) { showEngineMenuAt(p); });
    connect(m_privacyBtn, &QToolButton::clicked, this, [this](bool on){
        toggleDataSaver(on);
    });
    connect(m_ecoBtn, &QToolButton::clicked, this, [this]{
        if (m_ecoPanel && m_ecoPanel->isVisible()) { m_ecoPanel->hide(); return; }
        showEcoPanelAt(m_ecoBtn->mapToGlobal(QPoint(0, m_ecoBtn->height() + 6)));
    });
    connect(m_menuBtn, &QToolButton::clicked, this, [this]{
        showMenuAt(m_menuBtn->mapToGlobal(QPoint(0, m_menuBtn->height() + 4)));
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabBar, &QTabBar::tabMoved, this, &MainWindow::onTabMoved);

    // Raccourcis. Le libelle est enregistre AU MEME ENDROIT que la touche :
    // la fenetre F1 affiche exactement cette liste, donc impossible d'annoncer
    // un raccourci inexistant ni d'en oublier un.
    struct Bind {
        QKeySequence keys;
        QString label;      // texte affiche dans F1 (vide = non annonce)
        bool eco = false;   // range dans la section « Economie de donnees »
        std::function<void()> fn;
    };
    const QList<Bind> binds = {
        { QKeySequence(QStringLiteral("Ctrl+T")),     tr("Nouvel onglet"), false, [this]{ newTab(); } },
        { QKeySequence(QStringLiteral("Ctrl+W")),     tr("Fermer l'onglet"), false, [this]{ closeCurrentTab(); } },
        { QKeySequence(QStringLiteral("Ctrl+Tab")),   tr("Onglet suivant"), false, [this]{ if (m_tabs->count() > 1) m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count()); } },
        { QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), tr("Onglet précédent"), false, [this]{ if (m_tabs->count() > 1) m_tabs->setCurrentIndex((m_tabs->currentIndex() - 1 + m_tabs->count()) % m_tabs->count()); } },
        { QKeySequence(QStringLiteral("Ctrl+L")),     tr("Barre d'adresse"), false, [this]{ m_omni->selectAll(); } },
        { QKeySequence(QStringLiteral("Alt+Left")),   tr("Retour"), false, [this]{ if (auto v = currentView()) v->back(); } },
        { QKeySequence(QStringLiteral("Alt+Right")),  tr("Avancer"), false, [this]{ if (auto v = currentView()) v->forward(); } },
        { QKeySequence(QStringLiteral("F5")),         tr("Recharger (ou arrêter le chargement)"), false, [this]{
              auto v = currentView();
              if (!v) return;
              if (v->page()->isLoading()) v->stop(); else v->reload();
          } },
        { QKeySequence(QStringLiteral("Ctrl+Shift+R")), tr("Recharger sans cache"), false, [this]{ if (auto v = currentView()) { m_profile->clearHttpCache(); v->reload(); } } },
        { QKeySequence(Qt::Key_Escape),              tr("Arrêter le chargement / fermer les suggestions"), false, [this]{
              if (m_findBar && m_findBar->isVisible()) { closeFindBar(); return; }
              if (m_omni && m_omni->suggestionsVisible()) { m_omni->dismissSuggestions(); return; }
              if (auto v = currentView()) v->stop();
          } },
        { QKeySequence(QStringLiteral("Ctrl+F")),     tr("Rechercher dans la page"), false, [this]{ showFindBar(); } },
        // F6 doit passer par setHtml : setUrl("about:home") affichait la page
        // d'erreur de Chromium (« impossible d'atteindre cette page »), car rien
        // ne traitait le schema about:.
        { QKeySequence(QStringLiteral("F6")),         tr("Page d'accueil"), false, [this]{ if (auto v = currentView()) v->setHtml(homePageHtml(), QUrl("about:home")); } },
        { QKeySequence(QStringLiteral("F11")),        tr("Plein écran"), false, [this]{ if (isFullScreen()) showNormal(); else showFullScreen(); } },
        { QKeySequence(Qt::CTRL | Qt::Key_0),        tr("Zoom 100 %"), false, [this]{ setZoom(1.0); } },
        { QKeySequence(QStringLiteral("Ctrl+=")),     tr("Zoom avant"), false, [this]{ setZoom(m_zoom + 0.1); } },
        { QKeySequence(QStringLiteral("Ctrl++")),     tr("Zoom avant"), false, [this]{ setZoom(m_zoom + 0.1); } },
        { QKeySequence(QStringLiteral("Ctrl+-")),     tr("Zoom arrière"), false, [this]{ setZoom(m_zoom - 0.1); } },
        { QKeySequence(QStringLiteral("Ctrl+E")),     tr("Panneau Data saver"), true, [this]{ showEcoPanelAt(m_ecoBtn->mapToGlobal(QPoint(0, m_ecoBtn->height() + 6))); } },
        { QKeySequence(QStringLiteral("Ctrl+H")),     tr("Historique"), true, [this]{ showHistory(); } },
        { QKeySequence(QStringLiteral("Ctrl+J")),     tr("Téléchargements"), true, [this]{ showDownloads(); } },
        { QKeySequence(QStringLiteral("F1")),         tr("Cette fenêtre"), true, [this]{ showShortcuts(); } },
    };
    for (const Bind &b : binds) {
        auto *s = new QShortcut(b.keys, this);
        connect(s, &QShortcut::activated, this, b.fn);
        const ShortcutRow row{ b.keys.toString(QKeySequence::NativeText), b.label, b.eco };
        (b.eco ? m_ecoShortcuts : m_navShortcuts).append(row);
    }
    // Alt+1..9 : changer de moteur
    m_ecoShortcuts.append(ShortcutRow{ tr("Alt+1 … Alt+9"),
                                        tr("Changer de moteur de recherche"), true });
    // Alt+1..9 : changer de moteur
    for (int i = 0; i < 9; ++i) {
        auto *s = new QShortcut(QKeySequence(QStringLiteral("Alt+%1").arg(i + 1)), this);
        connect(s, &QShortcut::activated, this, [this, i]{
            const auto &engines = SearchEngineManager::registry();
            if (i < engines.size()) {
                chooseSearchEngine(engines.at(i).id);
                setStatus(tr("Moteur de recherche : %1  (%2)").arg(engines.at(i).label, engines.at(i).weightNote), 4000);
            }
        });
    }
}

// ---------------------------------------------------------------------------
// Onglets
// ---------------------------------------------------------------------------
QWebEngineView* MainWindow::createNewTabView() {
    QWebEngineView *view = new QWebEngineView(this);
    BrowserPage *page = new BrowserPage(m_profile, this, view);
    view->setPage(page);
    view->setZoomFactor(m_zoom);
    view->setFocusPolicy(Qt::StrongFocus);
    view->installEventFilter(this); // Ctrl+molette = zoom
    // Clic droit : sans ce menu, il n'y avait aucun moyen de copier un lien,
    // ouvrir dans un onglet, voir la source ou inspecter.

    EcoTab *info = new EcoTab();
    info->view = view;
    info->title = tr("Nouvel onglet");
    m_tabInfos.append(info);

    int idx = m_tabs->addTab(view, info->title);
    m_tabs->setCurrentIndex(idx);
    m_tabBar->setTabIcon(idx, Icons::globe().pixmap(14, 14));
    // La croix suit l'onglet (les onglets sont deplacables et l'ordre des
    // index change) : on memorise la VUE, pas un index figé au clic.
    auto *close = new QToolButton(m_tabBar);
    close->setObjectName(QStringLiteral("tabClose"));
    close->setIcon(Icons::close());
    close->setIconSize(QSize(11, 11));
    close->setFixedSize(18, 18);
    close->setCursor(Qt::PointingHandCursor);
    close->setToolTip(tr("Fermer l'onglet  (Ctrl+W)"));
    close->setAccessibleName(tr("Fermer l'onglet"));
    close->setStyleSheet(QStringLiteral(
        "QToolButton#tabClose{background:transparent;border:none;border-radius:5px;padding:0;}"
        "QToolButton#tabClose:hover{background:#E8114B;}"));
    connect(close, &QToolButton::clicked, this, [this, view]{
        closeTab(m_tabs->indexOf(view));
    });
    m_tabBar->setTabButton(idx, QTabBar::RightSide, close);

    connect(view, &QWebEngineView::titleChanged, this, &MainWindow::onTitleChanged);
    connect(view, &QWebEngineView::urlChanged, this, &MainWindow::onUrlChanged);
    connect(view, &QWebEngineView::iconChanged, this, [this, view]{ updateTabChrome(view); });
    connect(view, &QWebEngineView::loadFinished, this, &MainWindow::onLoadFinished);
    connect(view, &QWebEngineView::loadProgress, this, &MainWindow::onLoadProgress);
// Crash du renderer (GPU, onglet, memoire) : sans ce gestionnaire l'onglet
    // restait blanc indefiniment, sans aucun message. La vue est capturee car
    // QWebEnginePage n'expose pas son QWidget parent.
    connect(page, &QWebEnginePage::renderProcessTerminated, this,
            [this, view](QWebEnginePage::RenderProcessTerminationStatus st, int code){
        handleRenderProcessTerminated(view, st, code);
    });
    // target=_blank / window.open -> onglet courant comme avant
    connect(page, &QWebEnginePage::newWindowRequested, this, [this](QWebEngineNewWindowRequest &req){
        if (auto *v = currentView()) req.openIn(v->page());
        else newTab(req.requestedUrl());
    });
    return view;
}

void MainWindow::newTab(const QUrl &url) {
    QWebEngineView *view = createNewTabView();
    if (url.isValid() && !url.isEmpty()) {
        view->load(url);
    } else {
        view->setHtml(homePageHtml(), QUrl("about:home"));
        m_omni->setText(QString());
    }
    m_omni->selectAll();
    updateTabChrome(view);
}

void MainWindow::closeTab(int index) {
    if (!m_tabs || index < 0 || index >= m_tabs->count()) return;
    // Dernier onglet : on en crée un avant, comme un vrai navigateur. newTab()
    // change l'onglet courant et peut donc faire bouger les index : on travaille
    // sur la vue elle-même, pas sur une position.
    if (m_tabs->count() <= 1) newTab();
    QWidget *w = m_tabs->widget(index);
    if (!w) return;
    const int pos = m_tabs->indexOf(w);
    if (pos < 0) return;
    m_tabs->removeTab(pos);
    for (int i = 0; i < m_tabInfos.size(); ++i)
        if (m_tabInfos[i]->view == w) { delete m_tabInfos.takeAt(i); break; }
    w->deleteLater();
    if (m_tabs->count() == 0) newTab();
    updateStats();
}

void MainWindow::closeCurrentTab() {
    closeTab(m_tabs ? m_tabs->currentIndex() : -1);
}

/* m_tabInfos n'est pas reordonne quand l'utilisateur deplace un onglet : la
   correspondance se fait par vue (voir tabInfoForView). Cette fonction n'existe
   que pour rafraichir ce qui depend de l'ordre (barre d'onglets, chrome). */
void MainWindow::onTabMoved(int from, int to)
{
    if (from == to) return;
    if (EcoTab *info = currentTabInfo()) updateTabChrome(info->view);
    updateStats();
}

void MainWindow::onTabBarDoubleClicked(int index) {
    if (index < 0) newTab();
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------
void MainWindow::onUrlEntered(const QUrl &url) {
    if (url.isEmpty()) return;
    QString engine;
    QUrlQuery q(url);
    QString query = q.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded);
    if (query.isEmpty()) query = q.queryItemValue(QStringLiteral("query"), QUrl::FullyDecoded);

    if (SerpGuard::isSearchPage(url)) {
        // Identifie le moteur reellement sollicite (bang ou moteur courant)
        QString id = SearchEngineManager::engineForHost(url);
        if (id.isEmpty()) id = m_search->engineId();
        m_serpEngine = id;
        m_lastQuery = query.isEmpty() ? m_omni->text().trimmed() : query;
        m_serpTried = QStringList{ id };
    }
    navigateCurrent(url);
    m_omni->dismissSuggestions();
    m_omni->lineEdit()->clearFocus();
}

void MainWindow::navigateCurrent(const QUrl &url) {
    m_omni->dismissSuggestions();
    if (auto v = currentView()) v->load(url);
}

// Ctrl+molette = zoom de la page (filtre installe sur chaque vue)
bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    // Clic droit : on affiche NOTRE menu et on consomme l'evenement, sinon Qt
    // affiche son menu par defaut (en anglais, et il n'a pas d'entree eco).
    if (event->type() == QEvent::ContextMenu) {
        auto *me = qobject_cast<QWebEngineView *>(obj);
        auto *ce = static_cast<QContextMenuEvent *>(event);
        if (me) { showPageContextMenu(me, ce->globalPos()); return true; }
    }
    if (event->type() == QEvent::Wheel) {
        auto *we = qobject_cast<QWebEngineView *>(obj);
        if (we) {
            auto *me = static_cast<QWheelEvent *>(event);
            if (me->modifiers() & Qt::ControlModifier) {
                setZoom(m_zoom + (me->angleDelta().y() > 0 ? 0.1 : -0.1));
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onTabChanged(int index) {
    if (index < 0) return;
    QWebEngineView *v = qobject_cast<QWebEngineView *>(m_tabs->widget(index));
    if (!v) return;
    m_omni->dismissSuggestions();
    // setCurrentUrl() n'ecrit que si le champ est vide ou focalise : sans
    // version forcee, changer d'onglet laissait l'URL de l'onglet precedent.
    m_omni->setCurrentUrlForced(v->url());
    setWindowTitle(tabTitleFor(v->url(), v->title()) + QStringLiteral(" — ") + QLatin1String(kAppName));
    if (m_findBar && m_findBar->isVisible()) m_findInfo->clear();  // « 3 / 7 » != autre page
    updateNavigationActions();
    updateZoomLabel();
}

void MainWindow::updateNavigationActions() {
    auto v = currentView();
    if (!v) return;
    m_backBtn->setEnabled(v->page()->history()->canGoBack());
    m_forwardBtn->setEnabled(v->page()->history()->canGoForward());
    m_reloadBtn->setIcon(v->page()->isLoading() ? Icons::stop() : Icons::reload());
    m_reloadBtn->setToolTip(v->page()->isLoading() ? tr("Arrêter le chargement  (Échap)")
                                                   : tr("Recharger  (F5)"));
}

void MainWindow::onTitleChanged(const QString &title) {
    QWebEngineView *view = qobject_cast<QWebEngineView *>(sender());
    if (!view) return;
    int idx = m_tabs->indexOf(view);
    if (idx == -1) return;
    QString t = tabTitleFor(view->url(), title);
    if (t.length() > 32) t = t.left(29) + QStringLiteral("…");
    m_tabs->setTabText(idx, t);
    for (auto *info : m_tabInfos) if (info->view == view) info->title = t;
    if (view == currentView()) {
        const QString full = tabTitleFor(view->url(), title);
        setWindowTitle(full + QStringLiteral(" — ") + QLatin1String(kAppName));
    }
}

void MainWindow::updateTabChrome(QWebEngineView *view)
{
    const int idx = m_tabs->indexOf(view);
    if (idx == -1) return;
    const QUrl u = view->url();
    if (m_favicons && !view->icon().isNull()) {
        m_tabBar->setTabIcon(idx, view->icon().pixmap(14, 14));
    } else {
        m_tabBar->setTabIcon(idx, Icons::globe().pixmap(14, 14));
    }
    if (view == currentView()) {
        m_omni->setCurrentUrlForced(u);
        updateZoomLabel();
    }
}

void MainWindow::onUrlChanged(const QUrl &url) {
    QWebEngineView *view = qobject_cast<QWebEngineView *>(sender());
    if (!view) return;
    updateTabChrome(view);
    if (view != currentView()) return;
    QString s = url.toString();
    if (s == QLatin1String("about:home")) s.clear();
    // Ne pas ecraser la saisie en cours : l'utilisateur est en train de taper.
    if (!m_omni->lineEdit()->hasFocus())
        m_omni->setText(s.isEmpty() ? QString() : url.toDisplayString(QUrl::RemovePassword | QUrl::NormalizePathSegments));
    if (!m_isPrivate && isWebUrl(url) && tabCountOfView(view) == 0)
        m_history->addVisit(url.toString(), view->title());
    // L'exemption d'images vaut POUR LA NAVIGATION EN COURS, pas pour la
    // session : elle est activee sur une page de verification anti-bot (sinon
    // l'image du captcha reste bloquee et la verification insoluble) et levee
    // des que l'on visite un site ordinaire (« Images OFF » redevient vrai).
    // Elle est positionnee ici, avant que la page ne commence a charger.
    if (m_interceptor->allowChallengeImages() != EcoInterceptor::isChallengeHost(url.host()))
        m_interceptor->setAllowChallengeImages(EcoInterceptor::isChallengeHost(url.host()));
    updateNavigationActions();
    updateStats();
}

void MainWindow::onLoadFinished(bool ok) {
    auto v = qobject_cast<QWebEngineView *>(sender());
    if (v == currentView()) m_progress->hide();
    updateNavigationActions();
    refreshStatusLine();   // l'historique vient de changer
    updateStats();
    if (!ok) {
        // Pas de remplacement du contenu : !ok couvre aussi les chargements
        // interrompus (Echap, nouvelle navigation), et afficher une page
        // d'erreur a ce moment-la detruirait la page reellement chargee.
        const QString u = v ? v->url().toString() : QString();
        setStatus(tr("Page non chargée (connexion ou réseau) : %1").arg(u), 10000);
        return;
    }
    if (!v) v = currentView();
    if (v) m_serpGuard->probe(v, v->url(), m_serpEngine);
}

/* Crash du renderer : l'onglet resterait blanc indefiniment. On affiche une
   page d'erreur avec un bouton Recharger, comme le fait tout navigateur. */
void MainWindow::handleRenderProcessTerminated(QWebEngineView *view,
                                               QWebEnginePage::RenderProcessTerminationStatus status,
                                               int exitCode)
{
    Q_UNUSED(exitCode)
    if (!view) return;
    QString why;
    switch (status) {
    case QWebEnginePage::RenderProcessTerminationStatus::NormalTerminationStatus:
        return;   // fin normale du renderer : rien a signaler
    case QWebEnginePage::RenderProcessTerminationStatus::AbnormalTerminationStatus:
        why = tr("le processus de rendu s'est arrêté anormalement"); break;
    case QWebEnginePage::RenderProcessTerminationStatus::CrashedTerminationStatus:
        why = tr("le processus de rendu a planté"); break;
    case QWebEnginePage::RenderProcessTerminationStatus::KilledTerminationStatus:
        why = tr("le processus de rendu a été arrêté (mémoire insuffisante ?)"); break;
    default:
        why = tr("raison inconnue"); break;
    }
    const int idx = m_tabs->indexOf(view);
    if (idx >= 0) m_tabs->setTabText(idx, tr("Onglet interrompu"));
    if (view == currentView()) m_progress->hide();
    setStatus(tr("Onglet interrompu (%1) — rechargez pour reprendre.").arg(why), 15000);
    showErrorPage(view, tr("Onglet interrompu : %1").arg(why));
}

/* Remplace le contenu d'un onglet par une page d'erreur lisible (le message
   natif de Chromium est en anglais et ne propose rien d'utile). */
void MainWindow::showErrorPage(const QWebEngineView *view, const QString &reason)
{
    if (!view) return;
    auto *v = const_cast<QWebEngineView *>(view);
    const QUrl u = view->url();
    v->setHtml(QStringLiteral(
        "<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"UTF-8\"><style>"
        "body{background:#0F0F17;color:#E8E8F0;font-family:sans-serif;"
        "display:flex;flex-direction:column;align-items:center;justify-content:center;"
        "height:100vh;margin:0;text-align:center}h2{color:#00D4AA}p{color:#8C90AE}"
        "button{background:#0F2A2A;color:#7BFFE4;border:1px solid #0E5F55;"
        "border-radius:9px;padding:9px 18px;font-size:14px;cursor:pointer}"
        "</style></head><body><h2>%1</h2><p>%2</p><p><small>%3</small></p>"
        "<button onclick=\"location.reload()\">%4</button></body></html>")
        .arg(tr("Page non chargée").toHtmlEscaped(),
             reason.toHtmlEscaped(),
             u.toDisplayString(QUrl::RemovePassword).toHtmlEscaped(),
             tr("Réessayer").toHtmlEscaped()),
        u.isEmpty() ? QUrl(QStringLiteral("about:error")) : u);
}

void MainWindow::onLoadProgress(int progress) {
    if (progress >= 0 && progress < 100) {
        m_progress->show();
        m_progress->setValue(progress);
    } else {
        m_progress->hide();
    }
}

// ---------------------------------------------------------------------------
// Repli de moteur de recherche (mur anti-bot)
// ---------------------------------------------------------------------------
void MainWindow::onSerpBlocked(const QString &engineId)
{
    if (!m_search->autoFallback()) return;
    if (m_serpTried.isEmpty() || m_lastQuery.isEmpty()) return;
    const QStringList chain = m_search->fallbackChain(engineId);
    QString next;
    for (const QString &id : chain) {
        if (!m_serpTried.contains(id)) { next = id; break; }
    }
    if (next.isEmpty() || m_serpTried.size() >= 4) {
        m_interceptor->setAllowChallengeImages(true);   // laisse au moins le captcha s'afficher
        setStatus(tr("Tous les moteurs légers sont bloqués par un mur anti-bot. "
                     "Tu peux résoudre la vérification affichée, ou choisir un autre moteur "
                     "dans le panneau Eco (Ctrl+E)."), 10000);
        return;
    }
    const SearchEngine *e = SearchEngineManager::byId(next);
    if (!e) return;
    m_serpTried << next;
    m_serpEngine = next;
    setStatus(tr("Moteur %1 bloqué → essai de %2…").arg(engineId, e->label), 5000);
    if (auto v = currentView()) v->load(m_search->buildUrl(*e, m_lastQuery));
}

/*
 * Un moteur qui fonctionne est adopte définitivement : le badge, la barre
 * d'adresse et les recherches suivantes utilisent alors le moteur reellement
 * utile. Sans cela le badge continuait d'annoncer « DDG » alors que les
 * resultats venaient d'un autre moteur : l'utilisateur ne savait plus ou il etait.
 */
void MainWindow::adoptWorkingEngine(const QString &engineId)
{
    if (engineId.isEmpty() || engineId == m_search->engineId()) return;
    const SearchEngine *e = SearchEngineManager::byId(engineId);
    if (!e) return;
    m_search->setEngineId(engineId);
    m_omni->refreshEngineBadge();
    if (m_ecoPanel) m_ecoPanel->refresh();
    saveSettings();
    setStatus(tr("Moteur actif changé sur %1 (les autres sont bloqués par anti-bot)")
                  .arg(e->label), 6000);
}

// ---------------------------------------------------------------------------
// Eco
// ---------------------------------------------------------------------------
void MainWindow::toggleDataSaver(bool checked) {
    m_dataSaver = checked;
    m_interceptor->setDataSaverEnabled(checked);
    saveSettings();
    applyWebSettings();
    syncEcoWidgets();
    updateStats();
    setStatus(checked ? tr("Économie de données activée (Save-Data: on)")
                     : tr("Économie de données désactivée"), 3000);
}

void MainWindow::toggleImagesOff(bool checked) {
    m_imagesOff = checked;
    m_interceptor->setImagesOff(checked);
    saveSettings();
    applyWebSettings();
    syncEcoWidgets();
    updateStats();
    if (auto v = currentView()) v->reload();
}

void MainWindow::toggleUltraEco(bool checked) {
    m_ultraEco = checked;
    m_interceptor->setUltraEcoEnabled(checked);
    m_favicons = !checked;
    // Le mode Ultra plafonne la qualite effective a 50 sans toucher au
    // reglage de l'utilisateur (avant, « quality=27 » etait reecrit a 50 puis
    // a 65 des la sortie, sans que l'utilisateur l'ait demande).
    applyQuality(m_quality, false);
    applyWebSettings();
    syncEcoWidgets();
    saveSettings();
    updateStats();
    if (auto v = currentView()) v->reload();
    setStatus(checked ? tr("Mode Ultra : streaming, favicons et médias bloqués (qualité 50)")
                     : tr("Mode Ultra désactivé"), 4000);
}

/* Sec-GPC : demande globale de limite de donnees envoyee a chaque site.
   Coût : 1 octet par requête. Effet mesuré sur les ressources statiques : nul
   (voir README) ; l'intérêt est d'exprimer le consentement de l'utilisateur,
   ce que Save-Data ne fait que pour les sites qui l'acceptent. */
void MainWindow::toggleSecGpc(bool checked) {
    m_interceptor->setSecGpcEnabled(checked);
    saveSettings();
    setStatus(checked
        ? tr("Signal vie privée envoyé : Sec-GPC: 1")
        : tr("Signal vie privée désactivé"), 4000);
}

void MainWindow::onQualityChanged(int value) {
    applyQuality(value, true);
}

void MainWindow::applyQuality(int value, bool fromUser)
{
    value = ImageCodec::clampQuality(value);
    if (fromUser) m_quality = value;
    // Le curseur est enfin branché : qualité 0 = pas de compression, sinon les
    // images passent par notre serveur local et reviennent compressées.
    // La borne est celle du codec (source unique), pas un qBound dupliqué.
    if (m_interceptor)
        m_interceptor->setImageCompression(value > 0 && m_imageServer && m_imageServer->isRunning(),
                                          value, m_imageServer ? m_imageServer->base() : QUrl());
    if (m_ecoPanel) m_ecoPanel->refresh();
    if (fromUser) {
        scheduleSaveSettings();   // un cran de curseur = une ecriture groupee
        updateStats();
        setStatus(value == 0 ? tr("Compression des images désactivée")
                             : tr("Qualité des images : %1%").arg(value), 2500);
    }
}

void MainWindow::syncEcoWidgets()
{
    if (m_ecoPanel) {
        m_ecoPanel->setDataSaverChecked(m_dataSaver);
        m_ecoPanel->setImagesChecked(m_imagesOff);
        m_ecoPanel->setUltraChecked(m_ultraEco);
        m_ecoPanel->setFaviconsChecked(m_favicons);
        m_ecoPanel->setSecGpcChecked(m_secGpc);
        m_ecoPanel->setQualityValue(m_quality);
        m_ecoPanel->refresh();
    }
    m_privacyBtn->setChecked(m_dataSaver || m_ultraEco);
    m_privacyBtn->setIcon(m_dataSaver || m_ultraEco ? Icons::shield() : Icons::eyeOff());
    m_ecoBtn->setObjectName(m_dataSaver || m_ultraEco ? QStringLiteral("ecoBtn") : QStringLiteral("ecoBtnOff"));
    m_ecoBtn->style()->unpolish(m_ecoBtn);
    m_ecoBtn->style()->polish(m_ecoBtn);
    QString mode = m_ultraEco ? tr("Ultra") : (m_dataSaver ? tr("Eco") : tr("Eco off"));
    m_ecoBtn->setText(mode);
}

// ---------------------------------------------------------------------------
// Panneau Eco
// ---------------------------------------------------------------------------
void MainWindow::showEcoPanelAt(const QPoint &globalPos)
{
    if (!m_ecoPanel) {
        m_ecoPanel = new EcoPanel(this);
        m_ecoPanel->setWindowFlags(Qt::Popup);
        m_ecoPanel->setWindowTitle(tr("Data saver"));
        m_ecoPanel->setAttribute(Qt::WA_ShowWithoutActivating, false);
        m_ecoPanel->setInterceptor(m_interceptor);
        m_ecoPanel->setImageServer(m_imageServer);
        connect(m_ecoPanel, &EcoPanel::imageStatsResetRequested,
                this, [this]{ if (m_imageServer) m_imageServer->resetStats(); });
        m_ecoPanel->setSearchManager(m_search);
        connect(m_ecoPanel, &EcoPanel::dataSaverToggled, this, &MainWindow::toggleDataSaver);
        connect(m_ecoPanel, &EcoPanel::imagesToggled, this, &MainWindow::toggleImagesOff);
        connect(m_ecoPanel, &EcoPanel::ultraToggled, this, &MainWindow::toggleUltraEco);
        connect(m_ecoPanel, &EcoPanel::faviconsToggled, this, [this](bool on){
            m_favicons = on;
            applyWebSettings();
            syncEcoWidgets();
            saveSettings();
            if (auto v = currentView()) v->reload();
        });
        connect(m_ecoPanel, &EcoPanel::secGpcToggled, this, &MainWindow::toggleSecGpc);
        connect(m_ecoPanel, &EcoPanel::qualityChanged, this, [this](int v){ applyQuality(v, true); });
        connect(m_ecoPanel, &EcoPanel::engineChanged, this, &MainWindow::chooseSearchEngine);
        connect(m_ecoPanel, &EcoPanel::autoFallbackToggled, this, [this](bool on){
            m_search->setAutoFallback(on);
            saveSettings();
            setStatus(on ? tr("Repli automatique du moteur activé") : tr("Repli automatique désactivé"), 3000);
        });
        connect(m_ecoPanel, &EcoPanel::remoteSuggestToggled, this, [this](bool on){
            m_search->setRemoteSuggestions(on);
            saveSettings();
        });
        connect(m_ecoPanel, &EcoPanel::searxInstanceRequested, this, [this]{
            bool ok = false;
            const QString url = QInputDialog::getText(this, tr("Instance SearXNG"),
                tr("Adresse de ton instance SearXNG (auto-hébergée = meilleure précision, 0 suivi) :"),
                QLineEdit::Normal, m_search->searxUrl(), &ok);
            if (ok && !url.trimmed().isEmpty()) {
                m_search->setSearxUrl(url.trimmed());
                saveSettings();
                setStatus(tr("Instance SearXNG : %1").arg(m_search->searxUrl()), 4000);
            }
        });
        connect(m_ecoPanel, &EcoPanel::providerRequested, this, [this]{
            bool ok = false;
            const QString url = QInputDialog::getText(this, tr("Service de suggestions"),
                tr("Endpoint de suggestions (renvoie du JSON type DuckDuckGo \\\"ac\\\") :\n"
                   "Laisser vide pour désactiver. Chaque frappe coûte ~100 octets."),
                QLineEdit::Normal, m_search->providerUrl(), &ok);
            if (!ok) return;
            const QString v = url.trimmed();
            if (v.isEmpty() || v.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0) {
                m_search->setRemoteSuggestions(false);
                setStatus(tr("Suggestions réseau désactivées (100 % local)"), 4000);
            } else {
                m_search->setProviderUrl(v);
                m_search->setRemoteSuggestions(true);
                setStatus(tr("Suggestions : %1").arg(m_search->providerUrl()), 4000);
            }
            saveSettings();
            m_ecoPanel->refresh();
        });
        connect(m_ecoPanel, &EcoPanel::openHistoryRequested, this, &MainWindow::showHistory);
        connect(m_ecoPanel, &EcoPanel::clearCacheRequested, this, &MainWindow::clearCache);
        connect(m_ecoPanel, &EcoPanel::shortcutsRequested, this, &MainWindow::showShortcuts);
        connect(m_ecoPanel, &EcoPanel::aboutRequested, this, &MainWindow::showAbout);
    }
    syncEcoWidgets();
    m_ecoPanel->adjustSize();
    QPoint p = globalPos;
    const QSize s = m_ecoPanel->size();
    const QRect scr = QGuiApplication::primaryScreen()->availableGeometry();
    if (p.y() + s.height() > scr.bottom()) p.setY(std::max(scr.top(), globalPos.y() - s.height() - 46));
    if (p.x() + s.width() > scr.right()) p.setX(scr.right() - s.width() - 8);
    m_ecoPanel->move(p);
    m_ecoPanel->show();
    m_ecoPanel->raise();
}

void MainWindow::chooseSearchEngine(const QString &engineId)
{
    m_search->setEngineId(engineId);
    m_omni->refreshEngineBadge();
    if (m_ecoPanel) m_ecoPanel->refresh();
    saveSettings();
    const SearchEngine *e = SearchEngineManager::byId(engineId);
    if (e) {
        setStatus(tr("Moteur : %1  (%2)").arg(e->label, e->weightNote), 4000);
        updateStats();
    }
}

void MainWindow::showEngineMenuAt(const QPoint &globalPos)
{
    // Meme precautions que le menu principal : le QMenu est detruit avant
    // d'ouvrir le panneau Eco, sinon deux boucles/grabs se cumulent.
    QMenu menu(this);
    menu.setStyleSheet(styleSheet());
    QAction *grp = menu.addAction(tr("Moteur de recherche"));
    grp->setEnabled(false);
    int i = 0;
    for (const SearchEngine &e : SearchEngineManager::registry()) {
        QAction *a = menu.addAction(e.label);
        a->setCheckable(true);
        a->setChecked(e.id == m_search->engineId());
        a->setToolTip(e.weightNote);
        const int id = i++;
        connect(a, &QAction::triggered, this, [this, id]{
            chooseSearchEngine(SearchEngineManager::registry().at(id).id);
        });
    }
    menu.addSeparator();
    QAction *eco = menu.addAction(tr("Réglages Data saver…"));
    eco->setIcon(Icons::leaf());
    connect(eco, &QAction::triggered, this, [this]{
        showEcoPanelAt(m_ecoBtn->mapToGlobal(QPoint(0, m_ecoBtn->height() + 6)));
    });
    menu.exec(globalPos);
}

/* Menu contextuel de la page, en francais et sans les entrees inutiles.
   La cible du clic droit (lien, image) est demandee a la page par un script
   de quelques centaines d'octets, execute uniquement au clic droit ; la
   selection vient de la vue. Le menu est construit ici, seule source des
   actions. */
/* Clic droit : on demande a la page ce qui se trouve SOUS le curseur (lien ou
   image), puis le menu est construit. Un seul aller-retour JS de quelques
   centaines d'octets, execute au clic droit uniquement ; le menu reste donc
   francophone et n'affiche que des actions possibles. Le menu de Qt n'est pas
   utilise : il est en anglais et QWebEngineView ne le remplace pas toujours. */
void MainWindow::showPageContextMenu(QWebEngineView *view, const QPoint &globalPos)
{
    if (!view || !view->page()) return;
    QPointer<QWebEngineView> target(view);
    const QPoint local = view->mapFromGlobal(globalPos);
    const QString js = QStringLiteral(R"JS((function(){
  var e = document.elementFromPoint(%1, %2);
  var out = { link: '', image: '' };
  while (e && e !== document.documentElement && e !== document.body) {
    if (e.tagName === 'A' && e.href) { out.link = e.href; break; }
    if ((e.tagName === 'IMG' || e.tagName === 'VIDEO') && e.src) { out.image = e.src; break; }
    e = e.parentElement;
  }
  return JSON.stringify(out);
})())JS")
        .arg(QString::number(local.x()), QString::number(local.y()));
    view->page()->runJavaScript(js, [this, target, globalPos](const QVariant &v) {
        if (target.isNull()) return;
        buildPageContextMenu(target, globalPos, v.toString());
    });
}

void MainWindow::buildPageContextMenu(QWebEngineView *view, const QPoint &globalPos,
                                      const QString &targetJson)
{
    if (!view || !view->page()) return;
    QMenu menu(this);
    menu.setStyleSheet(styleSheet());

    const QJsonObject target = QJsonDocument::fromJson(targetJson.toUtf8()).object();
    const QUrl linkUrl(target.value(QStringLiteral("link")).toString());
    const QUrl imageUrl(target.value(QStringLiteral("image")).toString());
    const QString selection = view->selectedText().trimmed();

    if (!linkUrl.isEmpty()) {
        if (isWebUrl(linkUrl)) {
            QAction *a = menu.addAction(tr("Ouvrir le lien dans un nouvel onglet"));
            a->setIcon(Icons::plus());
            connect(a, &QAction::triggered, this, [this, linkUrl]{ newTab(linkUrl); });
            a = menu.addAction(tr("Ouvrir le lien dans un onglet de fond"));
            a->setIcon(Icons::plus());
            connect(a, &QAction::triggered, this, [this, linkUrl]{
                createNewTabView()->load(linkUrl);   // sans passer en avant
            });
            a = menu.addAction(tr("Copier l'adresse du lien"));
            a->setIcon(Icons::list());
            connect(a, &QAction::triggered, this, [linkUrl]{
                QApplication::clipboard()->setText(linkUrl.toDisplayString());
            });
        } else {
            // mailto:, tel:, magnet: : le navigateur ne sait pas les ouvrir.
            QAction *a = menu.addAction(
                tr("Ouvrir %1: avec l'application système").arg(linkUrl.scheme()));
            a->setIcon(Icons::external());
            connect(a, &QAction::triggered, this, [this, linkUrl]{
                if (!QDesktopServices::openUrl(linkUrl))
                    setStatus(tr("Aucune application ne peut ouvrir un lien %1").arg(linkUrl.scheme()), 4000);
            });
        }
        menu.addSeparator();
    }

    if (!imageUrl.isEmpty() && isWebUrl(imageUrl)) {
        QAction *a = menu.addAction(tr("Ouvrir l'image dans un nouvel onglet"));
        a->setIcon(Icons::external());
        connect(a, &QAction::triggered, this, [this, imageUrl]{ newTab(imageUrl); });
        a = menu.addAction(tr("Copier l'adresse de l'image"));
        connect(a, &QAction::triggered, this, [imageUrl]{
            QApplication::clipboard()->setText(imageUrl.toDisplayString());
        });
        menu.addSeparator();
    }

    if (!selection.isEmpty()) {
        QAction *a = menu.addAction(tr("Copier la sélection"));
        a->setIcon(Icons::list());
        const QString sel = selection;
        connect(a, &QAction::triggered, this, [sel]{
            QApplication::clipboard()->setText(sel);
        });
        a = menu.addAction(tr("Rechercher « %1 »").arg(sel.left(40) + (sel.size() > 40 ? QStringLiteral("…") : QString())));
        a->setIcon(Icons::search());
        connect(a, &QAction::triggered, this, [this, sel]{
            onUrlEntered(m_search->buildUrl(*m_search->current(), sel));
        });
        menu.addSeparator();
    }

    auto *page = view->page();

    // Exception d'images du site visite : « Images OFF » est le reglage le
    // plus rentable (-90 % de moyenne mesuree) mais rend les sites photos
    // illisibles. Le clic droit autorise CE site, une fois pour toutes.
    // La decision porte sur le site visite, pas sur l'hote de l'image : les
    // images viennent presque toujours d'un autre domaine (CDN).
    const QString pageHost = page->url().host();
    if (isWebUrl(page->url()) && !pageHost.isEmpty()) {
        const bool allowed = m_interceptor->isImageHostAllowed(pageHost);
        QAction *img = menu.addAction(tr("Images de ce site"));
        img->setCheckable(true);
        img->setChecked(allowed);
        img->setIcon(Icons::eyeOff());
        connect(img, &QAction::triggered, this, [this, pageHost, allowed]{
            m_interceptor->setImageHostAllowed(pageHost, !allowed);
            saveSettings();
            syncEcoWidgets();
            updateStats();
            setStatus(allowed
                          ? tr("Images de nouveau bloquées sur %1").arg(pageHost)
                          : tr("Images autorisées sur %1 — page rechargée").arg(pageHost), 5000);
            // Rechargement necessaire : sans lui, les images resteraient
            // absentes de la page deja affichee et le reglage semblerait inopérant.
            if (auto v = currentView()) v->reload();
        });
        menu.addSeparator();
    }

    if (page->isLoading()) {
        QAction *a = menu.addAction(tr("Arrêter le chargement"));
        a->setIcon(Icons::stop());
        connect(a, &QAction::triggered, this, [this]{ if (auto v = currentView()) v->stop(); });
    } else {
        QAction *a = menu.addAction(tr("Recharger"));
        a->setIcon(Icons::reload());
        connect(a, &QAction::triggered, this, [this]{ if (auto v = currentView()) v->reload(); });
    }
    if (isWebUrl(page->url())) {
        QAction *a = menu.addAction(tr("Copier l'adresse de la page"));
        connect(a, &QAction::triggered, this, [page]{
            QApplication::clipboard()->setText(page->url().toDisplayString());
        });
    }
    menu.addSeparator();

    QAction *a = menu.addAction(tr("Zoom arrière"));
    connect(a, &QAction::triggered, this, [this]{ setZoom(m_zoom - 0.1); });
    a = menu.addAction(tr("Zoom 100 %"));
    connect(a, &QAction::triggered, this, [this]{ setZoom(1.0); });
    a = menu.addAction(tr("Zoom avant"));
    connect(a, &QAction::triggered, this, [this]{ setZoom(m_zoom + 0.1); });
    menu.addSeparator();

    a = menu.addAction(tr("Afficher le code source"));
    a->setIcon(Icons::list());
    connect(a, &QAction::triggered, this, [this]{
        if (auto v = currentView()) newTab(QUrl(QStringLiteral("view-source:") + v->url().toString()));
    });
    a = menu.addAction(tr("Inspecter la page"));
    a->setIcon(Icons::gear());
    connect(a, &QAction::triggered, this, [this]{
        if (auto v = currentView())
            v->page()->triggerAction(QWebEnginePage::WebAction::InspectElement);
    });
    a = menu.addAction(tr("Réglages Data saver…"));
    a->setIcon(Icons::leaf());
    connect(a, &QAction::triggered, this, [this]{
        showEcoPanelAt(m_ecoBtn->mapToGlobal(QPoint(0, m_ecoBtn->height() + 6)));
    });

    // Les actions sont connectees a la construction : QMenu::exec() rend la main
    // apres le declic, donc aucune boite modale n'est ouverte ici.
    menu.exec(globalPos);
}

// ---------------------------------------------------------------------------
// Menu principal
// ---------------------------------------------------------------------------
void MainWindow::showMenuAt(const QPoint &globalPos)
{
    // Le QMenu vit dans un bloc dedie : QMenu::exec() tient un grab
    // souris/clavier pendant toute sa duree de vie. Ouvrir ensuite une boite
    // modale (Telechargements, Historique...) empile deux boucles d'evenements
    // imbriquees tant que ce menu existe -> l'interface ne repond plus.
    // On ressort donc du bloc AVANT d'appeler l'action.
    int choisi = 0;
    {
        QMenu menu(this);
        menu.setStyleSheet(styleSheet());
        QHash<int, QAction *> acts;

        // Pas de setShortcut() ici : la fenetre enregistre deja ces raccourcis
        // via QShortcut. Les dupliquer faisait capturer la meme touche deux fois.
        auto add = [&](int id, const QString &text, const QIcon &ic) {
            QAction *a = menu.addAction(ic, text);
            acts.insert(id, a);
            return a;
        };

        add(1, tr("Nouvel onglet"), Icons::plus());
        add(2, tr("Historique"), Icons::history());
        add(3, tr("Téléchargements"), Icons::download());
        add(4, tr("Recharger"), Icons::reload());
        add(5, tr("Recharger sans cache"), Icons::reload());
        menu.addSeparator();
        add(7, tr("Rechercher dans la page"), Icons::search());
        add(8, tr("Ajouter / voir les favoris"), Icons::star());
        add(9, tr("Page d'accueil"), Icons::home());
        menu.addSeparator();
        add(10, tr("Zoom avant"), Icons::plus());
        add(11, tr("Zoom arrière"), Icons::search());
        add(12, tr("Zoom 100 %"), Icons::globe());
        add(13, tr("Plein écran"), Icons::external());
        menu.addSeparator();
        add(14, tr("Vider le cache"), Icons::trash());
        add(15, tr("Réinitialiser les statistiques"), Icons::trash());
        add(16, tr("Raccourcis clavier"), Icons::list());
        // Reinitialisation des exceptions d'images : visible seulement s'il y
        // en a au moins une, sinon l'entree serait toujours grise.
        if (m_interceptor->imageAllowedCount() > 0) {
            add(18, tr("Images autorisées : tout réinitialiser"), Icons::trash());
        }
        add(17, tr("À propos de %1").arg(QLatin1String(kAppName)), Icons::shield());
        if (m_isPrivate) {
            menu.addSeparator();
            QAction *p = menu.addAction(tr("Mode privé : actif (rien n'est enregistré)"));
            p->setEnabled(false);
        }

        if (QAction *action = menu.exec(globalPos))
            choisi = acts.key(action);
    }   // <- le QMenu est detruit ici, grab compris
    if (choisi == 0) return;
    applySessionAction(choisi);
}

void MainWindow::applySessionAction(int actionId)
{
    auto v = currentView();
    switch (actionId) {
    case 1: newTab(); break;
    case 2: showHistory(); break;
    case 3: showDownloads(); break;
    case 4: if (v) v->reload(); break;
    case 5: m_profile->clearHttpCache(); if (v) v->reload(); break;
    case 7: showFindBar(); break;
    case 8: setStatus(tr("Marque-pages : désactivé dans cette version (pas de DB)"), 4000); break;
    case 9: if (v) v->setHtml(homePageHtml(), QUrl("about:home")); break;
    case 10: setZoom(m_zoom + 0.1); break;
    case 11: setZoom(m_zoom - 0.1); break;
    case 12: setZoom(1.0); break;
    case 13: if (isFullScreen()) showNormal(); else showFullScreen(); break;
    case 14: clearCache(); break;
    case 15: m_interceptor->resetStats(); updateStats(); setStatus(tr("Statistiques remises à zéro"), 2500); break;
    case 16: showShortcuts(); break;
    case 17: showAbout(); break;
    case 18:
        m_interceptor->clearImageAllowedHosts();
        saveSettings();
        syncEcoWidgets();
        updateStats();
        setStatus(tr("Exceptions d'images réinitialisées"), 4000);
        if (auto v = currentView()) v->reload();
        break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Dialogues
// ---------------------------------------------------------------------------
void MainWindow::showHistory() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Historique — %1").arg(QLatin1String(kAppName)));
    dlg.resize(760, 520);
    dlg.setAttribute(Qt::WA_StyledBackground, true);
    dlg.setStyleSheet(QStringLiteral(
        "QDialog{background:#0F0F17;color:#E8E8F0;}"
        "QLabel{color:#8C90AE;}"));
    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    lay->setSpacing(8);

    QLineEdit *filter = new QLineEdit(&dlg);
    filter->setPlaceholderText(tr("Filtrer l'historique…"));
    filter->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#1A1A2E;border:1px solid #2A2A45;border-radius:7px;padding:7px 10px;color:#E8E8F0;}"
        "QLineEdit:focus{border-color:#00D4AA;}"));
    lay->addWidget(filter);

    QListWidget *list = new QListWidget(&dlg);
    list->setStyleSheet(QStringLiteral(
        "QListWidget{background:#151525;border:1px solid #2A2A45;border-radius:8px;outline:none;}"
        "QListWidget::item{padding:6px;}QListWidget::item:selected{background:#243046;border-radius:6px;}"));
    lay->addWidget(list, 1);

    const int roleUrl = Qt::UserRole + 1;
    std::function<void(const QString &)> fill = [&](const QString &needle) {
        list->clear();
        const auto entries = needle.isEmpty()
            ? m_history->recent(300)
            : m_history->search(needle, 300);
        for (const auto &e : entries) {
            if (e.url.startsWith(QLatin1String("about:"))) continue;
            const QString dt = QDateTime::fromSecsSinceEpoch(e.visitedAt).toString("dd/MM/yyyy hh:mm");
            auto *it = new QListWidgetItem(QStringLiteral("%1\n%2   ·   %3 visite(s)   ·   %4")
                                              .arg(e.title.isEmpty() ? e.url : e.title, e.url)
                                              .arg(e.visitCount).arg(dt));
            it->setData(roleUrl, e.url);
            it->setToolTip(e.url);
            list->addItem(it);
        }
        if (list->count() == 0) {
            auto *it = new QListWidgetItem(needle.isEmpty() ? tr("Aucun historique") : tr("Aucun résultat"));
            it->setFlags(Qt::NoItemFlags);
            list->addItem(it);
        }
    };
    fill(QString());
    // Un SELECT LIKE par frappe : le filtrage est regroupe (250 ms) au lieu de
    // relancer une requete complete a chaque caractere tape.
    auto *debounce = new QTimer(&dlg);
    debounce->setSingleShot(true);
    debounce->setInterval(250);
    connect(filter, &QLineEdit::textChanged, debounce, [debounce, &fill](const QString &t){
        Q_UNUSED(t);
        debounce->start();
    });
    connect(debounce, &QTimer::timeout, &dlg, [debounce, filter, &fill]{
        fill(filter->text());
    });

    QHBoxLayout *btns = new QHBoxLayout;
    auto *openBtn = new QPushButton(tr("Ouvrir dans un nouvel onglet"), &dlg);
    auto *delBtn = new QPushButton(tr("Supprimer la sélection"), &dlg);
    delBtn->setProperty("destructive", true);
    auto *clearBtn = new QPushButton(tr("Tout effacer"), &dlg);
    clearBtn->setProperty("destructive", true);
    btns->addWidget(openBtn);
    btns->addWidget(delBtn);
    btns->addStretch();
    btns->addWidget(clearBtn);
    auto *closeBtn = new QPushButton(tr("Fermer"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btns->addWidget(closeBtn);
    lay->addLayout(btns);

    connect(list, &QListWidget::itemDoubleClicked, [this, &dlg, list, roleUrl]{
        QListWidgetItem *it = list->currentItem();
        if (!it) return;
        const QString url = it->data(roleUrl).toString();
        if (!url.isEmpty()) { newTab(QUrl(url)); dlg.accept(); }
    });
    connect(openBtn, &QPushButton::clicked, [this, &dlg, list, roleUrl]{
        QListWidgetItem *it = list->currentItem();
        if (!it) return;
        const QString url = it->data(roleUrl).toString();
        if (!url.isEmpty()) { newTab(QUrl(url)); dlg.accept(); }
    });
    connect(delBtn, &QPushButton::clicked, [this, list, roleUrl]{
        for (QListWidgetItem *it : list->selectedItems()) {
            const QString url = it->data(roleUrl).toString();
            if (!url.isEmpty()) { m_history->remove(url); delete list->takeItem(list->row(it)); }
        }
    });
    connect(clearBtn, &QPushButton::clicked, [this, filter, &fill]{
        if (QMessageBox::question(this, tr("Effacer l'historique"),
                tr("Effacer tout l'historique de navigation ?")) == QMessageBox::Yes) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            m_history->clear();          // VACUUM : plusieurs secondes si gros historique
            QApplication::restoreOverrideCursor();
            fill(filter->text());
        }
    });
    dlg.exec();
}

void MainWindow::showDownloads() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Téléchargements"));
    dlg.resize(640, 420);
    dlg.setAttribute(Qt::WA_StyledBackground, true);
    dlg.setStyleSheet(QStringLiteral("QDialog{background:#0F0F17;color:#E8E8F0;}"));
    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    QListWidget *list = new QListWidget(&dlg);
    list->setStyleSheet(QStringLiteral(
        "QListWidget{background:#151525;border:1px solid #2A2A45;border-radius:8px;outline:none;}"
        "QListWidget::item{padding:6px;}"));
    auto refresh = [this, list]{
        list->clear();
        const auto dl = m_downloads;
        if (dl.isEmpty()) {
            auto *it = new QListWidgetItem(tr("Aucun téléchargement"));
            it->setFlags(Qt::NoItemFlags);
            list->addItem(it);
            return;
        }
        for (const auto &d : dl) {
            QString st = d.state == 1 ? tr("terminé") : d.state == 2 ? tr("annulé")
                        : d.state == 3 ? tr("erreur") : tr("en cours");
            if (d.state == 0 && d.total > 0)
                st = tr("en cours (%1 %)").arg(d.received * 100 / d.total);
            list->addItem(QStringLiteral("%1   ·   %2   ·   %3\n%4").arg(d.fileName, st, humanBytes(d.received), d.path));
        }
    };
    refresh();
    lay->addWidget(list, 1);
    QHBoxLayout *btns = new QHBoxLayout;
    auto *refreshBtn = new QPushButton(tr("Actualiser"), &dlg);
    auto *clearBtn = new QPushButton(tr("Vider la liste"), &dlg);
    auto *closeBtn = new QPushButton(tr("Fermer"), &dlg);
    closeBtn->setDefault(true);
    btns->addWidget(refreshBtn);
    btns->addWidget(clearBtn);
    btns->addStretch();
    btns->addWidget(closeBtn);
    lay->addLayout(btns);
    connect(refreshBtn, &QPushButton::clicked, this, refresh);
    // La liste etait figee : un telechargement lance apres l'ouverture
    // n'apparait qu'apres « Actualiser ». Rafraichissement periodique le temps
    // que le dialogue soit visible.
    auto *poll = new QTimer(&dlg);
    poll->setInterval(1000);
    connect(poll, &QTimer::timeout, &dlg, [poll, &dlg, refresh]{
        if (!dlg.isVisible()) { poll->stop(); return; }
        refresh();
    });
    poll->start();
    connect(clearBtn, &QPushButton::clicked, this, [this, refresh]{ m_downloads.clear(); refresh(); });
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

void MainWindow::showShortcuts() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Raccourcis clavier"));
    dlg.resize(560, 640);
    dlg.setAttribute(Qt::WA_StyledBackground, true);
    QFont uiFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    uiFont.setFamilies({QStringLiteral("DejaVu Sans"), QStringLiteral("Noto Sans"),
                        QStringLiteral("Liberation Sans"), QStringLiteral("sans-serif")});
    dlg.setFont(uiFont);
    dlg.setStyleSheet(QStringLiteral("QDialog{background:#0F0F17;color:#E8E8F0;}"));
    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    QLabel *title = new QLabel(tr("Navigation"), &dlg);
    title->setStyleSheet(QStringLiteral("color:#00D4AA;font-weight:600;"));
    lay->addWidget(title);
    auto grid = [this, &dlg, &lay]{
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(18);
        int r = 0;
        for (const ShortcutRow &row : m_navShortcuts) {
            auto *k = new QLabel(row.keys, &dlg);
            k->setObjectName(QStringLiteral("keyHint"));
            k->setStyleSheet(QStringLiteral("color:#00D4AA;font-family:monospace;"));
            auto *d = new QLabel(row.label, &dlg);
            d->setStyleSheet(QStringLiteral("color:#8C90AE;"));
            g->addWidget(k, r, 0, Qt::AlignLeft);
            g->addWidget(d, r, 1);
            ++r;
        }
        lay->addLayout(g);
    };
    grid();
    QLabel *t2 = new QLabel(tr("Économie de données"), &dlg);
    t2->setStyleSheet(QStringLiteral("color:#00D4AA;font-weight:600;margin-top:10px;"));
    lay->addWidget(t2);
    {
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(18);
        int r = 0;
        for (const ShortcutRow &row : m_ecoShortcuts) {
            auto *k = new QLabel(row.keys, &dlg);
            k->setObjectName(QStringLiteral("keyHint"));
            k->setStyleSheet(QStringLiteral("color:#00D4AA;font-family:monospace;"));
            auto *d = new QLabel(row.label, &dlg);
            d->setStyleSheet(QStringLiteral("color:#8C90AE;"));
            g->addWidget(k, r, 0, Qt::AlignLeft);
            g->addWidget(d, r, 1);
            ++r;
        }
        lay->addLayout(g);
    }
    QLabel *t3 = new QLabel(tr("Astuces de recherche"), &dlg);
    t3->setStyleSheet(QStringLiteral("color:#00D4AA;font-weight:600;margin-top:10px;"));
    lay->addWidget(t3);
    QLabel *tips = new QLabel(
        tr("• Tapez l'URL directement : exemple.fr, 192.168.1.1, localhost:8080\n"
           "• Bangs : !w linux, !so lambda, !gh qt6, !mdn array, !yt concert\n"
           "• Préfixes courts : so lambda, gh qt6, w linux\n"
           "• Clic droit sur un lien = ouvrir dans un nouvel onglet, copier l'adresse"), &dlg);
    tips->setStyleSheet(QStringLiteral("color:#8C90AE;"));
    tips->setWordWrap(true);
    lay->addWidget(tips);
    lay->addStretch();
    auto *ok = new QPushButton(tr("Fermer"), &dlg);
    lay->addWidget(ok);
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

void MainWindow::showAbout() {
    const int rules = m_adblocker.ruleCount();
    const SearchEngine *e = m_search ? m_search->current() : nullptr;
    // Jetons {nom} plutot que %1/%2 : l'ordre des .arg() devient illisible
    // et une simple insertion durait tout decaler silencieusement.
    QString txt = tr("<h3>{app} 1.1</h3>"
        "<p>Navigateur portable Qt6 orienté <b>économie de données</b> et vie privée.</p>"
        "<p><b>Moteur de recherche :</b> {engine} ({weight})<br>"
        "<b>Filtres anti-pubs :</b> {rules} règles<br>"
        "<b>Requêtes bloquées :</b> {blocked}<br>"
        "<b>Économie estimée :</b> {saved}<br>"
        "<b>Cache :</b> {cache} entrées<br>"
        "<b>Historique :</b> {hist} entrées<br>"
        "<b>Mode :</b> {mode}</p>"
        "<p style='color:#888'>Suggestions distantes : ~100 octets par frappe.<br>"
        "Aucun telemetrage, aucun envoi de requêtes à un serveur de recherche interne.</p>");
    txt.replace("{app}", QLatin1String(kAppName));
    txt.replace("{engine}", e ? e->label : QStringLiteral("—"));
    txt.replace("{weight}", e ? e->weightNote : QString());
    txt.replace("{rules}", QString::number(rules));
    txt.replace("{blocked}", QString::number(m_interceptor->blockedCount()));
    txt.replace("{saved}", humanBytes(m_interceptor->estimatedSavedBytes()));
    txt.replace("{cache}", QString::number(m_cache->count()));
    txt.replace("{hist}", QString::number(m_history->count()));
    txt.replace("{mode}", m_isPrivate ? tr("privé (mémoire seule)") : tr("normal (profil persistant)"));
    QMessageBox::about(this, tr("À propos de %1").arg(QLatin1String(kAppName)), txt);
}

void MainWindow::clearCache() {
    m_cache->clear();
    m_profile->clearHttpCache();
    QMessageBox::information(this, tr("Cache"), tr("Cache effacé."));
    updateStats();
}

// ---------------------------------------------------------------------------
// Recherche dans la page
// ---------------------------------------------------------------------------
void MainWindow::showFindBar() {
    m_findBar->show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void MainWindow::closeFindBar() {
    m_findBar->hide();
    m_findInfo->clear();          // « 3 / 7 » ne vautait pas pour la page suivante
    // Nettoyage du DOM inutile si aucune recherche n'a ete lancee.
    if (m_findEdit->text().isEmpty()) {
        if (auto v = currentView()) v->setFocus();
        return;
    }
    if (auto v = currentView()) {
        v->page()->runJavaScript(QStringLiteral(
            "document.querySelectorAll('mark[data-eco]').forEach(function(m){"
            "var p=m.parentNode;while(m.firstChild)p.removeChild(m.firstChild);"
            "p.replaceChild(document.createTextNode(m.textContent),m);p.normalize();});"));
    }
    if (auto v = currentView()) v->setFocus();
}

void MainWindow::findInPage(bool backwards) {
    auto v = currentView();
    if (!v || m_findEdit->text().isEmpty()) return;
    QString term = m_findEdit->text();
    term.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    term.replace(QLatin1Char('\''), QLatin1String("\\'"));
    const QString js = QStringLiteral(R"JS((function(){
  var q = '{term}'; var back = {back};
  var marks = document.querySelectorAll('mark[data-eco]');
  for (var i = 0; i < marks.length; ++i) {
    var m = marks[i], p = m.parentNode;
    while (m.firstChild) p.removeChild(m.firstChild);
    p.replaceChild(document.createTextNode(m.textContent), m);
    p.normalize();
  }
  var st = window.__ecoFind;
  if (!st || st.term !== q || st.len !== document.body.innerHTML.length) {
    st = { term: q, len: document.body.innerHTML.length, hits: [], idx: 0 };
    var w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, null), n, list = [];
    var low = q.toLowerCase();
    while ((n = w.nextNode())) {
      if (n.nodeValue && n.parentNode && n.parentNode.nodeName !== 'SCRIPT'
          && n.nodeValue.toLowerCase().indexOf(low) >= 0) list.push(n);
    }
    st.hits = list;
    window.__ecoFind = st;
  }
  if (!st.hits.length) return { n: 0, i: 0 };
  st.idx = (st.idx + (back ? -1 : 1) + st.hits.length) % st.hits.length;
  var cur = st.hits[st.idx];
  if (!cur.parentNode) return { n: 0, i: 0 };
  var range = document.createRange();
  range.selectNodeContents(cur);
  var mark = document.createElement('mark');
  mark.setAttribute('data-eco', '1');
  mark.style.background = '#FFD54A';
  mark.style.color = '#101010';
  try { range.surroundContents(mark); } catch (e) {}
  mark.scrollIntoView({ block: 'center' });
  return { n: st.hits.length, i: st.idx + 1 };
})())JS")
        .replace(QStringLiteral("{term}"), term)
        .replace(QStringLiteral("{back}"), backwards ? QStringLiteral("true") : QStringLiteral("false"));
    v->page()->runJavaScript(js, [this](const QVariant &val){
        const QVariantMap m = val.toMap();
        const int n = m.value(QStringLiteral("n")).toInt();
        const int i = m.value(QStringLiteral("i")).toInt();
        m_findInfo->setText(n > 0 ? tr("%1 / %2").arg(i).arg(n) : tr("aucun résultat"));
    });
}

// ---------------------------------------------------------------------------
// Zoom
// ---------------------------------------------------------------------------
void MainWindow::setZoom(double factor) {
    m_zoom = qBound(0.25, factor, 3.0);
    for (EcoTab *info : m_tabInfos)
        if (info->view) info->view->setZoomFactor(m_zoom);
    updateZoomLabel();
    // Un cran de molette = une ecriture de fichier : on regroupe.
    scheduleSaveSettings();
}

void MainWindow::scheduleSaveSettings() {
    if (!m_saveTimer || m_shuttingDown) return;
    m_saveTimer->start();
}

void MainWindow::updateZoomLabel() {
    m_zoomLabel->setText(tr("Zoom %1 %").arg(qRound(m_zoom * 100)));
}

// ---------------------------------------------------------------------------
// Web settings
// ---------------------------------------------------------------------------
void MainWindow::applyWebSettings() {
    auto *s = m_profile->settings();
    s->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    s->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);
    s->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
    s->setAttribute(QWebEngineSettings::AutoLoadImages, !m_imagesOff);
    s->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    s->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, false);
    s->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    s->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
    s->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);
    s->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, true);
    s->setAttribute(QWebEngineSettings::AutoLoadIconsForPage, m_favicons && !m_ultraEco);
    s->setAttribute(QWebEngineSettings::PdfViewerEnabled, false);
    s->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);
    m_profile->setHttpUserAgent(QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                                               "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36 DataSaverBrowser/1.1"));
}

// ---------------------------------------------------------------------------
// Statistiques
// ---------------------------------------------------------------------------
/* Les compteurs ne sont recalcules que sur evenement (navigation, blocage,
   ouverture/fermeture d'onglet). Les interroger toutes les 350 ms faisait
   tourner deux SELECT COUNT(*) sur le thread GUI : c'est ce qui gonflait le
   fichier history.db-wal a 4 Mo pour 300 lignes. */
void MainWindow::updateStats() {
    if (!m_statusLabel) return;
    const qint64 blocked = m_interceptor ? m_interceptor->blockedCount() : 0;
    if (m_statusUntil > QDateTime::currentMSecsSinceEpoch())
        return;   // un message temporaire (erreur, telechargement) garde la main
    refreshStatusLine();
    QString eco = m_ultraEco ? tr("ULTRA") : (m_dataSaver ? tr("ON") : tr("OFF"));
    if (m_imagesOff) {
        const int n = m_interceptor->imageAllowedCount();
        eco += n > 0 ? QStringLiteral(" +NoImg/%1").arg(n) : QStringLiteral(" +NoImg");
    }
    m_statsLabel->setText(tr("Eco %1 · %2 bloqués · ≈%3")
                              .arg(eco).arg(blocked).arg(humanBytes(m_interceptor ? m_interceptor->estimatedSavedBytes() : 0)));
    if (m_ecoPanel && m_ecoPanel->isVisible()) m_ecoPanel->refresh();
    updateNavigationActions();
}

void MainWindow::refreshStatusLine() {
    m_cachedRules = m_adblocker.ruleCount();
    m_cachedCacheCount = m_cache ? m_cache->count() : 0;
    m_cachedHistoryCount = m_history ? m_history->count() : 0;
    const QString mode = m_isPrivate ? tr("Privé ") : QString();
    m_statusLabel->setText(QStringLiteral("%1Filtres : %2 · Cache : %3 · Historique : %4")
                              .arg(mode).arg(m_cachedRules)
                              .arg(m_cachedCacheCount).arg(m_cachedHistoryCount));
}

void MainWindow::onBlocked(qint64 count) {
    Q_UNUSED(count)
    // Rafraichissement groupe (evite un repaint par requete bloquee).
    if (m_statsTimer && !m_statsTimer->isActive()) m_statsTimer->start();
}

/* Le timer de statistiques ne tourne que s'il y a quelque chose a rafraichir :
   en combien il est a l'arret au lieu de sonder deux bases toutes les 200 ms. */
void MainWindow::pollStats()
{
    const bool dirty = m_interceptor && m_interceptor->takeStatsDirty();
    updateStats();
    // Single-shot : on ne reprogramme que s'il reste des statistiques a
    // traiter. Sans requete bloquee, le timer s'arrete de lui-meme.
    if (dirty && m_statsTimer && !m_shuttingDown) m_statsTimer->start();
}

void MainWindow::setStatus(const QString &text, int timeoutMs) {
    m_statusLabel->setText(text);
    // Tant que le message est valide, updateStats() n'ecrase rien : c'etait la
    // cause des erreurs de chargement invisibles (effacees en 350 ms).
    m_statusUntil = QDateTime::currentMSecsSinceEpoch() + qMax(0, timeoutMs);
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this]{
            if (m_statusUntil > QDateTime::currentMSecsSinceEpoch()) return;
            refreshStatusLine();
        });
    }
}

// ---------------------------------------------------------------------------
// Page d'accueil
// ---------------------------------------------------------------------------
QString MainWindow::homePageHtml() const {
    QString nowTime = QTime::currentTime().toString("HH:mm");
    QString nowDate = QLocale(QLocale::French).toString(QDate::currentDate(), "dddd dd MMMM yyyy");
    const SearchEngine *e = m_search ? m_search->current() : nullptr;
    const QString engineLabel = e ? e->label : QStringLiteral("le Web");
    const QString engineShort = e ? e->shortLabel : QStringLiteral("WEB");
    const QString ecoMode = m_ultraEco ? QStringLiteral("ULTRA")
                            : (m_dataSaver ? QStringLiteral("ECO ON") : QStringLiteral("ECO OFF"));
    // Raccourcis "site:" resolus en local = 0 requete de moteur
    const char *shortcutJs =
        "(function(){"
        " var q=this.value.trim(); if(!q) return;"
        " var m=q.match(/^(\\S+)\\s+(.*)$/);"
        " var map={so:'https://stackoverflow.com/search?q=',gh:'https://github.com/search?q=',"
        "w:'https://fr.wikipedia.org/w/index.php?search=',wiki:'https://fr.wikipedia.org/w/index.php?search=',"
        "mdn:'https://developer.mozilla.org/fr/search?q=',npm:'https://www.npmjs.com/search?q=',"
        "yt:'https://m.youtube.com/results?search_query='};"
        " if(m && map[m[1].toLowerCase()]){ location.href=map[m[1].toLowerCase()]+encodeURIComponent(m[2]); return; }"
        " if(q.indexOf('://')>=0 || /^[a-z0-9\\-]+(\\.[a-z0-9\\-]+)+(\\/\\S*)?$/i.test(q)){ location.href=q; return; }"
        " location.href='browseeco://search?q='+encodeURIComponent(q);"
        "}).call(this)";
    // Template avec jetons {..} (pas d'arg() : evite tout conflit avec les % du CSS)
    QString html = QStringLiteral(
        "<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{background:radial-gradient(1200px 600px at 50% -10%,#1a1a35 0%,#0B0B14 60%);color:#E8E8F0;"
        "font-family:'Segoe UI',system-ui,-apple-system,sans-serif;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:7vh 20px 24px}"
        ".logo{font-size:30px;font-weight:700;letter-spacing:-.5px}.logo span{color:#00D4AA}"
        ".mode{font-size:11px;color:#00D4AA;background:#0E2A2A;border:1px solid #0E5F55;border-radius:20px;padding:3px 12px;margin-top:10px}"
        ".time{font-size:64px;font-weight:200;color:#8C90AE;margin-top:26px;letter-spacing:-2px}"
        ".date{font-size:14px;color:#5A5F80;margin-top:2px;margin-bottom:34px}"
        ".search{width:620px;max-width:94vw;position:relative}"
        ".search input{width:100%;padding:15px 22px 15px 48px;background:#151525;border:1px solid #2A2A45;border-radius:14px;"
        "color:#E8E8F0;font-size:15px;outline:none;box-shadow:0 8px 30px rgba(0,0,0,.35)}"
        ".search input:focus{border-color:#00D4AA;box-shadow:0 8px 30px rgba(0,212,170,.12)}"
        ".search .ico{position:absolute;left:18px;top:16px;color:#5A5F80;font-size:15px}"
        ".hint{font-size:11px;color:#3F4360;margin-top:10px;text-align:center}"
        ".links{display:flex;gap:14px;margin-top:38px;flex-wrap:wrap;justify-content:center;max-width:780px}"
        ".link{display:flex;flex-direction:column;align-items:center;gap:7px;text-decoration:none;color:#7A7F9E;font-size:11px;width:92px}"
        ".link:hover{color:#00D4AA}"
        ".link .icon{width:46px;height:46px;background:#151525;border:1px solid #2A2A45;border-radius:13px;"
        "display:flex;align-items:center;justify-content:center;font-size:19px}"
        ".link:hover .icon{border-color:#00D4AA;background:#14142A}"
        ".stats{margin-top:34px;font-size:11px;color:#3F4360;text-align:center;line-height:1.7}"
        ".stats b{color:#00A88A;font-weight:600}"
        "</style></head><body>"
        "<div class=\"logo\">Data<span>Saver</span> Browser</div>"
        "<div class=\"mode\">{mode} · {engineShort}</div>"
        "<div class=\"time\">{time}</div>"
        "<div class=\"date\">{date}</div>"
        "<div class=\"search\"><span class=\"ico\">&#x1F50D;</span>"
        "<input type=\"text\" id=\"q\" placeholder=\"Rechercher sur {engineLabel}…\" autocomplete=\"off\" autofocus></div>"
        "<div class=\"hint\">Astuce : <b>so</b> question · <b>gh</b> dépôt · <b>w</b> Wikipédia · <b>mdn</b> doc — ou colle directement une URL</div>"
        "<div class=\"links\">"
        "<a class=\"link\" href=\"https://fr.wikipedia.org\"><div class=\"icon\">&#x1F4D6;</div>Wikipédia</a>"
        "<a class=\"link\" href=\"https://github.com\"><div class=\"icon\">&#x2328;</div>GitHub</a>"
        "<a class=\"link\" href=\"https://stackoverflow.com\"><div class=\"icon\">&#x1F9E0;</div>Stack Overflow</a>"
        "<a class=\"link\" href=\"https://developer.mozilla.org\"><div class=\"icon\">&#x1F4D8;</div>MDN</a>"
        "<a class=\"link\" href=\"https://www.openstreetmap.org\"><div class=\"icon\">&#x1F5FA;</div>OSM Maps</a>"
        "<a class=\"link\" href=\"https://fr.wikipedia.org/wiki/Sp%C3%A9cialit%C3%A9:Derniers_articles\"><div class=\"icon\">&#x1F4F0;</div>Actualités</a>"
        "<a class=\"link\" href=\"https://m.youtube.com\"><div class=\"icon\">&#x1F3AC;</div>YouTube</a>"
        "<a class=\"link\" href=\"https://wiby.me/\"><div class=\"icon\">&#x1F4BE;</div>Wiby</a>"
        "</div>"
        "<div class=\"stats\">{rules} règles anti-pubs &amp; trackers actives<br>"
        "Aucune requête envoyée à un serveur de recherche interne · suggestions réseau ≈ 100 octets<br>"
        "<b>Économie estimée</b> : {saved} · <b>requêtes bloquées</b> : {blocked}</div>"
        "<script>"
        "var i=document.getElementById('q');i.focus();"
        "i.addEventListener('keydown',function(e){if(e.key==='Enter'){ {shortcut} }});"
        "</script>"
        "</body></html>");
    html.replace(QStringLiteral("{time}"), nowTime);
    html.replace(QStringLiteral("{date}"), nowDate);
    html.replace(QStringLiteral("{mode}"), ecoMode);
    html.replace(QStringLiteral("{engineShort}"), engineShort);
    html.replace(QStringLiteral("{engineLabel}"), engineLabel);
    html.replace(QStringLiteral("{rules}"), QString::number(m_adblocker.ruleCount()));
    html.replace(QStringLiteral("{saved}"), humanBytes(m_interceptor ? m_interceptor->estimatedSavedBytes() : 0));
    html.replace(QStringLiteral("{blocked}"), QString::number(m_interceptor ? m_interceptor->blockedCount() : 0));
    html.replace(QStringLiteral("{shortcut}"), QString::fromLatin1(shortcutJs));
    return html;
}

// ---------------------------------------------------------------------------
// Réglages
// ---------------------------------------------------------------------------
void MainWindow::loadSettings() {
    // Lecture : toute la logique de format est dans SettingsStore, partagee
    // avec les tests (avant, le testSettings recopiait cette fonction et
    // passait meme si elle etait cassee).
    const SettingsData d = SettingsStore::load(m_settingsPath);

    m_dataSaver = d.dataSaver;
    m_imagesOff = d.imagesOff;
    m_ultraEco = d.ultraEco;
    m_favicons = d.favicons;
    m_restoreSession = d.restoreSession;
    m_session = d.session;

    // setEngineId ignore un moteur inconnu, setSearxUrl / setProviderUrl
    // refusent un schema non http(s) : un fichier modifie ne peut pas
    // transformer le navigateur en client arbitraire.
    if (!d.engineId.isEmpty())    m_search->setEngineId(d.engineId);
    if (!d.searxUrl.isEmpty())    m_search->setSearxUrl(d.searxUrl);
    if (!d.providerUrl.isEmpty()) m_search->setProviderUrl(d.providerUrl);
    m_search->setAutoFallback(d.autoFallback);
    m_search->setRemoteSuggestions(d.remoteSuggestions);

    for (const QString &h : d.imageAllowedHosts)
        m_interceptor->setImageHostAllowed(h, true);

    m_secGpc = d.secGpc;
    m_interceptor->setDataSaverEnabled(m_dataSaver);
    m_interceptor->setImagesOff(m_imagesOff);
    m_interceptor->setUltraEcoEnabled(m_ultraEco);
    m_interceptor->setSecGpcEnabled(m_secGpc);
    m_zoom = d.zoom;

    m_quality = ImageCodec::clampQuality(d.quality);
    if (m_ecoPanel) m_ecoPanel->setQualityValue(d.quality);
    m_omni->refreshEngineBadge();
    syncEcoWidgets();
    if (m_isPrivate) setWindowTitle(QStringLiteral("%1 — Privé (mémoire seule)").arg(QLatin1String(kAppName)));
    updateZoomLabel();
}

void MainWindow::saveSettings() {
    // Les donnees de session ne sont jamais ecrites en mode prive.
    SettingsData d;
    d.dataSaver = m_dataSaver;
    d.imagesOff = m_imagesOff;
    d.ultraEco = m_ultraEco;
    d.favicons = m_favicons;
    d.quality = m_quality;
    d.zoom = m_zoom;
    d.engineId = m_search->engineId();
    d.searxUrl = m_search->searxUrl();
    d.providerUrl = m_search->providerUrl();
    d.autoFallback = m_search->autoFallback();
    d.remoteSuggestions = m_search->remoteSuggestions();
    d.restoreSession = m_restoreSession;
    d.secGpc = m_secGpc;
    d.imageAllowedHosts = m_interceptor->imageAllowedHosts();

    if (!m_isPrivate) {
        for (int i = 0; i < m_tabs->count() && d.session.size() < kMaxSessionTabs; ++i) {
            const QString u = m_tabs->widget(i) ? currentUrlOf(i) : QString();
            if (!u.isEmpty()) d.session << u;
        }
    }

    // Ecriture atomique (QSaveFile) : une coupure pendant la sauvegarde ne
    // peut pas tronquer settings.txt et reinitialiser tous les reglages.
    if (!SettingsStore::save(m_settingsPath, d))
        setStatus(tr("Réglages non enregistrés (dossier non inscriptible ?)"), 6000);
}

QString MainWindow::currentUrlOf(int index) const {
    if (index < 0 || index >= m_tabInfos.size()) return QString();
    QWebEngineView *v = m_tabInfos.at(index)->view;
    if (!v) return QString();
    const QString s = v->url().toString();
    if (s.isEmpty() || s.startsWith(QLatin1String("about:"))) return QString();
    return s;
}

EcoTab* MainWindow::currentTabInfo() const {
    int idx = m_tabs->currentIndex();
    if (idx < 0 || idx >= m_tabInfos.size()) return nullptr;
    return m_tabInfos[idx];
}

QWebEngineView* MainWindow::currentView() const {
    QWidget *w = m_tabs->currentWidget();
    return qobject_cast<QWebEngineView*>(w);
}

void MainWindow::handleInternalAction(const QString &action, const QString &payload) {
    if (action == QLatin1String("search")) {
        if (auto v = currentView()) v->load(m_search->resolve(payload));
    } else if (action == QLatin1String("home")) {
        if (auto v = currentView()) v->setHtml(homePageHtml(), QUrl("about:home"));
    }
}

void MainWindow::onDownloadRequested(QWebEngineDownloadRequest *download) {
    if (!download || m_shuttingDown) return;

    QString suggested = download->suggestedFileName();
    if (suggested.isEmpty()) suggested = QStringLiteral("download");
    QString dlDir = download->downloadDirectory();
    if (dlDir.isEmpty()) dlDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dlDir.isEmpty()) dlDir = QDir::homePath() + QStringLiteral("/Téléchargements");
    QDir().mkpath(dlDir);

    // Le choix du dossier passe par une boite modale : on sort d'abord du
    // handler de signal (aucune boucle d'evenements imbriquee ici), sinon la
    // fenetre pouvait etre detruite sous le dialogue.
    QPointer<QWebEngineDownloadRequest> req(download);
    QTimer::singleShot(0, this, [this, req, suggested, dlDir]{
        if (!req) return;
        if (m_shuttingDown || !m_tabs) { req->cancel(); return; }
        const QString savePath = QFileDialog::getSaveFileName(
            this, tr("Enregistrer sous"), QDir(dlDir).filePath(suggested));
        if (savePath.isEmpty()) { req->cancel(); return; }
        startDownload(req, savePath);
    });
}

void MainWindow::startDownload(QPointer<QWebEngineDownloadRequest> download,
                               const QString &savePath)
{
    if (!download) return;

    // Alerte data-mobile : extensions lourdes = risque forfait
    const QString lower = savePath.toLower();
    static const QStringList heavy = { ".mp4", ".mkv", ".avi", ".webm", ".mp3", ".flac", ".iso",
                                       ".zip", ".rar", ".7z", ".tar.gz", ".exe", ".msi", ".dmg",
                                       ".apk", ".appimage", ".deb", ".rpm" };
    for (const QString &ext : heavy) {
        if (lower.endsWith(ext)) {
            const auto rep = QMessageBox::question(this, tr("Fichier lourd — données mobiles ?"),
                tr("Ce fichier peut être très lourd et vider ton forfait mobile :\n%1\n\nContinuer ?")
                    .arg(savePath), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (rep != QMessageBox::Yes) {
                download->cancel();
                setStatus(tr("Téléchargement annulé (fichier lourd)"), 4000);
                return;
            }
            break;
        }
    }

    const QFileInfo fi(savePath);
    download->setDownloadDirectory(fi.absolutePath());
    download->setDownloadFileName(fi.fileName());

    EcoDownload rec;
    rec.url = download->url().toString();
    rec.fileName = fi.fileName();
    rec.path = fi.absoluteFilePath();
    rec.total = download->totalBytes();
    m_downloads.append(rec);
    const int idx = m_downloads.size() - 1;
    setStatus(tr("Téléchargement : %1 — 0 %").arg(fi.fileName()));

    // Les captures sont des QPointer : si la requete est detruite entre deux
    // signaux, la lambda ne dereference plus rien (le crash SIGSEGV historique
    // venait de captures de pointeurs bruts vers l'objet emetteur).
    connect(download, &QWebEngineDownloadRequest::receivedBytesChanged, this,
            [this, download, idx]{
        if (idx < 0 || idx >= m_downloads.size() || download.isNull()) return;
        const qint64 rec2 = download->receivedBytes();
        const qint64 tot = download->totalBytes();
        m_downloads[idx].received = rec2;
        m_downloads[idx].total = tot;
        const int pct = tot > 0 ? int(rec2 * 100 / tot) : 0;
        m_progress->setValue(pct);
        m_progress->show();
        setStatus(tr("Téléchargement : %1 — %2 % (%3 / %4)")
                      .arg(m_downloads[idx].fileName).arg(pct)
                      .arg(humanBytes(rec2)).arg(humanBytes(tot)));
    });
    connect(download, &QWebEngineDownloadRequest::stateChanged, this,
            [this, download, idx](QWebEngineDownloadRequest::DownloadState state){
        if (download.isNull()) return;
        if (idx < 0 || idx >= m_downloads.size()) return;   // liste vide entre-temps
        m_downloads[idx].total = download->totalBytes();
        m_downloads[idx].received = download->receivedBytes();
        if (state == QWebEngineDownloadRequest::DownloadCompleted) {
            m_downloads[idx].state = 1;
            m_progress->hide();
            setStatus(tr("Téléchargé : %1").arg(m_downloads[idx].path), 6000);
        } else if (state == QWebEngineDownloadRequest::DownloadCancelled) {
            m_downloads[idx].state = 2;
            m_progress->hide();
            setStatus(tr("Téléchargement annulé"), 4000);
        } else if (state == QWebEngineDownloadRequest::DownloadInterrupted) {
            m_downloads[idx].state = 3;
            m_downloads[idx].message = download->interruptReasonString();
            m_progress->hide();
            const QString url = m_downloads[idx].url;
            const QString why = m_downloads[idx].message;
            setStatus(tr("Téléchargement échoué : %1").arg(url), 10000);
            // Alerte differee : on ne supprime pas l'objet emetteur depuis son
            // propre emission, et on n'ouvre pas de dialogue modal ici.
            QTimer::singleShot(0, this, [this, url, why]{
                QMessageBox::warning(this, tr("Échec"),
                    tr("Échec du téléchargement :\n%1\n%2").arg(url, why));
            });
        }
    });

    download->accept();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    m_shuttingDown = true;
    if (m_saveTimer) m_saveTimer->stop();
    saveSettings();
    event->accept();
}
