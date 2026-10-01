// Verifie la compatibilite du format de settings (ancien 5 lignes -> nouveau key=value)
#include <QCoreApplication>
#include <QTextStream>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QStringList>

// Replique la logique de MainWindow::loadSettings (meme source)
struct Parsed {
    bool dataSaver = true, imagesOff = true, ultraEco = false, favicons = true;
    bool autoFallback = true, remoteSuggest = true, restoreSession = false;
    int quality = 65; double zoom = 1.0;
    QString engine, searxUrl, providerUrl;
    QStringList session;
};

static Parsed parse(const QString &path) {
    Parsed p;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return p;
    QTextStream in(&f);
    QStringList lines;
    while (!in.atEnd()) lines << in.readLine().trimmed();
    bool qualitySeen = false;
    for (const QString &line : lines) {
        if (line.isEmpty() || line.startsWith('#')) continue;
        const int eq = line.indexOf('=');
        QString key = eq < 0 ? QString() : line.left(eq).trimmed();
        QString val = eq < 0 ? QString() : line.mid(eq + 1).trimmed();
        bool ok = false;
        if (eq < 0) {
            const int n = line.toInt(&ok);
            if (ok && !qualitySeen) { p.quality = qBound(0, n, 85); qualitySeen = true; }
            continue;
        }
        if (key == "dataSaver") p.dataSaver = val.toInt() != 0;
        else if (key == "imagesOff") p.imagesOff = val.toInt() != 0;
        else if (key == "ultraEco") p.ultraEco = val.toInt() != 0;
        else if (key == "favicons") p.favicons = val.toInt() != 0;
        else if (key == "quality") { const int n = val.toInt(&ok); if (ok) { p.quality = qBound(0, n, 85); qualitySeen = true; } }
        else if (key == "zoom") { const double z = val.toDouble(&ok); if (ok) p.zoom = qBound(0.25, z, 3.0); }
        else if (key == "engine") p.engine = val;
        else if (key == "searxUrl") p.searxUrl = val;
        else if (key == "providerUrl") p.providerUrl = val;
        else if (key == "autoFallback") p.autoFallback = val.toInt() != 0;
        else if (key == "remoteSuggest") p.remoteSuggest = val.toInt() != 0;
        else if (key == "restoreSession") p.restoreSession = val.toInt() != 0;
        else if (key == "session") p.session = val.split('|', Qt::SkipEmptyParts);
    }
    return p;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTemporaryDir dir;
    int fails = 0;
    auto chk = [&](const QString &label, bool cond) {
        if (!cond) ++fails;
        out << (cond ? "  ok   " : "  FAIL ") << label << "\n";
    };
    auto write = [&](const QString &name, const QString &content) {
        const QString p = dir.path() + "/" + name;
        QFile f(p); f.open(QIODevice::WriteOnly); f.write(content.toUtf8()); f.close();
        return p;
    };

    // 1. Ancien format Windows/Linux (5 lignes, 2e = qualite seule)
    {
        Parsed p = parse(write("old.txt", "dataSaver=1\n20\nimagesOff=1\nultraEco=0\nquality=20\n"));
        chk("ancien: dataSaver", p.dataSaver == true);
        chk("ancien: imagesOff", p.imagesOff == true);
        chk("ancien: ultraEco", p.ultraEco == false);
        chk("ancien: quality=20", p.quality == 20);
    }
    // 2. Ancien format ou la 2e ligne n'est pas un nombre
    {
        Parsed p = parse(write("old2.txt", "dataSaver=0\nxyz\nimagesOff=0\nultraEco=1\n"));
        chk("ancien degrade: quality defaut 65", p.quality == 65);
        chk("ancien degrade: ultraEco", p.ultraEco == true);
        chk("ancien degrade: imagesOff off", p.imagesOff == false);
    }
    // 3. Nouveau format complet
    {
        Parsed p = parse(write("new.txt",
            "# commentaire\ndataSaver=1\nimagesOff=0\nultraEco=0\nfavicons=1\nquality=72\nzoom=1.25\n"
            "engine=searx\nsearxUrl=https://mon.fr/searx\nproviderUrl=https://duckduckgo.com/ac/\n"
            "autoFallback=0\nremoteSuggest=0\nrestoreSession=1\nsession=https://a.fr|https://b.fr\n"));
        chk("nouveau: quality", p.quality == 72);
        chk("nouveau: zoom", qAbs(p.zoom - 1.25) < 0.001);
        chk("nouveau: engine", p.engine == "searx");
        chk("nouveau: searxUrl", p.searxUrl == "https://mon.fr/searx");
        chk("nouveau: providerUrl", p.providerUrl == "https://duckduckgo.com/ac/");
        chk("nouveau: autoFallback off", p.autoFallback == false);
        chk("nouveau: remoteSuggest off", p.remoteSuggest == false);
        chk("nouveau: restoreSession", p.restoreSession == true);
        chk("nouveau: session", p.session.size() == 2 && p.session.at(1) == "https://b.fr");
    }
    // 4. Bornes de securite
    {
        Parsed p = parse(write("bounds.txt", "quality=999\nzoom=50\n"));
        chk("quality borne a 85", p.quality == 85);
        chk("zoom borne a 3.0", qAbs(p.zoom - 3.0) < 0.001);
        Parsed n = parse(write("neg.txt", "quality=-5\nzoom=0.01\n"));
        chk("quality negative -> 0", n.quality == 0);
        chk("zoom negatif -> 0.25", qAbs(n.zoom - 0.25) < 0.001);
    }
    // 5. Fichier absent / vide / corrompu
    {
        Parsed p = parse(dir.path() + "/absent.txt");
        chk("absent: defauts", p.dataSaver && p.imagesOff && p.quality == 65);
        Parsed e = parse(write("empty.txt", ""));
        chk("vide: defauts", e.quality == 65);
        Parsed g = parse(write("garbage.txt", "ligne1\nligne2\n= \n=\n"));
        chk("corrompu: defauts", g.quality == 65 && g.dataSaver);
    }
    out << (fails == 0 ? "SETTINGS : OK\n" : QString("%1 ECHEC(S)\n").arg(fails));
    return fails ? 1 : 0;
}
