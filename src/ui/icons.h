#pragma once
#include <QIcon>
#include <QColor>

/*
 * Icons — jeu d'icones vectorielles dessinees a la volee (QPainter).
 * Aucun fichier image, aucun emoji : rendu net a toute taille,
 * colores selon le theme, et 0 octet de telechargement.
 */
namespace Icons {

QColor normal();
QColor disabled();
QColor accent();

QIcon back();
QIcon forward();
QIcon reload();
QIcon stop();
QIcon home();
QIcon plus();
QIcon close();
QIcon menu();
QIcon shield();        // protection / eco actif
QIcon leaf();          // eco
QIcon lock();          // https
QIcon warn();          // http / non securise
QIcon search();
QIcon gear();
QIcon trash();
QIcon history();
QIcon download();
QIcon external();
QIcon chevronDown();
QIcon globe();
QIcon star();
QIcon eyeOff();
QIcon film();
QIcon list();

} // namespace Icons
