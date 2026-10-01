#include "imageoptimizer.h"
#include <QImage>
#include <QBuffer>
#include <QDebug>

ImageOptimizer::ImageOptimizer(int minBytes, int maxWidth, int quality)
    : m_minBytes(minBytes), m_maxWidth(qBound(320, maxWidth, 900)), m_quality(qBound(20, quality, 85))
{}

void ImageOptimizer::setMaxWidth(int w) { m_maxWidth = qBound(320, w, 900); }
void ImageOptimizer::setQuality(int q) { m_quality = qBound(20, q, 85); }

void ImageOptimizer::applyPreset(bool ultraEco) {
    if (ultraEco) { m_maxWidth = 480; m_quality = 50; }
    else { m_maxWidth = 600; m_quality = 65; }
}

QByteArray ImageOptimizer::optimize(const QByteArray &input, const QString &contentType) const {
    Q_UNUSED(contentType)
    if (input.size() < m_minBytes) return QByteArray();

    QImage img;
    if (!img.loadFromData(input)) {
        return QByteArray();
    }
    if (img.width() > m_maxWidth) {
        double ratio = double(m_maxWidth) / img.width();
        int newH = int(img.height() * ratio);
        img = img.scaled(m_maxWidth, newH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QByteArray webp;
    QBuffer buf(&webp);
    buf.open(QIODevice::WriteOnly);
    // Qt 6.8 supporte WebP via plugin imageformats si disponible
    bool ok = img.save(&buf, "WEBP", m_quality);
    if (!ok) {
        // fallback JPEG si WebP non dispo
        webp.clear();
        QBuffer buf2(&webp);
        buf2.open(QIODevice::WriteOnly);
        ok = img.save(&buf2, "JPEG", m_quality);
        if (!ok) return QByteArray();
    }
    if (webp.size() >= input.size() || webp.isEmpty()) return QByteArray();
    return webp;
}
