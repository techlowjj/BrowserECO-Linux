#include "cachemanager.h"
#include <QCoreApplication>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QUuid>

QString CacheManager::defaultDbPath() {
    // Portable d'abord : à côté du binaire, sinon LocalApplicationData
    QString appDir = QCoreApplication::applicationDirPath();
    QString portable = appDir + "/cache.db";
    // Si appDir writable, on l'utilise (portable)
    QFileInfo fi(portable);
    QDir d = fi.dir();
    if (d.exists()) {
        // test writable
        QString test = d.filePath(".writetest");
        QFile f(test);
        if (f.open(QIODevice::WriteOnly)) { f.remove(); return portable; }
    }
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDir.isEmpty()) dataDir = QDir::homePath() + "/.cache/BrowserECO";
    QDir().mkpath(dataDir);
    return dataDir + "/cache.db";
}

CacheManager::CacheManager(const QString &dbPath, qint64 maxBytes)
    : m_maxBytes(maxBytes)
{
    QString path = dbPath.isEmpty() ? defaultDbPath() : dbPath;
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    m_connectionName = QString("cache_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        qWarning() << "CacheManager: cannot open" << path << m_db.lastError().text();
        return;
    }
    // WAL + perf
    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL;");
    q.exec("PRAGMA synchronous=NORMAL;");
    q.exec("PRAGMA busy_timeout=5000;");
    // Le fichier -wal resterait sinon grossi indefiniment (voir HistoryManager).
    q.exec("PRAGMA journal_size_limit=1048576;");
    ensureTable();
    updateCurrentBytes();
    qDebug() << "CacheManager: db" << path << "bytes" << m_currentBytes << "/" << m_maxBytes;
}

CacheManager::~CacheManager() {
    if (m_db.isOpen()) {
        QSqlQuery q(m_db);
        q.exec("PRAGMA wal_checkpoint(TRUNCATE);");
        m_db.close();
    }
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

void CacheManager::ensureTable() {
    QSqlQuery q(m_db);
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS cache (
            url TEXT PRIMARY KEY,
            data BLOB NOT NULL,
            content_type TEXT NOT NULL,
            created_at INTEGER NOT NULL
        );
    )");
    q.exec("CREATE INDEX IF NOT EXISTS idx_cache_created ON cache(created_at);");
}

void CacheManager::updateCurrentBytes() {
    QSqlQuery q(m_db);
    if (q.exec("SELECT IFNULL(SUM(LENGTH(data)),0) FROM cache;") && q.next()) {
        m_currentBytes = q.value(0).toLongLong();
    }
}

int CacheManager::count() const {
    QMutexLocker lock(&m_mutex);
    if (!m_db.isOpen()) return 0;
    QSqlQuery q(m_db);
    if (q.exec("SELECT COUNT(*) FROM cache;") && q.next()) return q.value(0).toInt();
    return 0;
}

bool CacheManager::tryGet(const QString &url, Entry &out) {
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("SELECT data, content_type, created_at FROM cache WHERE url = ?;");
    q.addBindValue(url);
    if (!q.exec()) return false;
    if (q.next()) {
        out.data = q.value(0).toByteArray();
        out.contentType = q.value(1).toString();
        out.createdAt = q.value(2).toLongLong();
        return true;
    }
    return false;
}

void CacheManager::put(const QString &url, const QByteArray &data, const QString &contentType) {
    if (data.isEmpty() || url.isEmpty()) return;
    // ne cache que ressources statiques raisonnables < 5MB
    if (data.size() > 5 * 1024 * 1024) return;

    QMutexLocker lock(&m_mutex);
    if (!m_db.isOpen()) return;

    // Taille de l'entree deja presente : INSERT OR REPLACE ecrase l'ancienne
    // ligne, et ajouter data.size() au compteur sans retirer l'ancien volume
    // faisait grossir indefiniment le total (et donc declencher des
    // evacuations de donnees encore valides).
    qint64 previous = 0;
    {
        QSqlQuery old(m_db);
        old.prepare("SELECT LENGTH(data) FROM cache WHERE url = ?;");
        old.addBindValue(url);
        if (old.exec() && old.next()) previous = old.value(0).toLongLong();
    }

    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO cache(url, data, content_type, created_at) VALUES(?,?,?,?);");
    q.addBindValue(url);
    q.addBindValue(data);
    q.addBindValue(contentType.isEmpty() ? "application/octet-stream" : contentType);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!q.exec()) {
        qWarning() << "CacheManager put failed" << q.lastError().text();
        m_db.rollback();
        return;
    }
    if (!m_db.commit()) {
        qWarning() << "CacheManager: commit impossible" << m_db.lastError().text();
        return;
    }
    m_currentBytes += data.size() - previous;
    evictIfNeeded();   // appele SOUS le verrou : ne doit pas le reprendre
}

/* ATTENTION : appelee avec m_mutex deja pris (put()) ; ne pas le reprendre,
   QMutex n'est pas recursif. */
qint64 CacheManager::currentBytes() const {
    QMutexLocker lock(&m_mutex);
    return m_currentBytes;
}

void CacheManager::evictIfNeeded() {
    if (m_currentBytes <= m_maxBytes) return;
    // LRU : supprime les plus vieux jusqu'a < 80% max
    const qint64 target = m_maxBytes * 8 / 10;
    QSqlQuery q(m_db);
    if (!q.exec("SELECT url, LENGTH(data), created_at FROM cache ORDER BY created_at ASC;")) return;

    // Les DELETE sont dans UNE transaction avec une requete preparee une seule
    // fois : en autocommit, vider 20 Mo de cache prenait des dizaines de
    // milliers de transactions separees (un fsync chacune).
    QSqlQuery del(m_db);
    del.prepare("DELETE FROM cache WHERE url = ?;");
    m_db.transaction();
    while (m_currentBytes > target && q.next()) {
        del.addBindValue(q.value(0).toString());
        if (!del.exec()) break;
        m_currentBytes -= q.value(1).toLongLong();
    }
    m_db.commit();
}

void CacheManager::clear() {
    QMutexLocker lock(&m_mutex);
    if (!m_db.isOpen()) return;
    QSqlQuery q(m_db);
    m_db.transaction();
    q.exec("DELETE FROM cache;");
    m_db.commit();
    m_currentBytes = 0;
    // VACUUM hors transaction, et son echec n'annule pas l'effacement.
    if (!q.exec("VACUUM;"))
        qWarning() << "CacheManager: effacement ok, VACUUM impossible" << q.lastError().text();
    q.exec("PRAGMA wal_checkpoint(TRUNCATE);");
}
