#include "ecopanel.h"
#include "services/ecoimageserver.h"
#include "services/imagecodec.h"
#include "icons.h"
#include "ecointerceptor.h"
#include "services/searchengine.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QToolButton>
#include <QSlider>
#include <QPushButton>
#include <QFrame>
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCoreApplication>

namespace {

QString humanBytes(qint64 b)
{
    if (b <= 0) return QStringLiteral("0 Ko");
    const char *u[] = { "o", "Ko", "Mo", "Go" };
    double v = double(b);
    int i = 0;
    while (v >= 1024.0 && i < 3) { v /= 1024.0; ++i; }
    return QString::number(v, 'f', v < 10 ? 1 : 0) + QLatin1Char(' ') + QString::fromLatin1(u[i]);
}

QToolButton *makeToggle(const QString &text, const QString &tip, const QIcon &icon, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setIcon(icon);
    b->setIconSize(QSize(15, 15));
    b->setCheckable(true);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setCursor(Qt::PointingHandCursor);
    b->setAutoRaise(false);
    return b;
}

} // namespace

EcoPanel::EcoPanel(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("ecoPanel"));
    setFrameShape(QFrame::NoFrame);
    buildUi();
}

void EcoPanel::buildUi()
{
    setStyleSheet(QStringLiteral(
        "#ecoPanel{background:#12121F;border:1px solid #2C3150;border-radius:12px;}"
        "QLabel{color:#C6C9DC;background:transparent;border:none;}"
        "QToolButton#ecoToggle{background:#1B1B2C;color:#C6C9DC;border:1px solid #2C3150;"
        "border-radius:9px;padding:7px 10px;text-align:left;font-size:12px;}"
        "QToolButton#ecoToggle:hover{border-color:#00A88A;background:#202038;}"
        "QToolButton#ecoToggle:checked{background:#0E2A2A;border-color:#00D4AA;color:#7BFFE4;}"
        "QSlider::groove:horizontal{background:#0F0F17;height:5px;border-radius:3px;}"
        "QSlider::handle:horizontal{background:#00D4AA;width:13px;height:13px;margin:-5px 0;border-radius:7px;}"
        "QSlider::sub-page:horizontal{background:#00D4AA;border-radius:3px;}"
        "QToolButton#linkBtn{background:transparent;color:#8C90AE;border:none;text-align:left;"
        "padding:5px 6px;border-radius:7px;font-size:12px;}"
        "QToolButton#linkBtn:hover{background:#1B1B2C;color:#00D4AA;}"
        "QFrame#sep{background:#22243A;max-height:1px;border:none;}"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 11, 12, 11);
    root->setSpacing(9);

    // ---- titre
    auto *title = new QLabel(tr("Data saver & vie privée"), this);
    QFont tf = title->font(); tf.setPointSizeF(10.5); tf.setBold(true);
    title->setFont(tf);
    title->setStyleSheet(QStringLiteral("color:#E8E8F0;"));
    root->addWidget(title);

    // ---- bascule data saver
    m_dataSaver = makeToggle(tr("Économie de données  (Save-Data + bloqueurs)"),
                             tr("Envoie l'en-tête Save-Data et bloque pubs/trackers/polices non essentielles"),
                             Icons::shield(), this);
    m_dataSaver->setObjectName(QStringLiteral("ecoToggle"));
    root->addWidget(m_dataSaver);
    connect(m_dataSaver, &QToolButton::toggled, this, &EcoPanel::dataSaverToggled);

    // ---- images
    m_images = makeToggle(tr("Bloquer les images"),
                          tr("Économie très importante, mais la page devient « brute »"),
                          Icons::eyeOff(), this);
    m_images->setObjectName(QStringLiteral("ecoToggle"));
    root->addWidget(m_images);
    connect(m_images, &QToolButton::toggled, this, &EcoPanel::imagesToggled);

    // ---- ultra
    m_ultra = makeToggle(tr("Mode Ultra  (vidéo + streaming bloqués)"),
                         tr("Coupe YouTube/streaming, favicons et chunks média. 480 px / qualité 50 forcés"),
                         Icons::film(), this);
    m_ultra->setObjectName(QStringLiteral("ecoToggle"));
    root->addWidget(m_ultra);
    connect(m_ultra, &QToolButton::toggled, this, &EcoPanel::ultraToggled);

    // ---- favicons
    m_favicons = makeToggle(tr("Charger les favicons"),
                            tr("Les icônes de site coûtent ~1 requête par site (désactivé en Ultra)"),
                            Icons::star(), this);
    m_favicons->setObjectName(QStringLiteral("ecoToggle"));
    root->addWidget(m_favicons);
    connect(m_favicons, &QToolButton::toggled, this, &EcoPanel::faviconsToggled);

    // ---- Sec-GPC
    m_secGpc = makeToggle(tr("Signal vie privée  (Sec-GPC)"),
                          tr("Envoie « Sec-GPC: 1 » : demande aux sites de limiter les données "
                             "(images, vidéos). 1 octet par requête."),
                          Icons::lock(), this);
    m_secGpc->setObjectName(QStringLiteral("ecoToggle"));
    root->addWidget(m_secGpc);
    connect(m_secGpc, &QToolButton::toggled, this, &EcoPanel::secGpcToggled);

    auto *sep1 = new QFrame(this);
    sep1->setObjectName(QStringLiteral("sep"));
    sep1->setFrameShape(QFrame::NoFrame);
    sep1->setFixedHeight(1);
    root->addWidget(sep1);

    // ---- qualité
    auto *qrow = new QHBoxLayout;
    qrow->setSpacing(8);
    auto *qlabel = new QLabel(tr("Qualité WebP"), this);
    qlabel->setToolTip(tr("Seuil indicatif : en dessous de ~45 la qualité devient visible. 0 = pas de compression."));
    qrow->addWidget(qlabel);
    m_quality = new QSlider(Qt::Horizontal, this);
    m_quality->setRange(0, 85);
    m_quality->setValue(65);
    m_quality->setToolTip(tr("0 = images non compressées · 85 = qualité maximale"));
    qrow->addWidget(m_quality, 1);
    m_qualityValue = new QLabel(QStringLiteral("65"), this);
    m_qualityValue->setFixedWidth(30);
    m_qualityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_qualityValue->setStyleSheet(QStringLiteral("color:#00D4AA;font-weight:600;"));
    qrow->addWidget(m_qualityValue);
    root->addLayout(qrow);
    connect(m_quality, &QSlider::valueChanged, this, &EcoPanel::onSliderMoved);

    auto *sep2 = new QFrame(this);
    sep2->setObjectName(QStringLiteral("sep"));
    sep2->setFixedHeight(1);
    root->addWidget(sep2);

    // ---- stats
    m_saved = new QLabel(QStringLiteral("≈ 0 Ko économisés"), this);
    QFont sf = m_saved->font(); sf.setPointSizeF(11.5); sf.setBold(true);
    m_saved->setFont(sf);
    m_saved->setStyleSheet(QStringLiteral("color:#00D4AA;"));
    m_saved->setToolTip(tr("Estimation à partir des requêtes bloquées (moyennes par catégorie)"));
    root->addWidget(m_saved);

    m_ratio = new QLabel(QStringLiteral("0 requête bloquée"), this);
    m_ratio->setStyleSheet(QStringLiteral("color:#7A7F9E;font-size:11px;"));
    root->addWidget(m_ratio);

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 2, 0, 2);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(1);
    struct Row { const char *name; int cat; };
    const Row rows[] = {
        { QT_TR_NOOP("Pubs"),        int(EcoCategory::Pubs) },
        { QT_TR_NOOP("Trackers"),    int(EcoCategory::Trackers) },
        { QT_TR_NOOP("Polices"),     int(EcoCategory::Fonts) },
        { QT_TR_NOOP("Médias"),      int(EcoCategory::Media) },
        { QT_TR_NOOP("Images"),      int(EcoCategory::Images) },
        { QT_TR_NOOP("Préchargement"), int(EcoCategory::Prefetch) },
        { QT_TR_NOOP("Streaming"),   int(EcoCategory::Stream) },
    };
    for (int i = 0; i < int(sizeof(rows) / sizeof(rows[0])); ++i) {
        auto *n = new QLabel(tr(rows[i].name), this);
        n->setStyleSheet(QStringLiteral("color:#7A7F9E;font-size:11px;"));
        auto *v = new QLabel(QStringLiteral("0"), this);
        v->setStyleSheet(QStringLiteral("color:#C6C9DC;font-size:11px;"));
        v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(n, i, 0);
        grid->addWidget(v, i, 1);
        m_catLabels.append(v);
    }
    grid->setColumnStretch(0, 1);
    root->addLayout(grid);

    auto *sep3 = new QFrame(this);
    sep3->setObjectName(QStringLiteral("sep"));
    sep3->setFixedHeight(1);
    root->addWidget(sep3);

    // ---- images compressées (MESURE REELLE, pas une estimation)
    {
        auto *titre = new QHBoxLayout;
        auto *t = new QLabel(tr("Images compressées"), this);
        t->setStyleSheet(QStringLiteral("color:#C6C9DC;font-size:11px;font-weight:600;"));
        titre->addWidget(t);
        titre->addStretch(1);
        m_imgReset = new QToolButton(this);
        m_imgReset->setText(tr("Remise à zéro"));
        m_imgReset->setToolTip(tr("Remet les compteurs à zéro sans vider le cache réseau"));
        m_imgReset->setAutoRaise(true);
        m_imgReset->setStyleSheet(QStringLiteral("color:#7A7F9E;font-size:10px;"));
        connect(m_imgReset, &QToolButton::clicked, this, &EcoPanel::imageStatsResetRequested);
        titre->addWidget(m_imgReset);
        root->addLayout(titre);
    }
    m_imgSaved = new QLabel(QStringLiteral("− 0 Ko réellement économisés"), this);
    m_imgSaved->setStyleSheet(QStringLiteral("color:#00D4AA;font-size:11.5px;font-weight:600;"));
    m_imgSaved->setToolTip(tr("Octets téléchargés moins octets servis, mesurés sur le réseau"));
    root->addWidget(m_imgSaved);
    m_imgDetail = new QLabel(QStringLiteral("—"), this);
    m_imgDetail->setStyleSheet(QStringLiteral("color:#7A7F9E;font-size:11px;"));
    m_imgDetail->setWordWrap(true);
    root->addWidget(m_imgDetail);

    auto *sepImg = new QFrame(this);
    sepImg->setObjectName(QStringLiteral("sep"));
    sepImg->setFixedHeight(1);
    root->addWidget(sepImg);

    // ---- recherche
    auto *searchBox = makeToggle(tr("Moteur : —"), tr("Cliquez pour choisir le moteur"),
                                 Icons::search(), this);
    searchBox->setObjectName(QStringLiteral("ecoToggle"));
    searchBox->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    root->addWidget(searchBox);
    {
        auto *menu = new QMenu(this);   // detruit avec le panneau Eco
        menu->setStyleSheet(QStringLiteral(
            "QMenu{background:#151525;color:#E8E8F0;border:1px solid #2E3350;padding:6px;}"
            "QMenu::item{padding:7px 26px 7px 12px;border-radius:6px;}"
            "QMenu::item:selected{background:#243046;}"
            "QMenu::separator{height:1px;background:#22243A;margin:5px 8px;}"));
        int id = 0;
        for (const SearchEngine &e : SearchEngineManager::registry()) {
            QAction *a = menu->addAction(e.label);
            a->setData(id);
            a->setToolTip(e.weightNote);
            ++id;
        }
        menu->addSeparator();
        QAction *inst = menu->addAction(tr("Mon instance SearXNG…"));
        inst->setData(-1);
        QAction *prov = menu->addAction(tr("Service de suggestions…"));
        prov->setData(-4);
        QAction *af = menu->addAction(tr("Repli auto si mur anti-bot"));
        af->setCheckable(true);
        af->setData(-2);
        QAction *rs = menu->addAction(tr("Suggestions distantes (~100 o)"));
        rs->setCheckable(true);
        rs->setData(-3);
        m_autoFallback = makeToggle(tr("Repli auto si le moteur est bloqué"), QString(), Icons::shield(), this);
        m_autoFallback->setObjectName(QStringLiteral("ecoToggle"));
        m_autoFallback->hide();
        connect(m_autoFallback, &QToolButton::toggled, this, &EcoPanel::autoFallbackToggled);
        m_remoteSuggest = makeToggle(tr("Suggestions réseau (~100 octets)"), QString(), Icons::search(), this);
        m_remoteSuggest->setObjectName(QStringLiteral("ecoToggle"));
        m_remoteSuggest->hide();
        connect(m_remoteSuggest, &QToolButton::toggled, this, &EcoPanel::remoteSuggestToggled);
        root->addWidget(m_autoFallback);
        root->addWidget(m_remoteSuggest);

        connect(searchBox, &QToolButton::clicked, this, [this, searchBox, menu, af, rs]{
            m_autoFallback->setChecked(false);
            m_autoFallback->hide();
            m_remoteSuggest->setChecked(false);
            m_remoteSuggest->hide();
            if (af->isChecked() || rs->isChecked()) {
                m_autoFallback->setChecked(af->isChecked());
                m_remoteSuggest->setChecked(rs->isChecked());
                m_autoFallback->show();
                m_remoteSuggest->show();
            }
            QPoint g = searchBox->mapToGlobal(QPoint(0, searchBox->height() + 4));
            menu->popup(g);
        });
        connect(menu, &QMenu::triggered, this, [this](QAction *a){ emitEngine(a->data().toInt()); });
        m_rows.append(m_autoFallback);
        m_rows.append(m_remoteSuggest);
        m_searchBtn = searchBox;
    }

    auto *sep4 = new QFrame(this);
    sep4->setObjectName(QStringLiteral("sep"));
    sep4->setFixedHeight(1);
    root->addWidget(sep4);

    // ---- actions
    auto *hist = new QToolButton(this);
    hist->setObjectName(QStringLiteral("linkBtn"));
    hist->setText(tr("Historique  (Ctrl+H)"));
    hist->setIcon(Icons::history());
    hist->setCursor(Qt::PointingHandCursor);
    hist->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(hist, &QToolButton::clicked, this, &EcoPanel::openHistoryRequested);
    root->addWidget(hist);

    auto *cache = new QToolButton(this);
    cache->setObjectName(QStringLiteral("linkBtn"));
    cache->setText(tr("Vider le cache"));
    cache->setIcon(Icons::trash());
    cache->setCursor(Qt::PointingHandCursor);
    cache->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(cache, &QToolButton::clicked, this, &EcoPanel::clearCacheRequested);
    root->addWidget(cache);

    auto *sc = new QToolButton(this);
    sc->setObjectName(QStringLiteral("linkBtn"));
    sc->setText(tr("Raccourcis clavier  (F1)"));
    sc->setIcon(Icons::list());
    sc->setCursor(Qt::PointingHandCursor);
    sc->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(sc, &QToolButton::clicked, this, &EcoPanel::shortcutsRequested);
    root->addWidget(sc);

    auto *ab = new QToolButton(this);
    ab->setObjectName(QStringLiteral("linkBtn"));
    ab->setText(tr("À propos"));
    ab->setIcon(Icons::globe());
    ab->setCursor(Qt::PointingHandCursor);
    ab->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(ab, &QToolButton::clicked, this, &EcoPanel::aboutRequested);
    root->addWidget(ab);

    setFixedWidth(360);
}

