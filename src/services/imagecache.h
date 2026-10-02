#pragma once
#include <QByteArray>
#include <QCache>
#include <QMutex>
#include <QString>

/*
 * ImageCache — cache mémoire des images DÉJÀ compressées.
 *
 * Pourquoi ne pas réutiliser CacheManager : celui-ci est un cache SQLite sur
 * disque, prévu pour des ressources statiques, et il n'est appelé par personne.
 * Ici il faut un cache mémoire,LRU, accessible depuis le gestionnaire
 * d'images qui répond sur le thread réseau.
 *
 * Deux règles :
 *  1. la clé inclut la transformation (largeur, qualité, format) : une même
 *     URL servie en 800 px et en 1600 px ne doit pas seReturning l'une l'autre ;
 *  2. le coût est en OCTETS (pas en entrées) : une photo à 1,5 Mo ne doit pas
 *     occuper la même place qu'une vignette de 8 Ko.
 *
 * QCache fait déjà l'éviction LRU (le plus récemment utilisé est celui qui
 * sort en premier) ; il reste à le protéger par un mutex, car le gestionnaire
 * d'images travaille sur le thread réseau pendant que l'IHM lit les compteurs.
 */
class ImageCache {
public:
    struct Entry {
        QByteArray data;
        QByteArray contentType;
    };

    explicit ImageCache(qint64 budgetBytes = 32LL * 1024 * 1024);

    // Clé canonique : URL + transformation. À utiliser partout, pour ne pas
    // avoir deux endroits qui construisent la clé différemment.
    static QString keyFor(const QString &url, int targetWidth, int quality, const QByteArray &format);

    bool get(const QString &key, Entry &out);
    void put(const QString &key, const QByteArray &data, const QByteArray &contentType);
    void clear();

    /* Ajuste le budget APRES construction (curseur de l'IHM). Baisser le budget
     * evict immediatement les entrées les moins récemment utilisées si le coût
     * total dépasse la nouvelle limite : c'est le comportement voulu. */
    void setBudget(qint64 budgetBytes);

    qint64 budget() const { return m_budget; }
    qint64 usedBytes() const;
    int count() const;
    qint64 hits() const;
    qint64 misses() const;
    // Ce que le cache a réellement évité au réseau (octets non retéléchargés).
    qint64 savedBytes() const;

private:
    // QCache n'est pas thread-safe : tout accès passe par m_mutex.
    QCache<QString, Entry> m_cache;
    mutable QMutex m_mutex;
    qint64 m_budget;
    qint64 m_hits = 0;
    qint64 m_misses = 0;
    qint64 m_savedBytes = 0;
};