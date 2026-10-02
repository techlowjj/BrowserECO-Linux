#include "imagecache.h"

ImageCache::ImageCache(qint64 budgetBytes)
    : m_cache(static_cast<int>(budgetBytes < 1024 ? 1024 : budgetBytes)),
      m_budget(budgetBytes < 1024 ? 1024 : budgetBytes)
{}

QString ImageCache::keyFor(const QString &url, int targetWidth, int quality, const QByteArray &format)
{
    // La transformation fait partie de la clé : servir 800 px puis 1600 px pour
    // la même URL doit produire deux entrées, sinon la seconde renverrait la
    // première (image trop petite ou trop floue).
    return QStringLiteral("%1|%2|%3|%4")
        .arg(url, targetWidth)
        .arg(quality)
        .arg(QString::fromLatin1(format.isEmpty() ? QByteArrayLiteral("AUTO") : format));
}

bool ImageCache::get(const QString &key, Entry &out)
{
    QMutexLocker lock(&m_mutex);
    if (const Entry *e = m_cache.object(key)) {
        out = *e;
        ++m_hits;
        m_savedBytes += out.data.size();   // octets NON retéléchargés
        return true;
    }
    ++m_misses;
    return false;
}

void ImageCache::put(const QString &key, const QByteArray &data, const QByteArray &contentType)
{
    if (data.isEmpty()) return;
    QMutexLocker lock(&m_mutex);
    // QCache stocke des pointeurs et possède les objets : une allocation par
    // ENTRÉE (donc par absence de cache), jamais par requête servie.
    // Le coût DOIT être la taille en octets : par défaut QCache compte 1 par
    // entrée, et le budget en octets deviendrait un nombre d'entrées.
    auto *e = new Entry;
    e->data = data;
    e->contentType = contentType;
    m_cache.insert(key, e, qsizetype(e->data.size()));
}

void ImageCache::setBudget(qint64 budgetBytes)
{
    QMutexLocker lock(&m_mutex);
    const qint64 borne = budgetBytes < 1024 ? 1024 : budgetBytes;
    m_cache.setMaxCost(static_cast<int>(borne));
    m_budget = borne;
}

void ImageCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
}

qint64 ImageCache::usedBytes() const
{
    QMutexLocker lock(&m_mutex);
    return m_cache.totalCost();
}

int ImageCache::count() const
{
    QMutexLocker lock(&m_mutex);
    return m_cache.count();
}

qint64 ImageCache::hits() const
{
    QMutexLocker lock(&m_mutex);
    return m_hits;
}

qint64 ImageCache::misses() const
{
    QMutexLocker lock(&m_mutex);
    return m_misses;
}

qint64 ImageCache::savedBytes() const
{
    QMutexLocker lock(&m_mutex);
    return m_savedBytes;
}