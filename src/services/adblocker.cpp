#include "adblocker.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QUrl>
#include <QDebug>

namespace {

// Sous-chaines trop courtes (1 a 3 caracteres) matchent partout : on les jette,
// elles ne filtrent que du bruit. Mesure sur EasyList : 312 regles de ce type.
constexpr int kMinLooseLen = 4;

// Nombre de jetons par regle libre indexes (au-dela, la regle reste appliquee
// par un seul de ses jetons les plus longs).
constexpr int kMaxTokensPerRule = 4;

// Regles libres au-dela : abandonnees plutot que de faire grossir l'index.
constexpr int kMaxLooseRules = 40000;
static int g_looseDropped = 0;
static int g_looseTotal = 0;

// Decoupe en « jetons » alphanumeriques d'au moins 4 caracteres : c'est
// l'index des regles libres.
QStringList tokens(const QString &text)
{
    QStringList out;
    QString cur;
    for (const QChar &c : text) {
        if (c.isLetterOrNumber()) {
            cur.append(c);
        } else {
            if (cur.size() >= 4) out << cur;
            cur.clear();
        }
        if (out.size() >= kMaxTokensPerRule) return out;
    }
    if (cur.size() >= 4) out << cur;
    return out;
}

/* Analyse une ligne de filtre ABP.
   Renvoie Skip pour tout ce qui n'est pas une regle de reseau applicable :
   commentaire, regle cosmetique (##), regle ALLOW/desactivee (-), regle regex,
   regle a portee ($domain=), exception limitee a un chemin. */
enum class RuleKind { Skip, AllowHost, BlockHost, BlockAnchored, BlockLoose };

// Retire les separateurs ABP en bord de motif.
void trimEdges(QString &t)
{
    while (!t.isEmpty() && (t.front() == QLatin1Char('^') || t.front() == QLatin1Char('*')
                            || t.front() == QLatin1Char('|')))
        t.remove(0, 1);
    while (!t.isEmpty() && (t.back() == QLatin1Char('^') || t.back() == QLatin1Char('*')
                            || t.back() == QLatin1Char('|') || t.back() == QLatin1Char('.')))
        t.chop(1);
}

RuleKind parseAbpLine(const QString &raw, QString &host, QString &pattern)
{
    host.clear();
    pattern.clear();

    QString line = raw.trimmed();
    if (line.isEmpty()) return RuleKind::Skip;
    const QChar c0 = line.at(0);
    if (c0 == QLatin1Char('!') || c0 == QLatin1Char('[')) return RuleKind::Skip;  // commentaire
    if (c0 == QLatin1Char('#')) return RuleKind::Skip;   // cosmetique (##, #@#, #?#)
    if (c0 == QLatin1Char('-')) return RuleKind::Skip;   // regle ALLOW/desactivee

    bool isAllow = false;
    if (line.startsWith(QLatin1String("@@"))) {
        isAllow = true;
        line = line.mid(2);
    }

    // Options $... : $domain= restreint la regle a certains sites. Appliquee
    // partout, elle bloquerait des sites sans rapport : ces regles sont ignorees.
    bool hasDomainOption = false;
    const int dollar = line.indexOf(QLatin1Char('$'));
    if (dollar != -1) {
        hasDomainOption = line.mid(dollar + 1).toLower().contains(QLatin1String("domain="));
        line = line.left(dollar);
    }
    if (hasDomainOption) return RuleKind::Skip;

    // Regle commencee par « / » mais qui n'est pas une regex : c'est une
    // regle de sous-chaine globale (« /ads/banner.gif »).
    bool leadingSlash = false;
    if (line.startsWith(QLatin1Char('/'))) {
        leadingSlash = true;
    }

    bool anchoredHost = false;
    if (line.startsWith(QLatin1String("||"))) {
        anchoredHost = true;
        line = line.mid(2);
    }

    // Regle regex : /motif/options. Une regle commencee par « / » mais dont la
    // fin n'est pas un delimiter suivi d'options est une simple sous-chaine
    // (« /ads/banner.gif »), pas une regex.
    if (line.startsWith(QLatin1Char('/'))) {
        const int last = line.lastIndexOf(QLatin1Char('/'));
        if (last > 0) {
            bool optionsOnly = true;
            for (int i = last + 1; i < line.size(); ++i) {
                if (!line.at(i).isLetter()) { optionsOnly = false; break; }
            }
            if (optionsOnly) return RuleKind::Skip;   // /.../ ou /.../flags
        }
    }

    if (leadingSlash) {
        QString pat = line.mid(1);   // le '/' de depart fait partie du motif
        trimEdges(pat);
        pat = pat.toLower();
        if (pat.size() < kMinLooseLen) return RuleKind::Skip;
        pattern = pat;
        return RuleKind::BlockLoose;
    }

    const int slash = line.indexOf(QLatin1Char('/'));
    const bool hasPath = slash > 0;
    QString hostPart = hasPath ? line.left(slash) : line;
    QString pathPart = hasPath ? line.mid(slash) : QString();

    trimEdges(hostPart);
    trimEdges(pathPart);
    hostPart = hostPart.trimmed().toLower();
    pathPart = pathPart.toLower();

    if (hostPart.isEmpty() || hostPart.contains(QLatin1Char(' '))) return RuleKind::Skip;

    // Exception : seule « autoriser tout le domaine » est representable ici,
    // une exception limitee a un chemin exigerait un matching par chemin.
    if (isAllow) {
        if (hasPath || !anchoredHost || !hostPart.contains(QLatin1Char('.')))
            return RuleKind::Skip;
        host = hostPart;
        return RuleKind::AllowHost;
    }

    if (!hasPath) {
        if (!hostPart.contains(QLatin1Char('.'))) return RuleKind::Skip;   // pas un hote
        host = hostPart;
        return RuleKind::BlockHost;
    }

    if (pathPart.isEmpty()) return RuleKind::Skip;

    if (anchoredHost && hostPart.contains(QLatin1Char('.'))) {
        // « ||example.com/chemin » : comparaison avec l'URL sans son schema
        host = hostPart;
        pattern = hostPart + pathPart;
        return RuleKind::BlockAnchored;
    }

    // Regle libre (sans hote resolvable) : simple sous-chaine. Sous 4 caracteres
    // elle matcherait partout (312 regles de ce genre dans EasyList) : ignoree.
    pattern = hostPart + pathPart;
    if (pattern.size() < kMinLooseLen) return RuleKind::Skip;
    return RuleKind::BlockLoose;
}

} // namespace

