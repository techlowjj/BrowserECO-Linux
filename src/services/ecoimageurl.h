#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>

/*
 * EcoImageUrl — URL de notre serveur d'images, signée.
 *
 * L'intercepteur remplace l'URL de chaque image par :
 *     http://127.0.0.1:<port>/i?u=<base64url(original)>&s=<signature>&w=<largeur>&q=<qualité>
 * et notre serveur local (EcoImageServer) sert l'image compressée.
 *
 * POURQUOI UN SERVEUR LOCAL ET NON UN SCHÉMA PRIVÉ
 * Un schéma personnalisé (ecoimg://) marche très bien quand il est écrit dans
 * le HTML, mais Qt 6.8 n'honore PAS la redirection d'un intercepteur vers un
 * schéma personnalisé pour une sous-ressource : la requête originale est
 * annulée et rien n'est demandé (mesuré : gestionnaire jamais appelé, serveur
 * d'origine jamais contacté). Le serveur local, lui, est une URL http
 * ordinaire : la redirection fonctionne, et Chromium exempte explicitement
 * 127.0.0.1 du blocage « mixed content ».
 *
 * RISQUE À SURVEILLER : depuis Chrome 142, les requêtes vers la boucle locale
 * sont soumises à une permission (« loopback-network »). Le Qt 6.8 embarqué
 * n'est pas concerné ; si le projet passe à un Chromium plus récent, il faudra
 * soit demander cette permission, soit revenir au schéma privé.
 *
 * POURQUOI UNE SIGNATURE
 * L'URL de notre serveur est une vraie URL http://127.0.0.1 : une page pourrait
 * elle-même l'appeler (SSRF vers le réseau local, ou lecture de la réponse si
 * elle lisait les octets). Chaque URL générée par l'application est donc signée
 * avec une clé aléatoire créée au démarrage du processus : une page ne peut
 * obtenir une signature que pour une URL qu'elle a déjà vue passer, et donc
 * rejouer l'identique — pas demander « http://127.0.0.1:8080/admin/secret ».
 * La validation http(s) de la cible est la seconde barrière.
 */
class EcoImageUrl {
public:
    static constexpr auto path = "/i";

    struct Target {
        QUrl original;        // URL d'origine à récupérer
        int targetWidth = 0;  // largeur cible (0 = taille d'origine)
        int quality = 0;      // qualité 0-85
        bool valid = false;   // false si signature invalide ou cible non http(s)
    };

    // `base` = racine de notre serveur local (http://127.0.0.1:port).
    static QUrl encode(const QUrl &base, const QUrl &original, int targetWidth, int quality);
    static Target decode(const QUrl &requestUrl, const QUrl &base);

    // Exposé pour les tests : la signature est une fonction pure du contenu.
    static QByteArray sign(const QByteArray &payload, int width, int quality);
};