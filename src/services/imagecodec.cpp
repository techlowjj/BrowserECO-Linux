#include "imagecodec.h"
#include <QBuffer>
#include <QImageWriter>

namespace ImageCodec {

QString verdictToString(Verdict v)
{
    switch (v) {
    case Verdict::Compressed:       return QStringLiteral("compressé");
    case Verdict::TooSmall:         return QStringLiteral("sous le seuil");
    case Verdict::NotAnImage:       return QStringLiteral("pas une image");
    case Verdict::PasRecode:        return QStringLiteral("format non réencodable");
    case Verdict::QualityDisabled:  return QStringLiteral("qualité 0");
    case Verdict::NotSmaller:       return QStringLiteral("pas plus léger");
    case Verdict::EncodeFailed:     return QStringLiteral("encodage impossible");
    }
    return QStringLiteral("inconnu");
}

int clampQuality(int q) { return q < kMinQuality ? kMinQuality : (q > kMaxQuality ? kMaxQuality : q); }

int clampWidth(int w)
{
    // 0 = pas de redimensionnement ; sinon borné à [1, kMaxWidth].
    if (w <= 0) return 0;
    return w > kMaxWidth ? kMaxWidth : w;
}

bool webpAvailable()
{
    // Détecté une seule fois : cette fonction est appelée à chaque image.
    static const bool dispo = []{
        const QList<QByteArray> f = QImageWriter::supportedImageFormats();
        return f.contains(QByteArrayLiteral("webp")) || f.contains(QByteArrayLiteral("WEBP"));
    }();
    return dispo;
}

QByteArray preferredFormat()
{
    return webpAvailable() ? QByteArrayLiteral("WEBP") : QByteArrayLiteral("JPEG");
}

QByteArray contentTypeForFormat(QByteArray format)
{
    return format.toUpper() == QByteArrayLiteral("WEBP") ? QByteArrayLiteral("image/webp")
                                                       : QByteArrayLiteral("image/jpeg");
}

Result compress(const Request &req, const QByteArray &forcedFormat)
{
    Result r;
    r.originalSize = req.data.size();

    if (req.data.isEmpty() || r.originalSize == 0) return r;   // NotAnImage
    if (!req.contentType.trimmed().startsWith(QStringLiteral("image/"), Qt::CaseInsensitive))
        return r;
    const QString ct = req.contentType.toLower();
    // GIF animé : QImage n'écrit pas de WebP animé → on ne touche pas à ce qui
    // pourrait être une animation. SVG : rastérisation = perte sèche.
    // Ce refus est une POLITIQUE de format, pas une question de taille : il est
    // donc évalué avant le seuil, sinon un petit GIF serait rapporté « trop
    // petit » alors que la raison réelle est « jamais réencodé ».
    if (ct.contains(QStringLiteral("gif")) || ct.contains(QStringLiteral("svg"))) {
        r.verdict = Verdict::PasRecode;
        return r;
    }

    // Un seuil négatif ne doit pas contourner la garde : il vaut 0.
    const qint64 minBytes = req.minBytes < 0 ? 0 : req.minBytes;
    if (r.originalSize < minBytes) { r.verdict = Verdict::TooSmall; return r; }
    const int quality = clampQuality(req.quality);
    if (quality == 0) { r.verdict = Verdict::QualityDisabled; return r; }

    QImage img;
    if (!img.loadFromData(req.data, QByteArray())) return r;   // NotAnImage
    r.originalWidth = img.width();
    r.originalHeight = img.height();
    if (img.isNull()) return r;

    // Redimensionnement : jamais d'agrandissement, et jamais au-delà du plafond.
    const int target = clampWidth(req.targetWidth);
    if (target > 0 && img.width() > target) {
        const int h = qMax(1, int(qint64(img.height()) * target / img.width()));
        img = img.scaled(target, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    r.width = img.width();
    r.height = img.height();

    const QByteArray format = forcedFormat.isEmpty() ? preferredFormat() : forcedFormat.toUpper();
    QByteArray out;
    QByteArray mime;
    // Deux essais : le format préféré, puis JPEG en repli. Un seul échec est
    // silencieux dans l'ancien code, ce qui rendait le diagnostic impossible.
    const QList<QByteArray> essais = (format == QByteArrayLiteral("WEBP"))
        ? QList<QByteArray>{ QByteArrayLiteral("WEBP"), QByteArrayLiteral("JPEG") }
        : QList<QByteArray>{ format, QByteArrayLiteral("JPEG") };
    for (const QByteArray &f : essais) {
        out.clear();
        QBuffer buf(&out);
        if (!buf.open(QIODevice::WriteOnly)) continue;
        if (!img.save(&buf, f.constData(), quality)) continue;
        buf.close();
        if (!out.isEmpty()) { mime = contentTypeForFormat(f); break; }
    }
    if (mime.isEmpty()) { r.verdict = Verdict::EncodeFailed; return r; }

    // Règle cardinale : jamais de dégradation.
    if (out.size() >= r.originalSize) { r.verdict = Verdict::NotSmaller; return r; }

    r.data = out;
    r.contentType = mime;
    r.verdict = Verdict::Compressed;
    return r;
}

} // namespace ImageCodec