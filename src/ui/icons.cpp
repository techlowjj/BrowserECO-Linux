#include "icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QtMath>

namespace {

const int kRes = 64;          // rendu surdimensionne puis reduit = net
const double kU = 24.0;       // unite de dessin (grille 24x24)
const double kStroke = 1.9;

using DrawFn = void (*)(QPainter &, const QRectF &);

QPixmap render(DrawFn fn, const QColor &color)
{
    QPixmap pm(kRes, kRes);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.scale(kRes / kU, kRes / kU);
    QPen pen(color);
    pen.setWidthF(kStroke);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const QRectF box(0, 0, kU, kU);
    fn(p, box);
    p.end();
    return pm;
}

QIcon make(DrawFn fn, bool withAccent = true)
{
    QIcon icon;
    const QPixmap n = render(fn, Icons::normal());
    const QPixmap d = render(fn, Icons::disabled());
    icon.addPixmap(n, QIcon::Normal, QIcon::On);
    icon.addPixmap(n, QIcon::Normal, QIcon::Off);
    icon.addPixmap(d, QIcon::Disabled, QIcon::On);
    icon.addPixmap(d, QIcon::Disabled, QIcon::Off);
    if (withAccent) {
        const QPixmap a = render(fn, Icons::accent());
        icon.addPixmap(a, QIcon::Active, QIcon::On);
        icon.addPixmap(a, QIcon::Selected, QIcon::On);
    }
    return icon;
}

QPointF polar(double cx, double cy, double r, double deg)
{
    const double a = qDegreesToRadians(deg);
    return QPointF(cx + r * std::cos(a), cy - r * std::sin(a));
}

void arrowHead(QPainter &p, const QPointF &tip, const QPointF &dir, double len, double half)
{
    const double a = std::atan2(dir.y(), dir.x());
    const QPointF b1(tip.x() - len * std::cos(a - half), tip.y() - len * std::sin(a - half));
    const QPointF b2(tip.x() - len * std::cos(a + half), tip.y() - len * std::sin(a + half));
    p.drawLine(tip, b1);
    p.drawLine(tip, b2);
}

void drawBack(QPainter &p, const QRectF &)
{
    p.drawPolyline(QPolygonF({ QPointF(15, 4.5), QPointF(7.5, 12), QPointF(15, 19.5) }));
    p.drawLine(QPointF(7.5, 12), QPointF(20, 12));
}

void drawForward(QPainter &p, const QRectF &)
{
    p.drawPolyline(QPolygonF({ QPointF(9, 4.5), QPointF(16.5, 12), QPointF(9, 19.5) }));
    p.drawLine(QPointF(16.5, 12), QPointF(4, 12));
}

void drawReload(QPainter &p, const QRectF &)
{
    const QPointF c(12, 12);
    const double r = 7.2;
    const double start = 65, span = 250;
    p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), int(start * 16), int(span * 16));
    // tete de fleche au point de depart, sens de parcours
    const QPointF tip = polar(c.x(), c.y(), r, start);
    const double a = qDegreesToRadians(start + 90); // tangente (sens trigonometrique)
    arrowHead(p, tip, QPointF(std::cos(a), -std::sin(a)), 4.0, 0.55);
}

void drawStop(QPainter &p, const QRectF &)
{
    QPen pen = p.pen();
    pen.setWidthF(0);
    p.setPen(pen);
    p.setBrush(p.pen().color());
    p.drawRoundedRect(QRectF(7, 7, 10, 10), 2, 2);
}

void drawHome(QPainter &p, const QRectF &)
{
    p.drawPolyline(QPolygonF({ QPointF(3.5, 11.5), QPointF(12, 4.5), QPointF(20.5, 11.5) }));
    p.drawPolyline(QPolygonF({ QPointF(5.8, 10.6), QPointF(5.8, 20), QPointF(18.2, 20), QPointF(18.2, 10.6) }));
    p.drawLine(QPointF(10, 20), QPointF(10, 14.5));
    p.drawLine(QPointF(14, 20), QPointF(14, 14.5));
}

void drawPlus(QPainter &p, const QRectF &)
{
    p.drawLine(QPointF(12, 5), QPointF(12, 19));
    p.drawLine(QPointF(5, 12), QPointF(19, 12));
}

void drawClose(QPainter &p, const QRectF &)
{
    p.drawLine(QPointF(6.5, 6.5), QPointF(17.5, 17.5));
    p.drawLine(QPointF(17.5, 6.5), QPointF(6.5, 17.5));
}