void EcoPanel::setInterceptor(EcoInterceptor *i) { m_interceptor = i; refresh(); }
void EcoPanel::setImageServer(EcoImageServer *server) { m_imageServer = server; refresh(); }
void EcoPanel::setSearchManager(SearchEngineManager *s) { m_search = s; }

void EcoPanel::setDataSaverChecked(bool on) { if (m_dataSaver) m_dataSaver->setChecked(on); }
void EcoPanel::setImagesChecked(bool on)     { if (m_images) m_images->setChecked(on); }
void EcoPanel::setUltraChecked(bool on)      { if (m_ultra) m_ultra->setChecked(on); }
void EcoPanel::setFaviconsChecked(bool on)   { if (m_favicons) m_favicons->setChecked(on); }
void EcoPanel::setSecGpcChecked(bool on)     { if (m_secGpc) m_secGpc->setChecked(on); }
void EcoPanel::setQualityValue(int v)        { if (m_quality) { m_quality->setValue(v); m_qualityValue->setText(QString::number(v)); } }

void EcoPanel::onSliderMoved(int v)
{
    m_qualityValue->setText(QString::number(v));
    emit qualityChanged(v);
}

void EcoPanel::emitEngine(int actionId)
{
    if (actionId == -1) { emit searxInstanceRequested(); return; }
    if (actionId == -2) { emit autoFallbackToggled(m_autoFallback->isChecked()); return; }
    if (actionId == -3) { emit remoteSuggestToggled(m_remoteSuggest->isChecked()); return; }
    if (actionId == -4) { emit providerRequested(); return; }
    const auto &engines = SearchEngineManager::registry();
    if (actionId >= 0 && actionId < engines.size()) emit engineChanged(engines.at(actionId).id);
}

