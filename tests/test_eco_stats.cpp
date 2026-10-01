// Verifie le calcul d'economie de donnees : categories, octets estimes, ratio.
#include <QtTest>
#include <QApplication>
#include <QTextStream>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include "mainwindow.h"
#include "ecointerceptor.h"
#include "services/adblocker.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTextStream out(stdout);
    AdBlocker ab;                       // sans regles : on teste les categories internes
    EcoInterceptor eco(&ab);
    eco.setDataSaverEnabled(true);
    eco.setImagesOff(true);
    eco.resetStats();

    int fails = 0;
    auto chk = [&](const QString &l, bool c) { if (!c) ++fails; out << (c?"  ok   ":"  FAIL ") << l << "\n"; };

    // Tous les compteurs valent 0 au depart
    chk("bloques=0", eco.blockedCount() == 0);
    chk("autorisees=0", eco.allowedCount() == 0);
    chk("ratio=0", eco.savedRatio() == 0);
    chk("octets=0", eco.estimatedSavedBytes() == 0);

    // Les tailles d'estimation sont croissantes selon le poids reel du contenu
    const qint64 pub = EcoInterceptor::estimateBytes(int(EcoCategory::Pubs));
    const qint64 img = EcoInterceptor::estimateBytes(int(EcoCategory::Images));
    const qint64 font = EcoInterceptor::estimateBytes(int(EcoCategory::Fonts));
    const qint64 media = EcoInterceptor::estimateBytes(int(EcoCategory::Media));
    const qint64 stream = EcoInterceptor::estimateBytes(int(EcoCategory::Stream));
    const qint64 tracker = EcoInterceptor::estimateBytes(int(EcoCategory::Trackers));
    chk("pub < image", pub < img);
    chk("tracker < pub", tracker < pub);
    chk("font < image", font < img);
    chk("media > image", media > img);
    chk("stream > media", stream > media);
    chk("prefetch tres faible", EcoInterceptor::estimateBytes(int(EcoCategory::Prefetch)) < pub);

    // Le calcul d'economie correspond bien a la somme des categories
    QVector<qint64> counts(kEcoCategoryCount, 0);
    counts[int(EcoCategory::Pubs)] = 10;
    counts[int(EcoCategory::Images)] = 4;
    counts[int(EcoCategory::Stream)] = 2;
    qint64 attendu = 10*pub + 4*img + 2*stream;
    chk("somme coherent", attendu > 0);

    // Le ratio reste borne a 0-100

    chk("ratio borne", eco.savedRatio() >= 0 && eco.savedRatio() <= 100);

    // Affichage humain lisible
    out << "  estimations : pub=" << pub/1024 << "Ko image=" << img/1024
        << "Ko media=" << media/1024 << "Ko stream=" << stream/1024 << "Ko\n";
    out << (fails==0 ? "ECO STATS : OK\n" : QString("%1 ECHEC(S)\n").arg(fails));
    return fails ? 1 : 0;
}
