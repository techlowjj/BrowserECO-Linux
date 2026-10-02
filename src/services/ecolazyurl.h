#pragma once
#include <QUrl>

/*
 * EcoLazyUrl — le marqueur « l'utilisateur a cliqué » sur une image.
 *
 * Le chargement d'images a la demande bloque les images au niveau reseau (voir
 * EcoInterceptor). Pour en liberer une, le script DOM n'a aucun moyen de
 * prevenir le navigateur autrement qu'en changeant l'URL : il y ajoute un
 * marqueur. L'intercepteur le retire ensuite, et memorise l'URL propre comme
 * « voulue » — sans cela, recharger la page rebloquerait des images que
 * l'utilisateur avait explicitement demandees.
 *
 * Le marqueur est un parametre de requete factice (`_eco=1`) : il ne fait pas
 * partie de l'adresse reelle. L'intercepteur le supprime AVANT toute requete
 * sortante, donc le CDN d'origine ne le voit jamais.
 */
namespace EcoLazyUrl {

inline constexpr auto markerKey = "_eco";

// Retire le marqueur si present. `propre` reçoit alors l'URL sans marqueur.
// Renvoie true si un marqueur a ete trouve (et donc retiré).
bool removeMarker(const QUrl &url, QUrl *propre);

// URL marquee, telle que le script doit la poser sur src.
QUrl mark(const QUrl &url);

} // namespace EcoLazyUrl