void drawMenu(QPainter &p, const QRectF &)
{
    p.drawLine(QPointF(4.5, 7), QPointF(19.5, 7));
    p.drawLine(QPointF(4.5, 12), QPointF(19.5, 12));
    p.drawLine(QPointF(4.5, 17), QPointF(19.5, 17));
}

void drawShield(QPainter &p, const QRectF &)
{
    QPainterPath path;
    path.moveTo(12, 3);
    path.lineTo(20, 6.2);
    path.lineTo(20, 12);
    path.cubicTo(20, 16.4, 16.6, 19.6, 12, 21.2);
    path.cubicTo(7.4, 19.6, 4, 16.4, 4, 12);
    path.lineTo(4, 6.2);
    path.closeSubpath();
    p.drawPath(path);
}

void drawShieldCheck(QPainter &p, const QRectF &)
{
    drawShield(p, QRectF());
    p.drawPolyline(QPolygonF({ QPointF(8.6, 12.2), QPointF(11.1, 14.8), QPointF(15.4, 9.8) }));
}

void drawLeaf(QPainter &p, const QRectF &)
{
    QPainterPath path;
    path.moveTo(19.5, 4.5);
    path.cubicTo(11, 4.2, 5, 8.4, 5, 14);
    path.cubicTo(5, 17.2, 6.6, 19.4, 8.4, 20.4);
    path.cubicTo(7.6, 15.6, 9.6, 11.4, 14.6, 8.8);
    path.cubicTo(16.4, 7.9, 18.2, 7.6, 19.5, 7.8);
    path.cubicTo(20.4, 10.4, 19.4, 15.2, 16.2, 18);
    path.cubicTo(13.8, 20, 11, 20.2, 8.4, 20.4);
    path.closeSubpath();
    p.drawPath(path);
    p.drawLine(QPointF(8.4, 20.4), QPointF(15.2, 10.6));
}

void drawLock(QPainter &p, const QRectF &)
{
    p.drawArc(QRectF(8, 3.4, 8, 8), 0, 180 * 16);
    p.drawRoundedRect(QRectF(5.6, 10.2, 12.8, 10.4), 2.2, 2.2);
    p.drawLine(QPointF(12, 14), QPointF(12, 17));
}

void drawWarn(QPainter &p, const QRectF &)
{
    QPainterPath path;
    path.moveTo(12, 4.2);
    path.lineTo(21, 19.6);
    path.lineTo(3, 19.6);
    path.closeSubpath();
    p.drawPath(path);
    p.drawLine(QPointF(12, 10), QPointF(12, 14.2));
    QPen pen = p.pen();
    pen.setWidthF(2.2);
    p.setPen(pen);
    p.drawPoint(QPointF(12, 16.8));
}

void drawSearch(QPainter &p, const QRectF &)
{
    p.drawEllipse(QRectF(4.4, 4.4, 11, 11));
    p.drawLine(QPointF(14.2, 14.2), QPointF(20, 20));
}

void drawGear(QPainter &p, const QRectF &)
{
    const double cx = 12, cy = 12;
    p.drawEllipse(QRectF(cx - 4.9, cy - 4.9, 9.8, 9.8));
    for (int i = 0; i < 8; ++i) {
        const double a = i * 45.0;
        p.drawLine(polar(cx, cy, 5.6, a), polar(cx, cy, 8.4, a));
    }
    QPen pen = p.pen();
    pen.setWidthF(0);
    p.setPen(pen);
    p.setBrush(pen.color());
    p.drawEllipse(QRectF(cx - 1.7, cy - 1.7, 3.4, 3.4));
}

void drawTrash(QPainter &p, const QRectF &)
{
    p.drawLine(QPointF(4.2, 6.6), QPointF(19.8, 6.6));
    p.drawPolyline(QPolygonF({ QPointF(9.4, 6.6), QPointF(9.4, 4.2), QPointF(14.6, 4.2), QPointF(14.6, 6.6) }));
    p.drawPolyline(QPolygonF({ QPointF(6.6, 6.6), QPointF(7.6, 20.2), QPointF(16.4, 20.2), QPointF(17.4, 6.6) }));
}

void drawHistory(QPainter &p, const QRectF &)
{
    p.drawArc(QRectF(4.2, 4.2, 15.6, 15.6), 60 * 16, -300 * 16);
    const QPointF tip = polar(12, 12, 7.8, 60);
    arrowHead(p, tip, QPointF(0.55, -0.83), 3.8, 0.6);
    p.drawPolyline(QPolygonF({ QPointF(12, 7.4), QPointF(12, 12.2), QPointF(15.6, 14.2) }));
}

