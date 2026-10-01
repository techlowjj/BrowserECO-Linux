#include "services/historymanager.h"
#include <QCoreApplication>
#include <QTextStream>
#include <QTemporaryDir>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTemporaryDir dir;
    HistoryManager h(dir.path() + "/h.db");
    int fails = 0;
    auto chk = [&](const QString &label, bool cond) {
        if (!cond) ++fails;
        out << (cond ? "  ok   " : "  FAIL ") << label << "\n";
    };
    h.addVisit("https://fr.wikipedia.org/wiki/Linux", "Linux");
    h.addVisit("https://github.com/qt", "Qt");
    h.addVisit("https://stackoverflow.com/q/1", "Stack Q");
    h.addVisit("https://exemple.fr/a_b", "page_avec_tiret");

    chk("recherche 'linux'",  h.search("linux", 10).size() == 1);
    chk("recherche 'Qt'",     h.search("Qt", 10).size() == 1);
    chk("joker % echappe",    h.search("%", 10).isEmpty());
    chk("joker _ echappe",    h.search("_", 10).size() == 1); // seule l'URL avec vrai "_" matche
    chk("recherche vide",     h.search("", 10).isEmpty());
    chk("total = 4",          h.count() == 4);
    h.remove("https://github.com/qt");
    chk("apres suppression",  h.count() == 3);
    h.clear();
    chk("apres effacement",   h.count() == 0);
    out << (fails == 0 ? "HISTORIQUE : OK\n" : QString("%1 ECHEC(S)\n").arg(fails));
    return fails ? 1 : 0;
}
