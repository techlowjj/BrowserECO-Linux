#include "browserpage.h"

#include <QFile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
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

/* Piege des .qrc en bibliotheque STATIQUE : browsereco_core est une bibliotheque
 * statique, et l'editeur de liens jette l'objet qui contient la ressource si
 * rien ne la reference — :/lazyload.js devient introuvable et la fonctionnalite
 * ne fait RIEN, silencieusement (verifie : le script etait absent du binaire).
 * L'initialisation est donc faite dans installLazyImageScript(), appele AVANT
 * toute lecture du fichier : elle ne depend donc plus du linkage, mais de
 * l'appel lui-meme. Si on oublie l'appel, le fichier manque aussi — mais on le
 * voit aussitot (avertissement), au lieu d'un echec muet. *//* Le script est lu UNE fois depuis la ressource Qt puis reutilise : le relire a
 * chaque page serait du gaspillage pour quelques kilo-octets. */
static QString lazyLoadScript()
{
    static const QString code = [] {
        QFile f(QStringLiteral(":/lazyload.js"));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
        return QString::fromUtf8(f.readAll());
    }();
    return code;
}

void BrowserPage::installLazyImageScript(QWebEngineProfile *profile)
{
    if (!profile) return;
    Q_INIT_RESOURCE(resources);   // avant de lire :/lazyload.js
    const QString js = lazyLoadScript();
    if (js.isEmpty()) {
        qWarning("BrowserPage: script de chargement d'images introuvable (:lazyload.js)");
        return;
    }

    QWebEngineScript script;
    script.setName(QStringLiteral("browsereco-lazy-images"));
    // DocumentCreation : execute avant le parsing du document, donc avant que
    // le navigateur ne demande les images du HTML initial.
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    // Monde applicatif : le script de la page ne peut ni le voir ni le
    // desactiver (un site hostile ne doit pas pouvoir forcer le chargement).
    script.setWorldId(QWebEngineScript::ApplicationWorld);
    script.setRunsOnSubFrames(false);
    script.setSourceCode(js);
    profile->scripts()->insert(script);
}

void BrowserPage::refreshDeferredImageCount()
{
    // Meme monde que le script (ApplicationWorld) : c'est la ou il tient son
    // compteur.
    runJavaScript(QStringLiteral("window.__browserecoLazy ? window.__browserecoLazy.count : -1"),
                  QWebEngineScript::ApplicationWorld, [this](const QVariant &v) {
        m_deferredImages = v.toInt();
        emit imagesDeferred(m_deferredImages);
    });
}
