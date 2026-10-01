#include "browserpage.h"
#include "mainwindow.h"
#include <QWebEngineProfile>
#include <QDebug>

BrowserPage::BrowserPage(QWebEngineProfile *profile, MainWindow *mainWindow, QObject *parent)
    : QWebEnginePage(profile, parent), m_mainWindow(mainWindow)
{
}

QWebEnginePage *BrowserPage::createWindow(QWebEnginePage::WebWindowType type) {
    Q_UNUSED(type)
    if (!m_mainWindow) return nullptr;
    // FIX: ouvre dans le même onglet (pas nouveau) comme demandé
    // Pour target=_blank et window.open, on charge dans l'onglet courant pour éviter "nouveau onglet à côté"
    // Si l'utilisateur veut vraiment un nouvel onglet, il peut faire Ctrl+Clic ou bouton + 
    qDebug() << "BrowserPage::createWindow type" << type << "-> same tab (fix)";
    if (auto *view = m_mainWindow->currentView()) {
        return view->page();
    }
    // fallback : crée nouvel onglet si pas de vue courante
    QWebEngineView *view = m_mainWindow->createNewTabView();
    if (!view) return nullptr;
    return view->page();
}

bool BrowserPage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) {
    // Liens internes "browseeco://action?..." : gere par MainWindow
    if (url.scheme() == QLatin1String("browseeco")) {
        if (m_mainWindow) {
            m_mainWindow->handleInternalAction(url.host(), url.query(QUrl::FullyDecoded));
        }
        return false;
    }
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}
