#pragma once
#include <QString>
#include <QByteArray>
#include <QSqlDatabase>
#include <QMutex>
#include <QDateTime>

/*
 * Port de Services/CacheManager.cs
 * Cache SQLite WAL 100MB pour ressources statiques
 * Niveau 1 : stockage seul (pas de scheme handler), stats + eviction LRU
 * Niveau 2 (futur) : QWebEngineUrlSchemeHandler pour servir depuis cache
 */
class CacheManager {
public:
    struct Entry {
        QByteArray data;
        QString contentType = "application/octet-stream";
        qint64 createdAt = 0;
    };

    explicit CacheManager(const QString &dbPath = QString(), qint64 maxBytes = 100 * 1024 * 1024);
    ~CacheManager();

    bool tryGet(const QString &url, Entry &out);
    void put(const QString &url, const QByteArray &data, const QString &contentType);
    void clear();
    qint64 currentBytes() const;   // verrouille en interne
    qint64 maxBytes() const { return m_maxBytes; }
    int count() const;

    static QString defaultDbPath();

private:
    void ensureTable();
    void evictIfNeeded();
    void updateCurrentBytes();

    QSqlDatabase m_db;
    QString m_connectionName;
    qint64 m_maxBytes;
    qint64 m_currentBytes = 0;
    mutable QMutex m_mutex;
};
