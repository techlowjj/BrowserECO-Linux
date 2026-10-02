#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>

/*
 * ImageCodec — décision de compression d'une image, SANS réseau et SANS
 * navigateur (donc testable seule).
 *
 * Remplace Services/ImageOptimizer.{h,cpp}, qui n'était jamais appelé : le
 * curseur de qualité et le réglage `quality=` ne faisaient donc rien. La
 * logique est ici, avec une politique explicite et des reasons lisibles
 * (ils apparaissent dans les tests et serviront de trace au débogage).
 *
 * Deux règles cardinales :
 *  1. JAMAIS de dégradation : si le résultat weigh plus lourd que l'original,
 *     on renvoie « non compressé » et l'appelant sert l'original ;
 *  2. JAMAIS de perte d'information : GIF animé et SVG ne sont pas réencodés
 *     (QImage ne sait pas écrire de WebP animé, et rastériser un SVG serait
 *     une perte sèche).
 */
namespace ImageCodec {

// Largeurs : le plafond correspond au gain mesuré (une photo 1600x1067
// downscalée en 800 px perd 76 % d'octets). Le plafond évite de servir
// 4000 px de large alors que 1600 px suffisent à l'écran.
constexpr int kMaxWidth = 1600;
constexpr int kMinQuality = 0;
constexpr int kMaxQuality = 85;
constexpr qint64 kDefaultMinBytes = 10 * 1024;

enum class Verdict {
    Compressed,       // données compressées dans « data »
    TooSmall,         // sous le seuil : l'octet économisé ne vaut pas le CPU
    NotAnImage,       // content-type non image, ou indéchiffrable
    PasRecode,        // GIF animé / SVG : réencodage interdit
    QualityDisabled,  // qualité 0 = compression désactivée
    NotSmaller,       // le réencodage n'a pas été plus léger
    EncodeFailed      // aucun encodeur n'a réussi
};

QString verdictToString(Verdict v);

struct Request {
    QByteArray data;
    QString contentType;                 // tel que servi par le serveur d'origine
    int targetWidth = kMaxWidth;         // largeur maximale voulue
    int quality = 65;                    // 0-85 (0 = pas de compression)
    qint64 minBytes = kDefaultMinBytes;
};

struct Result {
    Verdict verdict = Verdict::NotAnImage;
    QByteArray data;                     // valide si verdict == Compressed
    QByteArray contentType;              // type MIME de « data »
    int originalWidth = 0;
    int originalHeight = 0;
    int width = 0;                       // dimensions de « data »
    int height = 0;
    bool compressed() const { return verdict == Verdict::Compressed; }
    // Vrai si l'original est réellement plus gros que ce qu'on sert : c'est la
    // seule métrique d'économie qui compte (invariant vérifié par les tests).
    bool saves() const { return compressed() && data.size() < originalSize; }
    qint64 originalSize = 0;
};

/* Format de sortie préféré : WEBP quand le plugin Qt est présent (gain ~20 %),
   JPEG sinon. Détecté une fois, mis en cache : `save()` qui échoue est 1000x
   plus lent à découvrir qu'un `supportedImageFormats()`. */
bool webpAvailable();
QByteArray preferredFormat();            // "WEBP" ou "JPEG"
QByteArray contentTypeForFormat(QByteArray format);

// Bornes centralisées (partagées avec l'IHM).
int clampQuality(int q);
int clampWidth(int w);

/* Fonction principale, sans état : toute la décision est dans les arguments.
   `forcedFormat` permet de fixer le format de sortie (tests) ; vide = préférence. */
Result compress(const Request &req, const QByteArray &forcedFormat = QByteArray());

} // namespace ImageCodec