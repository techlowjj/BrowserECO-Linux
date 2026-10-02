/*
 * Format de settings.txt — testé sur le VRAI code de production.
 *
 * Ce fichier testait jusqu'ici une COPIE de la logique de lecture écrite dans
 * le test lui-même : il passait donc même si le vrai parseur était cassé. Il
 * s'appuie maintenant sur SettingsStore, celui qu'utilise MainWindow.
 */
#include <QtTest>
#include <QTemporaryDir>
#include "services/settingsstore.h"

class TestSettings : public QObject {
    Q_OBJECT
private slots:
    void defautsSurFichierAbsent();
    void lectureClefValeur();
    void ancienFormat5Lignes();
    void bornesSurLesValeurs();
    void fichierCorrompuNeCassePas();
    void booleensEnTexte();
    void listeDHotes();
    void ecritureAtomique();
    void allerRetour();
    void neRepasEcrasePasUnFichierEnCasDEchec();
};

void TestSettings::defautsSurFichierAbsent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const SettingsData d = SettingsStore::load(dir.filePath("absent.txt"));
    QCOMPARE(d.quality, 65);
    QCOMPARE(d.zoom, 1.0);
    QVERIFY(d.dataSaver);
    QVERIFY(d.imagesOff);
    QVERIFY(!d.ultraEco);
    QVERIFY(d.favicons);
    QVERIFY(d.autoFallback);
    QVERIFY(d.session.isEmpty());
    QVERIFY(d.imageAllowedHosts.isEmpty());
    // Sec-GPC est actif par defaut : c'est le signal de vie privee du navigateur.
    QVERIFY(d.secGpc);
}

void TestSettings::lectureClefValeur()
{
    const SettingsData d = SettingsStore::parse(QStringLiteral(
        "# commentaire\n"
        "dataSaver=0\n"
        "imagesOff=1\n"
        "ultraEco=1\n"
        "favicons=0\n"
        "quality=27\n"
        "zoom=0.90\n"
        "engine=brave\n"
        "searxUrl=https://mon-searx.example/search\n"
        "providerUrl=https://duckduckgo.com/ac/\n"
        "autoFallback=0\n"
        "remoteSuggest=0\n"
        "restoreSession=1\n"
        "secGpc=0\n"
        "imageAllow=exemple.fr|photos.net\n"
        "session=https://a.fr|https://b.fr\n"));
    QCOMPARE(d.dataSaver, false);
    QCOMPARE(d.imagesOff, true);
    QCOMPARE(d.ultraEco, true);
    QCOMPARE(d.favicons, false);
    QCOMPARE(d.quality, 27);
    QCOMPARE(d.zoom, 0.90);
    QCOMPARE(d.engineId, QStringLiteral("brave"));
    QCOMPARE(d.searxUrl, QStringLiteral("https://mon-searx.example/search"));
    QCOMPARE(d.providerUrl, QStringLiteral("https://duckduckgo.com/ac/"));
    QCOMPARE(d.autoFallback, false);
    QCOMPARE(d.remoteSuggestions, false);
    QCOMPARE(d.restoreSession, true);
    QCOMPARE(d.secGpc, false);
    QCOMPARE(d.imageAllowedHosts.size(), 2);
    QCOMPARE(d.session.size(), 2);
}

void TestSettings::ancienFormat5Lignes()
{
    // Format Windows d'origine : des lignes cle=valeur ET une ligne nue qui
    // porte la qualite seule (2e ligne dans les fichiers produits avant 1.1).
    const SettingsData d = SettingsStore::parse(QStringLiteral(
        "dataSaver=1\n20\nimagesOff=1\nultraEco=0\nquality=20\n"));
    QCOMPARE(d.quality, 20);
    QCOMPARE(d.dataSaver, true);
    QCOMPARE(d.imagesOff, true);
    QCOMPARE(d.ultraEco, false);

    // Version degradee : la ligne nue n'est pas un nombre -> defaut conserve,
    // le reste du fichier est lu normalement.
    const SettingsData d2 = SettingsStore::parse(QStringLiteral(
        "dataSaver=0\nxyz\nimagesOff=0\nultraEco=1\n"));
    QCOMPARE(d2.quality, 65);
    QCOMPARE(d2.dataSaver, false);
    QCOMPARE(d2.imagesOff, false);
    QCOMPARE(d2.ultraEco, true);
}

void TestSettings::bornesSurLesValeurs()
{
    QCOMPARE(SettingsStore::parse(QStringLiteral("quality=999")).quality, 85);
    QCOMPARE(SettingsStore::parse(QStringLiteral("quality=-5")).quality, 0);
    QCOMPARE(SettingsStore::parse(QStringLiteral("zoom=99")).zoom, 3.0);
    QCOMPARE(SettingsStore::parse(QStringLiteral("zoom=0.001")).zoom, 0.25);
    // Valeur non numerique : le defaut est conserve, aucune exception.
    QCOMPARE(SettingsStore::parse(QStringLiteral("quality=abc")).quality, 65);
    QCOMPARE(SettingsStore::parse(QStringLiteral("zoom=beaucoup")).zoom, 1.0);
}

