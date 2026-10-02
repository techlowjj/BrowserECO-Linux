#pragma once
#include <QWebEnginePage>
#include <QUrl>

class MainWindow;
class QWebEngineProfile;

class BrowserPage : public QWebEnginePage {
    Q_OBJECT
public:
    explicit BrowserPage(QWebEngineProfile *profile, MainWindow *mainWindow, QObject *parent = nullptr);

    /* Chargeur d'images a la demande.
     *
     * Enregistre le script sur le PROFIL : il s'execute a la creation de chaque
     * document, donc AVANT que le parseur HTML ne demande les images — c'est la
     * seule facon de vraiment ne pas telecharger (un script lance apres le
     * chargement arrive trop tard, les images sont deja parties).
     */
    static void installLazyImageScript(QWebEngineProfile *profile);

    /* Nombre d'images en attente sur la page courante, lu dans le compteur
     * tenu par le script. -1 si la page n'a pas encore repondu. */
    void refreshDeferredImageCount();
    int deferredImageCount() const { return m_deferredImages; }

signals:
    /* Nombre d'images en attente sur cette page (F5 : alimente le panneau Eco). */
    void imagesDeferred(int total);

protected:
    QWebEnginePage *createWindow(QWebEnginePage::WebWindowType type) override;
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override;

private:
    MainWindow *m_mainWindow = nullptr;
    int m_deferredImages = -1;
};
