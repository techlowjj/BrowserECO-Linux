// Verifie que le HTML de la page d'accueil ne contient aucun placeholder non resolu
// et que les jetons CSS (%, &) sont intacts.
#include <QCoreApplication>
#include <QTextStream>
#include <QString>
#include <QStringList>

static QString buildSample(const QString &mode, const QString &engineLabel,
                           const QString &engineShort, int rules, const QString &saved, const QString &blocked)
{
    const char *shortcutJs = "(function(){ var q=this.value.trim(); }).call(this)";
    QString html = QStringLiteral(
        "<!DOCTYPE html><html lang=\"fr\"><head><style>"
        "body{background:radial-gradient(1200px 600px at 50% -10%,#1a1a35 0%,#0B0B14 60%);}"
        ".search input{width:100%;padding:15px 22px 15px 48px;}"
        "a[href*=\"?q=\"] &amp; b{c:d}"
        "</style></head><body>"
        "<div class=\"mode\">{mode} · {engineShort}</div>"
        "<div class=\"time\">{time}</div>"
        "<div class=\"date\">{date}</div>"
        "<input placeholder=\"Rechercher sur {engineLabel}…\">"
        "<div class=\"stats\">{rules} regles<br><b>Économie estimée</b> : {saved} · <b>bloquées</b> : {blocked}</div>"
        "<script>i.addEventListener('keydown',function(e){if(e.key==='Enter'){ {shortcut} }});</script>"
        "</body></html>");
    html.replace("{mode}", mode);
    html.replace("{engineShort}", engineShort);
    html.replace("{time}", "12:34");
    html.replace("{date}", "lundi 01 janvier 2026");
    html.replace("{engineLabel}", engineLabel);
    html.replace("{rules}", QString::number(rules));
    html.replace("{saved}", saved);
    html.replace("{blocked}", blocked);
    html.replace("{shortcut}", QString::fromLatin1(shortcutJs));
    return html;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    int fails = 0;
    const QString h = buildSample("ULTRA", "DuckDuckGo (HTML léger)", "DDG", 114310, "12,4 Mo", "1 337");

    // 1. aucun placeholder residuel
    const QStringList tokens = { "{mode}", "{engineShort}", "{time}", "{date}",
                                 "{engineLabel}", "{rules}", "{saved}", "{blocked}", "{shortcut}" };
    for (const QString &t : tokens) {
        if (h.contains(t)) { out << "  FAIL placeholder residuel " << t << "\n"; ++fails; }
    }
    // 2. doubles accolades issues du double echappement
    if (h.contains("{{")) {
        out << "  FAIL double accolade ouvrante : [" << h.mid(qMax(0, h.indexOf("{{")-40), 80) << "]\n";
        ++fails;
    }
    // le JS injecte doit avoir des accolades equilibrees
    { int open = 0, close = 0;
      for (QChar c : h) { if (c == QLatin1Char('{')) ++open; else if (c == QLatin1Char('}')) ++close; }
      if (open != close) { out << "  FAIL accolades desequilibrees " << open << " vs " << close << "\n"; ++fails; } }
    // 3. le CSS est intact (les % n'ont pas ete consommes)
    const QStringList cssBits = { "50% -10%", "0%,#0B0B14 60%", "width:100%", "48px" };
    for (const QString &b : cssBits)
        if (!h.contains(b)) { out << "  FAIL CSS casse : " << b << "\n"; ++fails; }
    // 4. valeurs injectees presentes
    const QStringList vals = { "ULTRA", "DDG", "114310", "12,4 Mo", "1 337", "DuckDuckGo (HTML léger)" };
    for (const QString &v : vals)
        if (!h.contains(v)) { out << "  FAIL valeur manquante : " << v << "\n"; ++fails; }
    // 5. le & d'echappement XML survit
    if (!h.contains("&amp;")) { out << "  FAIL &amp; perdu\n"; ++fails; }

    out << (fails == 0 ? "PAGE D'ACCUEIL : OK\n" : QString("%1 ECHEC(S)\n").arg(fails));
    return fails ? 1 : 0;
}