void TestSettings::fichierCorrompuNeCassePas()
{
    // Ni binaire coupe, ni ligne sans « = », ni cle inconnue : le fichier est
    // librement modifiable, l'application doit demarrer quand meme.
    QByteArray brute;
    brute.append("quality=50\n");
    brute.append(QByteArray("\x00\x01\x02\xff", 4));
    brute.append("\ncleInconnue=42\n");
    brute.append("   \n");
    const SettingsData d = SettingsStore::parse(QString::fromUtf8(brute));
    QCOMPARE(d.quality, 50);
}

void TestSettings::booleensEnTexte()
{
    QVERIFY(SettingsStore::parse(QStringLiteral("imagesOff=true")).imagesOff);
    QVERIFY(SettingsStore::parse(QStringLiteral("imagesOff=oui")).imagesOff);
    QVERIFY(!SettingsStore::parse(QStringLiteral("imagesOff=false")).imagesOff);
    QVERIFY(!SettingsStore::parse(QStringLiteral("imagesOff=non")).imagesOff);
    // Valeur incomprehensible : le defaut est conserve
    QVERIFY(SettingsStore::parse(QStringLiteral("imagesOff=peut-etre")).imagesOff);
}

void TestSettings::listeDHotes()
{
    // Un chemin ou une entree sans point n'est pas un nom de domaine : refuse.
    const QStringList l = SettingsStore::parseHostList(
        QStringLiteral("Exemple.FR|b.fr|/etc/passwd|localhost||a.fr|x.fr|"));
    QCOMPARE(l, QStringList({QStringLiteral("a.fr"), QStringLiteral("b.fr"),
                             QStringLiteral("exemple.fr"), QStringLiteral("x.fr")}));
    QCOMPARE(SettingsStore::joinHostList(QStringList({QStringLiteral("b.fr"),
                                                     QStringLiteral("A.fr"),
                                                     QStringLiteral("b.fr")})),
             QStringLiteral("a.fr|b.fr"));
}

void TestSettings::ecritureAtomique()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.txt");

    SettingsData d;
    d.quality = 31;
    d.engineId = QStringLiteral("wiby");
    d.imageAllowedHosts = QStringList({QStringLiteral("exemple.fr")});
    d.session = QStringList({QStringLiteral("https://a.fr")});
    QVERIFY2(SettingsStore::save(path, d), "save() doit réussir");

    // Aucun fichier temporaire ne doit subsister (QSaveFile renomme).
    const QStringList leftovers = QDir(dir.path()).entryList(QDir::Files);
    QCOMPARE(leftovers, QStringList({QStringLiteral("settings.txt")}));

    // Le contenu ecrit est relisible et identique.
    const SettingsData back = SettingsStore::load(path);
    QCOMPARE(back.quality, 31);
    QCOMPARE(back.engineId, QStringLiteral("wiby"));
    QCOMPARE(back.imageAllowedHosts, d.imageAllowedHosts);
    QCOMPARE(back.session, d.session);
}

void TestSettings::allerRetour()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("settings.txt");
    SettingsData d;
    d.dataSaver = false;
    d.imagesOff = true;
    d.ultraEco = true;
    d.favicons = false;
    d.quality = 88;          // borné à l'écriture
    d.zoom = 2.5;
    d.autoFallback = false;
    d.remoteSuggestions = false;
    d.restoreSession = true;
    d.secGpc = false;
    d.imageAllowedHosts = QStringList({QStringLiteral("z.fr"), QStringLiteral("a.fr")});
    d.session = QStringList({QStringLiteral("https://un.fr"), QStringLiteral("https://deux.fr")});
    QVERIFY(SettingsStore::save(path, d));

    const SettingsData back = SettingsStore::load(path);
    QCOMPARE(back.dataSaver, d.dataSaver);
    QCOMPARE(back.imagesOff, d.imagesOff);
    QCOMPARE(back.ultraEco, d.ultraEco);
    QCOMPARE(back.favicons, d.favicons);
    QCOMPARE(back.quality, SettingsStore::kQualityMax);
    QCOMPARE(back.zoom, d.zoom);
    QCOMPARE(back.autoFallback, d.autoFallback);
    QCOMPARE(back.remoteSuggestions, d.remoteSuggestions);
    QCOMPARE(back.restoreSession, d.restoreSession);
    QCOMPARE(back.secGpc, d.secGpc);
    QCOMPARE(back.session, d.session);
    // Les hotes sont ecrits tries : le fichier reste stable d'une sauvegarde
    // a l'autre (pas de diff inutile a chaque sortie).
    QCOMPARE(back.imageAllowedHosts, QStringList({QStringLiteral("a.fr"), QStringLiteral("z.fr")}));
}

void TestSettings::neRepasEcrasePasUnFichierEnCasDEchec()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("settings.txt");
    SettingsData d;
    d.quality = 42;
    QVERIFY(SettingsStore::save(path, d));

    // Ecriture dans un dossier inexistant : echec NET, et surtout le fichier
    // existant reste intact (c'est tout l'interet de QSaveFile).
    const bool ok = SettingsStore::save(dir.filePath("absent/sous/x.txt"), d);
    QVERIFY(!ok);
    QCOMPARE(SettingsStore::load(path).quality, 42);
}

QTEST_MAIN(TestSettings)
#include "test_settings.moc"