#pragma once
#include <QByteArray>
#include <QString>

/*
 * Port de Services/ImageOptimizer.cs (SixLabors.ImageSharp -> QImage)
 * Optimisation WebP Niveau 1 : placeholder pour future scheme handler
 * Niveau 1 actuel : expose settings + stub optimize (QImage WebP)
 * Utilisé via CacheManager + futur QWebEngineUrlSchemeHandler
 */
class ImageOptimizer {
public:
    explicit ImageOptimizer(int minBytes = 10000, int maxWidth = 600, int quality = 65);

    void setMaxWidth(int w);
    void setQuality(int q);
    void applyPreset(bool ultraEco); // Standard 600/65 vs Ultra 480/50
    int maxWidth() const { return m_maxWidth; }
    int quality() const { return m_quality; }

    // Retourne WebP optimisé si plus léger, sinon QByteArray vide (conserver original)
    // Comme en C# : return null si webp >= original ou < minBytes
    QByteArray optimize(const QByteArray &input, const QString &contentType) const;

private:
    int m_minBytes;
    int m_maxWidth;
    int m_quality;
};
