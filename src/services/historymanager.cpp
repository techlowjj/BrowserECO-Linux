#include "historymanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QUuid>
#include <QCoreApplication>

QString HistoryManager::defaultDbPath() {
    QString appDir = QCoreApplication::applicationDirPath();
    QString portable = appDir + "/history.db";
    QDir d = QFileInfo(portable).dir();
    QString test = d.filePath(".writetest_hist");
    QFile f(test);
    if (f.open(QIODevice::WriteOnly)) { f.remove(); return portable; }
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDir.isEmpty()) dataDir = QDir::homePath() + "/.cache/BrowserECO";
    QDir().mkpath(dataDir);
    return dataDir + "/history.db";
}

HistoryManager::HistoryManager(const QString &dbPath) {
    QString path = dbPath.isEmpty() ? defaultDbPath() : dbPath;
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());
    m_connectionName = QString("hist_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        qWarning() << "HistoryManager cannot open" << path << m_db.lastError().text();
        return;
    }
    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL;");
    q.exec("PRAGMA synchronous=NORMAL;");
    q.exec("PRAGMA busy_timeout=5000;");
    // Sans journal_size_limit, le fichier -wal reste definitivement grossi :
    // wal_autocheckpoint (1000 pages) le laisse juste au-dessus du seuil et
    // personne ne le tronque -> 4 Mo de WAL pour 300 lignes d'historique.
    q.exec("PRAGMA journal_size_limit=1048576;");
    ensureTable();
}

HistoryManager::~HistoryManager() {
    if (m_db.isOpen()) {
        // Recopie le contenu du WAL dans history.db puis le tronque : sans cela
        // le fichier -wal reste de plusieurs Mo sur le disque.
        {
            QSqlQuery q(m_db);
            if (!q.exec("PRAGMA wal_checkpoint(TRUNCATE);"))
                qWarning() << QStringLiteral("HistoryManager: checkpoint final impossible :") << q.lastError().text();
        }
        m_db.close();
    }
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

void HistoryManager::ensureTable() {
    QSqlQuery q(m_db);
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            url TEXT UNIQUE NOT NULL,
            title TEXT NOT NULL,
            visited_at INTEGER NOT NULL,
            visit_count INTEGER NOT NULL DEFAULT 1
        );
    )");
    q.exec("CREATE INDEX IF NOT EXISTS idx_hist_time ON history(visited_at DESC);");
    q.exec("CREATE INDEX IF NOT EXISTS idx_hist_url ON history(url);");
}

void HistoryManager::addVisit(const QString &url, const QString &title) {
    if (url.isEmpty() || url.startsWith("about:") || url.startsWith("data:")) return;
    QMutexLocker lock(&m_mutex);
    if (!m_db.isOpen()) return;
    QSqlQuery q(m_db);
    // Transaction explicite : en autocommit, chaque visite/url force un fsync
    // et fait grossir le WAL (une transaction par evenement, meme si une seule
    // instruction est executee).
    m_db.transaction();
    // upsert
    q.prepare(R"(
        INSERT INTO history(url, title, visited_at, visit_count)
        VALUES(?,?,?,1)
        ON CONFLICT(url) DO UPDATE SET title=excluded.title, visited_at=excluded.visited_at, visit_count=visit_count+1;
    )");
    q.addBindValue(url);
    q.addBindValue(title.isEmpty() ? url : title);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!q.exec()) {
        qWarning() << QStringLiteral("History addVisit failed:") << q.lastError().text();
        m_db.rollback();
        return;
    }
    if (!m_db.commit()) qWarning() << QStringLiteral("History addVisit: commit impossible :") << m_db.lastError().text();
}

QVector<HistoryEntry> HistoryManager::recent(int limit) const {
    QMutexLocker lock(&m_mutex);
    QVector<HistoryEntry> out;
    if (!m_db.isOpen()) return out;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, url, title, visited_at, visit_count FROM history ORDER BY visited_at DESC LIMIT ?;");
    q.addBindValue(limit);
    if (!q.exec()) return out;
    while (q.next()) {
        HistoryEntry e;
        e.id = q.value(0).toLongLong();
        e.url = q.value(1).toString();
        e.title = q.value(2).toString();
        e.visitedAt = q.value(3).toLongLong();
        e.visitCount = q.value(4).toInt();
        out.append(e);
    }
    return out;
}

QVector<HistoryEntry> HistoryManager::search(const QString &term, int limit) const {
    QMutexLocker lock(&m_mutex);
    QVector<HistoryEntry> out;
    if (term.isEmpty() || !m_db.isOpen()) return out;
    QSqlQuery q(m_db);
    // Les jokers LIKE (% et _) sont echappes : taper "%" dans l'omnibox ne doit pas
    // lister tout l'historique.
    q.prepare("SELECT id, url, title, visited_at, visit_count FROM history "
              "WHERE url LIKE ? ESCAPE '\\' OR title LIKE ? ESCAPE '\\' "
              "ORDER BY visited_at DESC LIMIT ?;");
    QString escaped = term;
    escaped.replace(QLatin1String("\\"), QLatin1String("\\\\"));
    escaped.replace(QLatin1String("%"), QLatin1String("\\%"));
    escaped.replace(QLatin1String("_"), QLatin1String("\\_"));
    const QString like = "%" + escaped + "%";
    q.addBindValue(like);
    q.addBindValue(like);
    q.addBindValue(limit);
    if (!q.exec()) return out;
    while (q.next()) {
        HistoryEntry e;
        e.id = q.value(0).toLongLong();
        e.url = q.value(1).toString();
        e.title = q.value(2).toString();
        e.visitedAt = q.value(3).toLongLong();
        e.visitCount = q.value(4).toInt();
        out.append(e);
    }
    return out;
}

void HistoryManager::remove(const QString &url) {
    QMutexLocker lock(&m_mutex);
    if (!m_db.isOpen()) return;
    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM history WHERE url = ?;");
    q.addBindValue(url);
    if (!q.exec()) { m_db.rollback(); return; }
    m_db.commit();
}

void HistoryManager::clear() {
    QMutexLocker lock(&m_mutex);
    if (!m_db.isOpen()) return;
    m_db.transaction();
    QSqlQuery q(m_db);
    q.exec("DELETE FROM history;");
    m_db.commit();
    // VACUUM est bloquant (il reecrit le fichier) et doit HORS transaction.
    // Son echec n'invalide pas l'effacement : on le signale sans mentir.
    if (!q.exec("VACUUM;"))
        qWarning() << QStringLiteral("HistoryManager: effacement ok, VACUUM impossible :") << q.lastError().text();
    q.exec("PRAGMA wal_checkpoint(TRUNCATE);");
}

int HistoryManager::count() const {
    QMutexLocker lock(&m_mutex);
    if (!m_db.isOpen()) return 0;
    QSqlQuery q(m_db);
    if (q.exec("SELECT COUNT(*) FROM history;") && q.next()) return q.value(0).toInt();
    return 0;
}
