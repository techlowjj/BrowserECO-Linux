#pragma once
#include <QString>
#include <QSqlDatabase>
#include <QMutex>
#include <QDateTime>
#include <QVector>

/*
 * Port de Services/HistoryRepository.cs
 * SQLite WAL pour historique navigation
 */
struct HistoryEntry {
    qint64 id = 0;
    QString url;
    QString title;
    qint64 visitedAt = 0; // secs since epoch
    int visitCount = 1;
};

class HistoryManager {
public:
    explicit HistoryManager(const QString &dbPath = QString());
    ~HistoryManager();

    void addVisit(const QString &url, const QString &title);
    QVector<HistoryEntry> recent(int limit = 100) const;
    QVector<HistoryEntry> search(const QString &term, int limit = 100) const;
    void remove(const QString &url);
    void clear();
    int count() const;

    static QString defaultDbPath();

private:
    void ensureTable();
    QSqlDatabase m_db;
    QString m_connectionName;
    mutable QMutex m_mutex;
};
