#pragma once
#include <QWebEnginePage>
#include <QUrl>

class MainWindow;

class BrowserPage : public QWebEnginePage {
    Q_OBJECT
public:
    explicit BrowserPage(QWebEngineProfile *profile, MainWindow *mainWindow, QObject *parent = nullptr);

protected:
    QWebEnginePage *createWindow(QWebEnginePage::WebWindowType type) override;
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override;

private:
    MainWindow *m_mainWindow = nullptr;
};