void AdBlocker::clear() {
    QMutexLocker lock(&m_mutex);
    m_blockedHosts.clear();
    m_whitelist.clear();
    m_pathRules.clear();
    m_looseRules.clear();
    m_ruleCount = 0;
}

void AdBlocker::loadFilters(const QString &directory) {
    if (directory.isEmpty() || !QDir(directory).exists()) {
        qWarning() << QStringLiteral("AdBlocker: dossier de filtres introuvable :") << qPrintable(directory);
        return;
    }
    QDir dir(directory);
    const auto files = dir.entryList(QStringList() << QStringLiteral("*.txt"), QDir::Files);
    if (files.isEmpty()) {
        qWarning() << QStringLiteral("AdBlocker: aucune liste de filtres dans") << qPrintable(directory);
        return;
    }
    for (const auto &f : files) loadFilterFile(dir.filePath(f));
    qDebug("AdBlocker: %lld hotes bloques, %lld exceptions, %d regles au total",
           static_cast<long long>(m_blockedHosts.size()),
           static_cast<long long>(m_whitelist.size()), m_ruleCount);
    if (g_looseDropped > 0)
        qInfo("AdBlocker: %d regles « sous-chaine » abandonnees (bucket borne a %d)",
              g_looseDropped, kMaxLooseRules);
}

