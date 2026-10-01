#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QProgressBar>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineDownloadRequest>
#include <QWebEngineSettings>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QKeySequence>
#include <QPointer>
#include <QTimer>
#include <QLineEdit>
#include <QTabBar>
#include <QVector>

#include "ecointerceptor.h"
#include "services/adblocker.h"
#include "services/cachemanager.h"
#include "services/historymanager.h"
#include "services/imageoptimizer.h"
#include "services/searchengine.h"
#include "services/serpguard.h"

class OmniBox;
class EcoPanel;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

/* Une ligne du raccourci : texte affiche dans la fenetre F1. Remplie au meme
   endroit que la creation du QShortcut (voir setupConnections) pour que la
   liste annoncee soit exactement celle qui existe. */
struct ShortcutRow {
    QString keys;
    QString label;
    bool eco = false;   // section « Economie de donnees »
};

struct EcoTab {
    QWebEngineView *view = nullptr;
    QString title = "Nouvel onglet";
    QString url;
};

/* Un telechargement en cours, pour le panneau Telechargements (Ctrl+J) */
struct EcoDownload {
    QString url;
    QString fileName;
    QString path;
    qint64 received = 0;
    qint64 total = 0;
    int state = 0; // 0 en cours, 1 fini, 2 annule, 3 erreur
    QString message;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr,
                       const QString &dataDir = QString(),
                       bool privateMode = false);
    ~MainWindow();

    // URL (ou texte) passe en ligne de commande : ouvert dans l'onglet courant
    // s'il est encore vierge, sinon dans un nouvel onglet.
    void openUrlAtStartup(const QString &urlOrText);
    // Pour BrowserPage::createWindow
    QWebEngineView* createNewTabView();
    QWebEngineView* currentView() const;
    // Acces ponctuel (tests, integration) : ces classes ne derives pas de QObject,
    // findChild<>() ne peut donc pas les retrouver.
    HistoryManager* historyManager() const { return m_history; }
    SearchEngineManager* searchManager() const { return m_search; }
    AdBlocker* adblocker() const { return const_cast<AdBlocker *>(&m_adblocker); }
    EcoInterceptor* interceptor() const { return m_interceptor; }
    // Sert aux liens "browseeco://" de la page d'accueil
    void handleInternalAction(const QString &action, const QString &payload);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void newTab(const QUrl &url = QUrl());
    void closeTab(int index);
    void closeCurrentTab();
    void onUrlEntered(const QUrl &url);
    void onTabChanged(int index);
    void onTabMoved(int from, int to);
    void updateNavigationActions();
    void onTitleChanged(const QString &title);
    void onUrlChanged(const QUrl &url);
    void onLoadFinished(bool ok);
    void onLoadProgress(int progress);
    void toggleDataSaver(bool checked);
    void toggleImagesOff(bool checked);
    void toggleUltraEco(bool checked);
    void onQualityChanged(int value);
    void showHistory();
    void showDownloads();
    void showShortcuts();
    void showAbout();
    void showMenuAt(const QPoint &globalPos);
    void showEngineMenuAt(const QPoint &globalPos);
    void showFindBar();
    void findInPage(bool backwards);
    void closeFindBar();
    void clearCache();
    void updateStats();
    void pollStats();
    void onBlocked(qint64 count);
    void onDownloadRequested(QWebEngineDownloadRequest *download);
    void onSerpBlocked(const QString &engineId);
    void adoptWorkingEngine(const QString &engineId);
    void onTabBarDoubleClicked(int index);
    void applySessionAction(int actionId);
    void setZoom(double factor);
    void scheduleSaveSettings();