void EcoPanel::refresh()
{
    if (m_interceptor) {
        const qint64 blocked = m_interceptor->blockedCount();
        m_saved->setText(tr("≈ %1 économisés").arg(humanBytes(m_interceptor->estimatedSavedBytes())));
        m_ratio->setText(tr("%1 requêtes bloquées · %2 % autorisées")
                             .arg(blocked).arg(m_interceptor->allowedCount()));
        for (int i = 0; i < m_catLabels.size() && i < kEcoCategoryCount; ++i)
            m_catLabels[i]->setText(QString::number(m_interceptor->categoryCount(i)));
    }
    if (m_imageServer && m_imgSaved && m_imgDetail) {
        const qint64 dl = m_imageServer->originalBytes();
        const qint64 servi = m_imageServer->servedBytes();
        const qint64 gain = dl - servi;
        const qint64 cacheGain = m_imageServer->cache().savedBytes();
        const QString format = QString::fromLatin1(ImageCodec::preferredFormat());
        m_imgSaved->setText(tr("− %1 réellement économisés").arg(humanBytes(qMax<qint64>(0, gain + cacheGain))));
        if (dl > 0) {
            const int pct = dl > 0 ? int(100.0 * gain / dl) : 0;
            m_imgDetail->setText(tr("%1 compressées · %2 en cache · %3→%4 (−%5 %) · %6")
                                     .arg(m_imageServer->compressedCount())
                                     .arg(m_imageServer->cache().hits())
                                     .arg(humanBytes(dl)).arg(humanBytes(servi)).arg(qMax(0, pct))
                                     .arg(format));
        } else {
            m_imgDetail->setText(tr("aucune image mesurée pour l'instant"));
        }
    }
    if (m_search && m_searchBtn) {
        const SearchEngine *e = m_search->current();
        m_searchBtn->setText(tr("Moteur : %1").arg(e ? e->label : QStringLiteral("—")));
        m_searchBtn->setToolTip(e ? e->weightNote : QString());
        m_autoFallback->setChecked(m_search->autoFallback());
        m_remoteSuggest->setChecked(m_search->remoteSuggestions());
    }
}