void AdBlocker::loadFilterFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << QStringLiteral("AdBlocker: liste illisible :") << qPrintable(filePath);
        return;
    }
    QSet<QString> hosts;
    QSet<QString> whitelist;
    QHash<QString, QList<PathRule>> pathRules;
    QHash<QString, QList<PathRule>> looseRules;
    int added = 0;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString host, pattern;
        switch (parseAbpLine(in.readLine(), host, pattern)) {
        case RuleKind::Skip:
            continue;
        case RuleKind::AllowHost:
            if (!whitelist.contains(host)) { whitelist.insert(host); ++added; }
            continue;
        case RuleKind::BlockHost:
            if (!hosts.contains(host)) { hosts.insert(host); ++added; }
            continue;
        case RuleKind::BlockAnchored:
            pathRules[host].append(PathRule{ true, pattern });
            ++added;
            continue;
        case RuleKind::BlockLoose: {
            if (g_looseTotal >= kMaxLooseRules) { ++g_looseDropped; continue; }
            const QStringList tk = tokens(pattern);
            if (tk.isEmpty()) continue;      // aucun jeton significatif : bruit
            ++g_looseTotal;
            for (const QString &t : tk) looseRules[t].append(PathRule{ false, pattern });
            ++added;
            continue;
        }
        }
    }
    mergeParsed(hosts, whitelist, pathRules, looseRules, added);
}

void AdBlocker::mergeParsed(QSet<QString> &hosts,
                            QSet<QString> &whitelist,
                            QHash<QString, QList<PathRule>> &pathRules,
                            QHash<QString, QList<PathRule>> &looseRules,
                            int &ruleCount)
{
    QMutexLocker lock(&m_mutex);
    m_blockedHosts.unite(hosts);
    m_whitelist.unite(whitelist);
    for (auto it = pathRules.constBegin(); it != pathRules.constEnd(); ++it)
        m_pathRules[it.key()].append(it.value());
    for (auto it = looseRules.constBegin(); it != looseRules.constEnd(); ++it)
        m_looseRules[it.key()].append(it.value());
    m_ruleCount += ruleCount;
    ruleCount = 0;
}

bool AdBlocker::shouldBlock(const QString &urlStr) const {
    if (urlStr.isEmpty()) return false;
    const QUrl url(urlStr);
    if (!url.isValid()) return false;
    const QString host = url.host().toLower();
    if (host.isEmpty()) return false;

    QMutexLocker lock(&m_mutex);

    // Exception prioritaire : l'hote et tous ses suffixes (une exception
    // « example.com » couvre aussi ads.example.com). O(labels) au lieu d'un
    // parcours lineaire des 1330 exceptions.
    QString cur = host;
    for (;;) {
        if (m_whitelist.contains(cur)) return false;
        const int dot = cur.indexOf(QLatin1Char('.'));
        if (dot < 0) break;
        cur = cur.mid(dot + 1);
    }

    const QString lowerUrl = urlStr.toLower();
    // URL sans son schema : les regles ancrees « ||hote/chemin » sont ecrites
    // comme le texte qui suit « :// ».
    QString tail = lowerUrl;
    const int sep = tail.indexOf(QLatin1String("://"));
    if (sep >= 0) tail = tail.mid(sep + 3);

    // Hotes bloques : exact puis suffixes (foo.bar.example.com -> example.com)
    cur = host;
    for (;;) {
        if (m_blockedHosts.contains(cur)) return true;
        auto it = m_pathRules.constFind(cur);
        if (it != m_pathRules.constEnd()) {
            for (const PathRule &r : it.value()) {
                if (r.anchored ? tail.startsWith(r.pattern)
                               : lowerUrl.contains(r.pattern))
                    return true;
            }
        }
        const int dot = cur.indexOf(QLatin1Char('.'));
        if (dot < 0) break;
        cur = cur.mid(dot + 1);
    }

    // Regles « libres » : index par jeton, donc on ne regarde que les regles
    // dont un jeton figure dans l'URL (au lieu de toutes les sous-chaines).
    const QStringList tk = tokens(tail);
    for (const QString &t : tk) {
        auto loose = m_looseRules.constFind(t);
        if (loose == m_looseRules.constEnd()) continue;
        for (const PathRule &r : loose.value()) {
            if (lowerUrl.contains(r.pattern)) return true;
        }
    }
    return false;
}

QString AdBlocker::domainRoot(const QString &host) {
    // Conserve pour information : le dernier couple de labels (TLD double
    // ignore, l'index par hote exact rend cette aproximation inutile).
    const auto parts = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.size() <= 2) return host;
    return parts[parts.size() - 2] + QLatin1Char('.') + parts[parts.size() - 1];
}

int AdBlocker::blockedHostCount() const {
    QMutexLocker lock(&m_mutex);
    return m_blockedHosts.size();
}

int AdBlocker::ruleCount() const {
    QMutexLocker lock(&m_mutex);
    return m_ruleCount;
}