private:
    void setupUi();
    void setupConnections();
    void loadSettings();
    void saveSettings();
    QString homePageHtml() const;
    void navigateCurrent(const QUrl &url);
    EcoTab* currentTabInfo() const;
    // m_tabInfos n'est PAS indexes par index d'onglet (les onglets sont
    // deplacables) : on recherche toujours l'enregistrement par vue.
    EcoTab* tabInfoForView(const QWebEngineView *view) const;
    int tabCountOfView(const QWebEngineView *view) const;
    bool isWebUrl(const QUrl &url) const;
    void showErrorPage(const QWebEngineView *view, const QString &reason);
    void handleRenderProcessTerminated(QWebEngineView *view,
                                       QWebEnginePage::RenderProcessTerminationStatus status,
                                       int exitCode);
    void startDownload(QPointer<QWebEngineDownloadRequest> download, const QString &savePath);
    void applyWebSettings();
    void setStatus(const QString &text, int timeoutMs = 0);
    void refreshStatusLine();
    void showEcoPanelAt(const QPoint &globalPos);
    void chooseSearchEngine(const QString &engineId);
    void applyQuality(int value, bool fromUser);
    void syncEcoWidgets();
    void updateZoomLabel();
    void updateTabChrome(QWebEngineView *view);
    QString currentUrlOf(int index) const;

    // UI
    QWidget *m_central = nullptr;
    QVBoxLayout *m_mainLayout = nullptr;
    QWidget *m_toolBar = nullptr;
    QHBoxLayout *m_toolLayout = nullptr;
    QTabWidget *m_tabs = nullptr;
    OmniBox *m_omni = nullptr;
    QTabBar *m_tabBar = nullptr;
    QToolButton *m_backBtn = nullptr;
    QToolButton *m_forwardBtn = nullptr;
    QToolButton *m_reloadBtn = nullptr;
    QToolButton *m_homeBtn = nullptr;
    QToolButton *m_newTabBtn = nullptr;
    QToolButton *m_ecoBtn = nullptr;
    QToolButton *m_menuBtn = nullptr;
    QToolButton *m_privacyBtn = nullptr;
    EcoPanel *m_ecoPanel = nullptr;
    QLabel *m_ecoBadge = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QWidget *m_findBar = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLabel *m_findInfo = nullptr;
    QTimer *m_statsTimer = nullptr;
    // Evite d'ecraser settings.txt a chaque cran de zoom / de curseur :
    // une seule ecriture, 400 ms apres la derniere modification.
    QTimer *m_saveTimer = nullptr;

    // Services
    AdBlocker m_adblocker;
    CacheManager *m_cache = nullptr;
    HistoryManager *m_history = nullptr;
    ImageOptimizer m_imageOptimizer;
    EcoInterceptor *m_interceptor = nullptr;
    QWebEngineProfile *m_profile = nullptr;
    SearchEngineManager *m_search = nullptr;
    SerpGuard *m_serpGuard = nullptr;

    // State
    QVector<EcoTab*> m_tabInfos;
    QVector<EcoDownload> m_downloads;
    QVector<ShortcutRow> m_navShortcuts;
    QVector<ShortcutRow> m_ecoShortcuts;
    bool m_dataSaver = true;
    bool m_imagesOff = true;
    bool m_ultraEco = false;
    // Qualite choisie par l'utilisateur (0-85). Distincte de la qualite
    // EFFECTIVE : le mode Ultra la plafonne a 50 sans ecraser le reglage.
    int m_quality = 65;
    bool m_favicons = true;
    bool m_isPrivate = false;
    bool m_restoreSession = false;
    QString m_serpEngine;
    QString m_lastQuery;
    QStringList m_serpTried;
    QStringList m_session;
    double m_zoom = 1.0;
    // Instant (ms) jusqu'auquel un message de setStatus() garde la main sur
    // la ligne d'etat ; 0 = aucune surcharge. Sans cela le timer de stats
    // effacait en 350 ms tout message (erreurs de chargement, telechargements).
    qint64 m_statusUntil = 0;
    // Compteurs mis en cache pour ne pas interroger SQLite toutes les 350 ms.
    int m_cachedRules = 0;
    int m_cachedCacheCount = 0;
    int m_cachedHistoryCount = 0;
    bool m_shuttingDown = false;
    QString m_settingsPath;
};
