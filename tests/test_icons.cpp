// Verifie que chaque icone se dessine (pixmap non vide) et produit des pixels.
#include <QGuiApplication>
#include <QPixmap>
#include <QPainter>
#include <QTextStream>
#include <QIcon>
#include "ui/icons.h"

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);
    struct Item { const char *name; QIcon (*fn)(); };
    const Item items[] = {
        {"back", Icons::back}, {"forward", Icons::forward}, {"reload", Icons::reload},
        {"stop", Icons::stop}, {"home", Icons::home}, {"plus", Icons::plus},
        {"close", Icons::close}, {"menu", Icons::menu}, {"shield", Icons::shield},
        {"leaf", Icons::leaf}, {"lock", Icons::lock}, {"warn", Icons::warn},
        {"search", Icons::search}, {"gear", Icons::gear}, {"trash", Icons::trash},
        {"history", Icons::history}, {"download", Icons::download}, {"external", Icons::external},
        {"chevronDown", Icons::chevronDown}, {"globe", Icons::globe}, {"star", Icons::star},
        {"eyeOff", Icons::eyeOff}, {"film", Icons::film}, {"list", Icons::list},
    };
    int fails = 0;
    for (const Item &it : items) {
        const QIcon ic = it.fn();
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.drawPixmap(0, 0, ic.pixmap(16, 16));
        p.end();
        const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
        int opaque = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                if (qAlpha(img.pixel(x, y)) > 24) ++opaque;
        const bool ok = !ic.isNull() && opaque >= 8;
        if (!ok) ++fails;
        out << (ok ? "  ok   " : "  FAIL ") << it.name << "  pixels=" << opaque << "\n";
    }
    out << (fails == 0 ? "ICONES : OK\n" : QString("%1 ECHEC(S)\n").arg(fails));
    return fails ? 1 : 0;
}