void drawDownload(QPainter &p, const QRectF &)
{
    p.drawLine(QPointF(12, 3.8), QPointF(12, 15.4));
    arrowHead(p, QPointF(12, 15.8), QPointF(0, 1), 4.2, 0.6);
    p.drawLine(QPointF(4.4, 19.6), QPointF(19.6, 19.6));
}

void drawExternal(QPainter &p, const QRectF &)
{
    p.drawPolyline(QPolygonF({ QPointF(13.5, 4.6), QPointF(19.4, 4.6), QPointF(19.4, 10.5) }));
    p.drawLine(QPointF(19.4, 4.6), QPointF(11.6, 12.4));
    p.drawPolyline(QPolygonF({ QPointF(17, 13.4), QPointF(17, 19.4), QPointF(4.6, 19.4), QPointF(4.6, 7), QPointF(10.6, 7) }));
}

void drawChevronDown(QPainter &p, const QRectF &)
{
    p.drawPolyline(QPolygonF({ QPointF(6.5, 9.5), QPointF(12, 15), QPointF(17.5, 9.5) }));
}

void drawGlobe(QPainter &p, const QRectF &)
{
    p.drawEllipse(QRectF(4, 4, 16, 16));
    p.drawLine(QPointF(4, 12), QPointF(20, 12));
    p.drawArc(QRectF(8, 4, 8, 16), 0, 360 * 16);
    p.drawArc(QRectF(4, 6.4, 16, 11.2), 0, 360 * 16);
}

void drawStar(QPainter &p, const QRectF &)
{
    QPainterPath path;
    for (int i = 0; i < 10; ++i) {
        const double r = (i % 2 == 0) ? 8.2 : 3.4;
        const double a = -90.0 + i * 36.0;
        const QPointF pt = polar(12, 12, r, a);
        if (i == 0) path.moveTo(pt);
        else path.lineTo(pt);
    }
    path.closeSubpath();
    p.drawPath(path);
}

void drawEyeOff(QPainter &p, const QRectF &)
{
    p.drawArc(QRectF(3.6, 7.6, 16.8, 8.8), 0, 360 * 16);
    p.drawLine(QPointF(4.4, 19.6), QPointF(19.6, 4.4));
}

void drawFilm(QPainter &p, const QRectF &)
{
    p.drawRoundedRect(QRectF(3.6, 5.4, 16.8, 13.2), 2, 2);
    p.drawLine(QPointF(8, 5.4), QPointF(8, 18.6));
    p.drawLine(QPointF(16, 5.4), QPointF(16, 18.6));
    p.drawLine(QPointF(3.6, 12), QPointF(20.4, 12));
}

void drawList(QPainter &p, const QRectF &)
{
    p.drawLine(QPointF(4.6, 6.6), QPointF(19.4, 6.6));
    p.drawLine(QPointF(4.6, 12), QPointF(19.4, 12));
    p.drawLine(QPointF(4.6, 17.4), QPointF(19.4, 17.4));
    p.drawEllipse(QRectF(2.2, 5.6, 2, 2));
    p.drawEllipse(QRectF(2.2, 11, 2, 2));
    p.drawEllipse(QRectF(2.2, 16.4, 2, 2));
}

} // namespace

namespace Icons {

QColor normal()   { return QColor(0xC6, 0xC9, 0xDC); }
QColor disabled() { return QColor(0x4A, 0x4C, 0x63); }
QColor accent()   { return QColor(0x00, 0xD4, 0xAA); }

QIcon back()        { return make(drawBack); }
QIcon forward()     { return make(drawForward); }
QIcon reload()      { return make(drawReload); }
QIcon stop()        { return make(drawStop, false); }
QIcon home()        { return make(drawHome); }
QIcon plus()        { return make(drawPlus); }
QIcon close()       { return make(drawClose, false); }
QIcon menu()        { return make(drawMenu); }
QIcon shield()      { return make(drawShieldCheck); }
QIcon leaf()        { return make(drawLeaf); }
QIcon lock()        { return make(drawLock); }
QIcon warn()        { return make(drawWarn); }
QIcon search()      { return make(drawSearch); }
QIcon gear()        { return make(drawGear); }
QIcon trash()       { return make(drawTrash); }
QIcon history()     { return make(drawHistory); }
QIcon download()    { return make(drawDownload); }
QIcon external()    { return make(drawExternal); }
QIcon chevronDown() { return make(drawChevronDown); }
QIcon globe()       { return make(drawGlobe); }
QIcon star()        { return make(drawStar); }
QIcon eyeOff()      { return make(drawEyeOff); }
QIcon film()        { return make(drawFilm); }
QIcon list()        { return make(drawList); }

} // namespace Icons
