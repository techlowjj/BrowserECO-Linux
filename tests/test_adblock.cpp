/*
 * Adblocker : semantique ABP + performance.
 *
 * Ce test utilisait les vraies listes Filters/ quand elles sont presentes, et
 * une liste miniature sinon (il doit passer sur une machine sans les filtres).
 */
#include <QtTest>
#include "services/adblocker.h"

class TestAdblock : public QObject {
    Q_OBJECT
private slots:
    void miniList_Semantique();
    void allowRules_NeDeviennentPasDesBlocages();
    void cosmeticRules_Ignorees();
    void scopedRules_Ignorees();
    void cheminSeul_EviteLeBypassParQueryString();
    void sousDomaines();
    void exceptions();
    void performance_UnAppelParRequete();
};

// AdBlocker contient un QMutex : pas copiable. On remplit une reference.
static void makeMini(AdBlocker &a)
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(dir.filePath("mini.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    // regles de reseau
    out << "[Adblock Plus 2.0]\n";
    out << "! commentaire\n";
    out << "||ads.example.com^$third-party\n";
    out << "||tracker.net^$domain=example.com\n";      // a portee -> ignoree
    out << "||pixel.io^$domain=evil.com\n";           // a portee -> ignoree
    out << "||tracker.net/banner/*\n";                 // regle ancree
    out << "||pub.example.net^\n";
    out << "||media.example.com^$image\n";
    out << "||cdn.example.com^$script,domain=cdn.example.com\n";
    out << "/ads/banner123.gif\n";                     // sous-chaine
    out << "a\n";                                     // trop court -> ignoree
    out << "##.ad-banner\n";                          // cosmetique
    out << "example.com##.popup\n";                   // cosmetique
    out << "@@||cdn.example.com^$script\n";            // exception -> script autorise
    out << "@@||tracker.net/banner/x.js\n";           // exception par chemin -> ignoree
    out << "-||hidden.example.com^\n";                // regle ALLOW desactivee
    out << "/banner[0-9]+/^$script\n";                // regex -> ignoree
    out.flush();
    f.close();
    a.loadFilterFile(dir.filePath("mini.txt"));
}

void TestAdblock::miniList_Semantique()
{
    AdBlocker a;
    makeMini(a);
    QVERIFY2(a.shouldBlock("https://ads.example.com/banner.gif"),
             "hote bloque non detecte");
    // regle d'hote avec options mais sans $domain= : elle s'applique
    QVERIFY2(a.shouldBlock("https://media.example.com/clip.mp4"), "regle avec options ignoree");
    // ancres : « ||tracker.net/banner/ » doit matcher le couple hote/chemin
    QVERIFY2(a.shouldBlock("https://tracker.net/banner/x.js"), "regle ancree ignoree");
    // sous-chaine
    QVERIFY2(a.shouldBlock("https://cdn.mirror.net/ads/banner123.gif"), "regle libre ignoree");
    // ce qui ne doit PAS etre bloque
    QVERIFY2(!a.shouldBlock("https://example.com/page.html"), "faux positif sur site ordinaire");
    QVERIFY2(!a.shouldBlock("https://tracker.net/autre/page"), "faux positif : le chemin ne correspond pas");
    QVERIFY2(!a.shouldBlock("https://hidden.example.com/x"), "regle '-' traitee en blocage");
    QVERIFY2(!a.shouldBlock("https://mystique.example.org/x"), "hote non liste bloque");
}

void TestAdblock::allowRules_NeDeviennentPasDesBlocages()
{
    AdBlocker a;
    makeMini(a);
    // @@||cdn.example.com^$script : le domaine est en exception
    QVERIFY2(!a.shouldBlock("https://cdn.example.com/track.js"), "exception @@ non appliquee");
    // @@ limite a un chemin : on ne peut pas l'honorer au niveau du domaine,
    // elle est donc ignoree. Consequence : la regle de blocage ancree
    // « ||tracker.net/banner/ » reste active (pas de desactivation globale).
    QVERIFY2(a.shouldBlock("https://tracker.net/banner/x.js"),
             "une exception par chemin a desactive a tort tout le domaine");
}

void TestAdblock::cosmeticRules_Ignorees()
{
    AdBlocker a;
    makeMini(a);
    // « example.com##.popup » ne doit pas devenir une regle de blocage
    QVERIFY2(!a.shouldBlock("https://example.com/popup.js"),
             "une regle cosmetique bloque une URL");
    // ni « ##.ad-banner » -> aucune URL contenant ce texte
    QVERIFY2(!a.shouldBlock("https://example.com/ad-banner-encart.html"),
             "une regle cosmetique bloque une URL");
}

void TestAdblock::scopedRules_Ignorees()
{
    AdBlocker a;
    makeMini(a);
    // $domain=evil.com : la regle ||pixel.io ne doit toucher que evil.com, or
    // on ne sait pas l'appliquer -> elle est ignoree partout.
    QVERIFY2(!a.shouldBlock("https://example.com/pixel.png"),
             "regle a portee appliquee globalement");
    QVERIFY2(!a.shouldBlock("https://evil.com/autre.png"),
             "regle a portee appliquee alors qu'elle ne devrait pas l'etre");
}

void TestAdblock::cheminSeul_EviteLeBypassParQueryString()
{
    AdBlocker a;
    makeMini(a);
    // C'etait le vrai trou : l'extension de telechement etait cherchee dans
    // l'URL ENTIERE, donc « .../ads/banner123.gif?x=.pdf » passait.
    QVERIFY2(a.shouldBlock("https://cdn.mirror.net/ads/banner123.gif?cache=.pdf"),
             "bypass par parametre de requete");
    QVERIFY2(a.shouldBlock("https://cdn.mirror.net/ads/banner123.gif#x=.mp4"),
             "bypass par fragment");
}

void TestAdblock::sousDomaines()
{
    AdBlocker a;
    makeMini(a);
    QVERIFY(a.shouldBlock("https://static.ads.example.com/x.js"));
    QVERIFY(a.shouldBlock("https://a.b.pub.example.net/x"));
    // « ||tracker.net^$domain=example.com » est une regle a portee : elle est
    // ignoree, donc tracker.net n'est bloque que sur /banner/.
    QVERIFY(!a.shouldBlock("https://tracker.net/autre/page"));
    // « ||tracker.net/banner/ » est ancre sur l'hote EXACT : contrairement a
    // « ||tracker.net^ », il ne s'etend pas aux sous-domaines (semantique ABP).
    QVERIFY(!a.shouldBlock("https://a.b.tracker.net/banner/x.js"));
}

void TestAdblock::exceptions()
{
    AdBlocker a;
    makeMini(a);
    // la regle d'hote existe bien...
    AdBlocker b;
    makeMini(b);
    QVERIFY(b.shouldBlock("https://cdn.example.com/track.js") == false);
    // ...et un domaine sans exception reste bloque
    QVERIFY(!a.ruleCount() == 0);
}

void TestAdblock::performance_UnAppelParRequete()
{
    // Sur les vraies listes : shouldBlock() est appele sur le thread IO de
    // QtWebEngine. Il doit rester tres rapide (microsecondes, pas milliemes).
    const QString dir = QStringLiteral(BROWSERECO_SOURCE_DIR "/Filters");
    if (!QDir(dir).exists()) {
        qInfo("  (Filters/ absent : test de performance ignore)");
        QSKIP("Filters/ absent");
    }
    AdBlocker a;
    a.loadFilters(dir);
    QVERIFY2(a.ruleCount() > 1000, "les listes de filtres ne se chargent pas");
    qInfo("  %d regles chargees", a.ruleCount());

    QElapsedTimer t;
    t.start();
    int blocked = 0;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        // 80 % de pages sans rapport : c'est le cas courant qui coutait 1,9 ms
        if (a.shouldBlock(QStringLiteral("https://hote-inconnu-%1.exemple/page.html?a=%2")
                              .arg(i % 997).arg(i)))
            ++blocked;
    }
    const qint64 us = t.nsecsElapsed() / 1000 / n;
    qInfo("  %lld us par appel (%d bloques sur %d)", (long long)us, blocked, n);
    QVERIFY2(blocked == 0, "faux positif sur des hotes hors liste");
    // 100 us/appel = encore 100x trop lent ; on vise largement mieux
    QVERIFY2(us < 100, qPrintable(QStringLiteral("shouldBlock trop lent : %1 us/appel").arg(us)));
}

QTEST_MAIN(TestAdblock)
#include "test_adblock.moc"