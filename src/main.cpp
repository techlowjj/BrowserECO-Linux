#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QLockFile>
#include <QTextStream>
#include "mainwindow.h"
#include "browsereco_version.h"

int main(int argc, char *argv[])
{
    // High DPI avant la création de QApplication
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setApplicationName("BrowserECO");
    QApplication::setApplicationDisplayName(QStringLiteral(BROWSERECO_DISPLAY_NAME));
    QApplication::setOrganizationName("BrowserECO");
    QApplication::setApplicationVersion(QStringLiteral(BROWSERECO_VERSION));
    // Indispensable pour que WM_CLASS corresponde a StartupWMClass du .desktop
    QApplication::setDesktopFileName(QStringLiteral("BrowserECO"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Navigateur économe en données : bloque pubs et trackers, "
                       "économise la bande passante."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption privateOpt(QStringLiteral("private"),
        QStringLiteral("Profil en mémoire : rien n'est écrit sur le disque."));
    const QCommandLineOption dataDirOpt(QStringLiteral("data-dir"),
        QStringLiteral("Dossier de profil (settings.txt, bases, WebEngineProfile)."),
        QStringLiteral("chemin"));
    parser.addOption(privateOpt);
    parser.addOption(dataDirOpt);
    parser.addPositionalArgument(QStringLiteral("url"),
        QStringLiteral("URL(s) à ouvrir au démarrage."));
    parser.process(app);

    // Style sombre global pour Linux
    app.setStyle("Fusion");
    QPalette dark;
    dark.setColor(QPalette::Window, QColor("#0F0F17"));
    dark.setColor(QPalette::WindowText, QColor("#E8E8F0"));
    dark.setColor(QPalette::Base, QColor("#1A1A2E"));
    dark.setColor(QPalette::AlternateBase, QColor("#242440"));
    dark.setColor(QPalette::ToolTipBase, QColor("#E8E8F0"));
    dark.setColor(QPalette::ToolTipText, QColor("#0F0F17"));
    dark.setColor(QPalette::Text, QColor("#E8E8F0"));
    dark.setColor(QPalette::Button, QColor("#242440"));
    dark.setColor(QPalette::ButtonText, QColor("#E8E8F0"));
    dark.setColor(QPalette::Highlight, QColor("#00D4AA"));
    dark.setColor(QPalette::HighlightedText, QColor("#0F0F17"));
    app.setPalette(dark);

    // Une seule instance par dossier de profil : deux fenêtres ouvertes sur le
    // meme dossier se disputeraient settings.txt et les bases SQLite (WAL).
    // Le verrou se deverrouille seul si le processus precedent a disparu
    // (QLockFile verifie le PID).
    QString profileDir = parser.value(dataDirOpt);
    if (profileDir.isEmpty()) profileDir = QCoreApplication::applicationDirPath();
    const QString lockName = QStringLiteral("BrowserECO-%1.lock")
                                 .arg(QString::fromLatin1(
                                     QCryptographicHash::hash(profileDir.toUtf8(),
                                                              QCryptographicHash::Sha1)
                                         .toHex()));
    QLockFile lock(QDir(QDir::tempPath()).filePath(lockName));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100)) {
        QTextStream(stderr) << QStringLiteral("Une autre instance de %1 est deja en cours.\n")
                                   .arg(QApplication::applicationDisplayName());
        return 1;
    }

    MainWindow w(nullptr, parser.value(dataDirOpt), parser.isSet(privateOpt));
    for (const QString &a : parser.positionalArguments())
        w.openUrlAtStartup(a);
    w.show();
    return app.exec();
}