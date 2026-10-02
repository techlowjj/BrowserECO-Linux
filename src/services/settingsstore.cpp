#include "settingsstore.h"
#include <QSaveFile>
#include <QTextStream>
#include <QFile>
#include <QDebug>

namespace {

bool toBool(const QString &v, bool def)
{
    bool ok = false;
    const int n = v.toInt(&ok);
    if (ok) return n != 0;
    const QString s = v.trimmed().toLower();
    if (s == QLatin1String("true") || s == QLatin1String("oui") || s == QLatin1String("on")) return true;
    if (s == QLatin1String("false") || s == QLatin1String("non") || s == QLatin1String("off")) return false;
    return def;
}

} // namespace

namespace SettingsStore {

SettingsData parse(const QString &contents)
{
    SettingsData d;
    bool qualitySeen = false;

    const QStringList lines = contents.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0) {
            // Ancien format : une valeur numerique seule sur la ligne
            // (la 2e ligne portait la qualite).
            bool ok = false;
            const int n = line.toInt(&ok);
            if (ok && !qualitySeen) {
                d.quality = qBound(kQualityMin, n, kQualityMax);
                qualitySeen = true;
            }
            continue;
        }

        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1).trimmed();
        bool ok = false;

        if (key == QLatin1String("dataSaver"))            d.dataSaver = toBool(val, d.dataSaver);
        else if (key == QLatin1String("imagesOff"))       d.imagesOff = toBool(val, d.imagesOff);
        else if (key == QLatin1String("ultraEco"))        d.ultraEco = toBool(val, d.ultraEco);
        else if (key == QLatin1String("favicons"))        d.favicons = toBool(val, d.favicons);
        else if (key == QLatin1String("quality")) {
            const int n = val.toInt(&ok);
            if (ok) { d.quality = qBound(kQualityMin, n, kQualityMax); qualitySeen = true; }
        } else if (key == QLatin1String("zoom")) {
            const double z = val.toDouble(&ok);
            if (ok) d.zoom = qBound(kZoomMin, z, kZoomMax);
        }
        else if (key == QLatin1String("engine"))           d.engineId = val;
        else if (key == QLatin1String("searxUrl"))         d.searxUrl = val;
        else if (key == QLatin1String("providerUrl"))      d.providerUrl = val;
        else if (key == QLatin1String("autoFallback"))     d.autoFallback = toBool(val, d.autoFallback);
        else if (key == QLatin1String("remoteSuggest"))   d.remoteSuggestions = toBool(val, d.remoteSuggestions);
        else if (key == QLatin1String("restoreSession"))   d.restoreSession = toBool(val, d.restoreSession);
        else if (key == QLatin1String("secGpc"))          d.secGpc = toBool(val, d.secGpc);
        else if (key == QLatin1String("session"))          d.session = val.split(QLatin1Char('|'), Qt::SkipEmptyParts);
        else if (key == QLatin1String("imageAllow"))       d.imageAllowedHosts = parseHostList(val);
        // cle inconnue : ignoree (le format peut evoluer sans casser l'ancien)
    }
    return d;
}

QStringList parseHostList(const QString &value)
{
    QStringList out;
    const QStringList parts = value.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        const QString h = p.trimmed().toLower();
        if (h.isEmpty() || h.contains(QLatin1Char('/')) || !h.contains(QLatin1Char('.')))
            continue;   // ce n'est pas un nom de domaine
        if (!out.contains(h)) out << h;
    }
    out.sort();
    return out;
}

QString joinHostList(const QStringList &hosts)
{
    QStringList clean;
    for (const QString &h : hosts) {
        const QString t = h.trimmed().toLower();
        if (!t.isEmpty() && !clean.contains(t)) clean << t;
    }
    clean.sort();
    return clean.join(QLatin1Char('|'));
}

QString serialize(const SettingsData &d)
{
    QString s;
    s += QStringLiteral("# DataSaver Browser — réglages (format clé=valeur, compatible Windows)\n");
    s += QStringLiteral("dataSaver=%1\n").arg(d.dataSaver ? 1 : 0);
    s += QStringLiteral("imagesOff=%1\n").arg(d.imagesOff ? 1 : 0);
    s += QStringLiteral("ultraEco=%1\n").arg(d.ultraEco ? 1 : 0);
    s += QStringLiteral("favicons=%1\n").arg(d.favicons ? 1 : 0);
    s += QStringLiteral("quality=%1\n").arg(qBound(kQualityMin, d.quality, kQualityMax));
    s += QStringLiteral("zoom=%1\n").arg(QString::number(qBound(kZoomMin, d.zoom, kZoomMax), 'f', 2));
    if (!d.engineId.isEmpty())       s += QStringLiteral("engine=%1\n").arg(d.engineId);
    if (!d.searxUrl.isEmpty())       s += QStringLiteral("searxUrl=%1\n").arg(d.searxUrl);
    if (!d.providerUrl.isEmpty())    s += QStringLiteral("providerUrl=%1\n").arg(d.providerUrl);
    s += QStringLiteral("autoFallback=%1\n").arg(d.autoFallback ? 1 : 0);
    s += QStringLiteral("remoteSuggest=%1\n").arg(d.remoteSuggestions ? 1 : 0);
    s += QStringLiteral("restoreSession=%1\n").arg(d.restoreSession ? 1 : 0);
    s += QStringLiteral("secGpc=%1\n").arg(d.secGpc ? 1 : 0);
    s += QStringLiteral("imageAllow=%1\n").arg(joinHostList(d.imageAllowedHosts));
    s += QStringLiteral("session=%1\n").arg(d.session.join(QLatin1Char('|')));
    return s;
}

SettingsData load(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return SettingsData();   // premier lancement : defauts
    const QString contents = QString::fromUtf8(f.readAll());
    f.close();
    return parse(contents);
}

bool save(const QString &path, const SettingsData &data)
{
    // QSaveFile ecrit dans un fichier temporaire puis renomme : une coupure
    // pendant l'ecriture laisse l'ancien settings.txt intact. Avec QFile, le
    // fichier etait tronque a l'ouverture : un crash au mauvais moment
    // reinitialisait tous les reglages.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << QStringLiteral("SettingsStore: écriture impossible :") << f.errorString();
        return false;
    }
    {
        QTextStream out(&f);
        out << serialize(data);
    }
    if (!f.commit()) {
        qWarning() << QStringLiteral("SettingsStore: commit impossible :") << f.errorString();
        return false;
    }
    return true;
}

} // namespace SettingsStore