#include "omnibox.h"
#include "icons.h"
#include "services/searchengine.h"
#include "services/historymanager.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QApplication>
#include <QListWidgetItem>
#include <QFontMetrics>
#include <QStyle>
#include <QDebug>

namespace {
constexpr int kRoleUrl = Qt::UserRole + 1;
constexpr int kRoleIsSearch = Qt::UserRole + 2;
constexpr int kRoleSub = Qt::UserRole + 3;
constexpr int kMaxVisible = 8;
} // namespace

OmniBox::OmniBox(QWidget *parent)
    : QWidget(parent)
{
    // La popup de suggestions est un enfant de la fenetre : on doit la replier
    // des que la fenetre perd le focus, sinon elle "flotte" au-dessus d'une autre
    // application. QWidget n'expose pas windowDeactivate -> filtre d'evenement.
    if (QWidget *top = window())
        top->installEventFilter(this);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(QStringLiteral("Rechercher ou saisir une URL  (Ctrl+L)"));
    m_edit->setClearButtonEnabled(true);
    m_edit->setFont(QFont(m_edit->font().family(), 10));
    m_edit->installEventFilter(this);

    // icone de securite / favicon a gauche du champ
    m_secIcon = new QLabel(this);
    m_secIcon->setFixedSize(20, 20);
    m_secIcon->setPixmap(Icons::globe().pixmap(16, 16));
    m_secIcon->setStyleSheet(QStringLiteral("background:transparent; padding:0 2px;"));

    // badge moteur a droite
    m_engineBtn = new QToolButton(this);
    m_engineBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_engineBtn->setIcon(Icons::search());
    m_engineBtn->setIconSize(QSize(14, 14));
    m_engineBtn->setText(QStringLiteral("DDG"));
    m_engineBtn->setAutoRaise(true);
    m_engineBtn->setCursor(Qt::PointingHandCursor);
    m_engineBtn->setStyleSheet(QStringLiteral(
        "QToolButton{color:#8C90AE;background:transparent;border:none;padding:2px 6px;"
        "border-radius:6px;font-size:11px;font-weight:600;}"
        "QToolButton:hover{color:#00D4AA;background:#1E2A38;}"));
    m_engineBtn->setToolTip(QStringLiteral("Moteur de recherche — clic pour changer (Alt+1 à Alt+9)"));
    connect(m_engineBtn, &QToolButton::clicked, this, [this]{
        const QPoint g = m_engineBtn->mapToGlobal(QPoint(0, m_engineBtn->height() + 4));
        emit engineMenuRequested(g);
    });

    lay->addWidget(m_secIcon);
    lay->addWidget(m_edit, 1);
    lay->addWidget(m_engineBtn);

    // popup de suggestions : enfant de la fenetre => toujours au-dessus
    QWidget *top = window();
    m_popup = new QListWidget(top);
    m_popup->setObjectName(QStringLiteral("omniPopup"));
    m_popup->setFocusPolicy(Qt::NoFocus);
    m_popup->setUniformItemSizes(true);
    m_popup->setMouseTracking(true);
    m_popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_popup->setStyleSheet(QStringLiteral(
        "#omniPopup{background:#151525;border:1px solid #2E3350;border-radius:10px;"
        "padding:4px;color:#E8E8F0;outline:none;}"
        "#omniPopup::item{padding:6px 8px;border-radius:6px;}"
        "#omniPopup::item:selected{background:#243046;color:#FFFFFF;}"));
    m_popup->hide();
    connect(m_popup, &QListWidget::itemClicked, this, [this](QListWidgetItem *it){ activateRow(m_popup->row(it)); });

    connect(m_edit, &QLineEdit::textEdited, this, &OmniBox::onTextEdited);
    connect(m_edit, &QLineEdit::returnPressed, this, &OmniBox::onReturnPressed);
    connect(m_edit, &QLineEdit::returnPressed, m_popup, &QListWidget::hide);

    refreshEngineBadge();
}

OmniBox::~OmniBox()
{
    if (m_popup) m_popup->deleteLater();
}

void OmniBox::setSearchManager(SearchEngineManager *m)
{
    if (m_search) m_search->disconnect(this);
    m_search = m;
    if (m_search) {
        connect(m_search, &SearchEngineManager::suggestionsReady, this, &OmniBox::onSuggestionsReady);
    }
    refreshEngineBadge();
}

void OmniBox::setHistoryManager(HistoryManager *h)
{
    m_history = h;
}

void OmniBox::refreshEngineBadge()
{
    if (!m_search) return;
    const SearchEngine *e = m_search->current();
    m_engineBtn->setText(e ? e->shortLabel : QStringLiteral("?"));
    const QString tip = e ? tr("Moteur : %1\n%2\nClic pour changer · Alt+1 à Alt+9").arg(e->label, e->weightNote)
                          : tr("Moteur de recherche");
    m_engineBtn->setToolTip(tip);
    if (m_edit && m_edit->hasFocus())
        m_edit->setPlaceholderText(tr("Rechercher sur %1…").arg(e ? e->label : QStringLiteral("le Web")));
}

QString OmniBox::text() const { return m_edit->text(); }

void OmniBox::setText(const QString &t)
{
    // m_updating empeche onTextEdited de reconstruire la liste : poser une URL
    // depuis le code ne doit pas faire clignoter les suggestions.
    m_updating = true;
    m_edit->setText(t);
    m_edit->setCursorPosition(0);
    m_updating = false;
    hideSuggestions();
}

void OmniBox::selectAll()
{
    m_edit->setFocus();
    m_edit->selectAll();
}

void OmniBox::focusInEvent(QFocusEvent *e)
{
    QWidget::focusInEvent(e);
    if (m_edit) m_edit->setFocus();
}

void OmniBox::setCurrentUrl(const QUrl &url)
{
    if (m_edit->hasFocus() || m_edit->text().isEmpty()) {
        setText(url.isEmpty() || url.toString() == QLatin1String("about:home")
                   ? QString() : url.toDisplayString(QUrl::RemovePassword | QUrl::NormalizePathSegments));
    }
    if (url.scheme() == QLatin1String("https")) {
        m_secIcon->setPixmap(Icons::lock().pixmap(15, 15));
        m_secIcon->setToolTip(tr("Connexion chiffrée (HTTPS)"));
    } else if (url.scheme() == QLatin1String("http")) {
        m_secIcon->setPixmap(Icons::warn().pixmap(15, 15));
        m_secIcon->setToolTip(tr("Connexion non chiffrée (HTTP)"));
    } else if (url.scheme() == QLatin1String("file")) {
        m_secIcon->setPixmap(Icons::globe().pixmap(15, 15));
        m_secIcon->setToolTip(tr("Fichier local"));
    } else {
        m_secIcon->setPixmap(Icons::globe().pixmap(15, 15));
        m_secIcon->setToolTip(QString());
    }
}

void OmniBox::setCurrentUrlForced(const QUrl &url)
{
    // Ne jamais voler le texte en cours de saisie.
    if (m_edit->hasFocus()) { setCurrentUrl(url); return; }
    m_updating = true;
    const bool isHome = url.isEmpty() || url.toString() == QLatin1String("about:home");
    m_edit->setText(isHome ? QString()
                           : url.toDisplayString(QUrl::RemovePassword | QUrl::NormalizePathSegments));
    m_updating = false;
    setCurrentUrl(url);
}

bool OmniBox::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == window() && ev->type() == QEvent::WindowDeactivate) {
        dismissSuggestions();
        return false;
    }
    if (obj == m_edit) {
        if (ev->type() == QEvent::FocusIn) {
            emit focusGained();
            m_edit->selectAll();
            onTextEdited(m_edit->text());
        } else if (ev->type() == QEvent::FocusOut) {
            // ne pas fermer si on clic une suggestion
            QWidget *w = QApplication::widgetAt(QCursor::pos());
            if (w != m_popup && !m_popup->isAncestorOf(w)) {
                hideSuggestions();
                emit focusLost();
            }
        } else if (ev->type() == QEvent::KeyPress) {
            QKeyEvent *ke = static_cast<QKeyEvent *>(ev);
            if (m_popup->isVisible() && m_popup->count() > 0) {
                const int cur = m_popup->currentRow();
                if (ke->key() == Qt::Key_Down) {
                    m_popup->setCurrentRow(cur < 0 ? 0 : qMin(cur + 1, m_popup->count() - 1));
                    return true;
                }
                if (ke->key() == Qt::Key_Up) {
                    const int r = cur <= 0 ? m_popup->count() - 1 : cur - 1;
                    m_popup->setCurrentRow(r);
                    return true;
                }
                if (ke->key() == Qt::Key_Escape) {
                    hideSuggestions();
                    m_edit->clearFocus();
                    return true;
                }
            }
            if (ke->key() == Qt::Key_Escape) {
                m_edit->clearFocus();
                return true;
            }
            if (ke->key() == Qt::Key_Tab && m_popup->isVisible() && m_popup->currentRow() >= 0) {
                activateRow(m_popup->currentRow());
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void OmniBox::onTextEdited(const QString &text)
{
    if (m_updating) return;
    m_token++;
    buildSuggestions(text);
    showPopup();
    if (m_search) m_search->requestSuggestions(text, m_token);
}

void OmniBox::buildSuggestions(const QString &prefix)
{
    m_popup->clear();
    m_suggestions.clear();
    const QString p = prefix.trimmed();
    if (p.length() < 1) { hideSuggestions(); return; }

    int added = 0;
    // 1) historique local : 0 octet de data
    if (m_history) {
        const QVector<HistoryEntry> hits = m_history->search(p, 5);
        for (const HistoryEntry &h : hits) {
            if (h.url.startsWith(QLatin1String("about:"))) continue;
            QListWidgetItem *it = new QListWidgetItem(Icons::history().pixmap(15, 15), h.title.isEmpty() ? h.url : h.title);
            it->setData(kRoleUrl, h.url);
            it->setData(kRoleIsSearch, false);
            it->setData(kRoleSub, h.url);
            it->setToolTip(h.url);
            m_popup->addItem(it);
            m_suggestions.append({ h.title.isEmpty() ? h.url : h.title, h.url });
            if (++added >= 5) break;
        }
    }
    // Separateur seulement si des suggestions reseau peuvent encore arriver
    if (added > 0 && m_search && m_search->remoteSuggestions()) {
        QListWidgetItem *sep = new QListWidgetItem(tr("Suggestions du Web"));
        sep->setFlags(Qt::NoItemFlags);
        QFont f = sep->font(); f.setBold(true); f.setPointSizeF(8.0);
        sep->setFont(f);
        sep->setForeground(QColor(0x55, 0x59, 0x78));
        m_popup->addItem(sep);
    }
}

void OmniBox::onSuggestionsReady(const QString &text, const QStringList &items, quint64 token)
{
    Q_UNUSED(text)
    // Reponse obsolete (la frappe a continue) : ignoree.
    if (token != m_token) return;
    if (items.isEmpty()) { if (m_popup->count() == 0) hideSuggestions(); return; }
    // On retire le separateur (derniere ligne) avant d'ajouter les propositions
    while (m_popup->count() > 0) {
        QListWidgetItem *last = m_popup->item(m_popup->count() - 1);
        if (last->flags() & Qt::ItemIsEnabled) break;
        delete m_popup->takeItem(m_popup->count() - 1);
    }
    for (const QString &s : items) {
        QListWidgetItem *it = new QListWidgetItem(Icons::search().pixmap(15, 15), s);
        it->setData(kRoleIsSearch, true);
        it->setData(kRoleSub, s);
        it->setToolTip(tr("Rechercher : %1").arg(s));
        m_popup->addItem(it);
        m_suggestions.append({ s, QString() });
    }
    if (m_popup->count() > 0) showPopup();
    else hideSuggestions();
}

void OmniBox::showPopup()
{
    if (m_popup->count() == 0) { hideSuggestions(); return; }
    QWidget *top = window();
    const QPoint tl = mapToGlobal(QPoint(0, height() + 2));
    const QPoint p = top->mapFromGlobal(tl);
    int rows = qMin(m_popup->count(), kMaxVisible);
    const int rowH = m_popup->sizeHintForRow(0) > 0 ? m_popup->sizeHintForRow(0) : 26;
    m_popup->setGeometry(p.x(), p.y(), width(), rows * rowH + 10);
    m_popup->raise();
    m_popup->show();
    m_popupActive = true;
}

void OmniBox::dismissSuggestions()
{
    hideSuggestions();
    if (m_search) m_search->cancelSuggestions();
    m_popup->clear();
    m_suggestions.clear();
}

void OmniBox::hideSuggestions()
{
    if (m_popup) m_popup->hide();
    m_popupActive = false;
}

void OmniBox::activateRow(int row)
{
    if (row < 0 || row >= m_popup->count()) return;
    QListWidgetItem *it = m_popup->item(row);
    if (!it || !(it->flags() & Qt::ItemIsEnabled)) return;
    const bool isSearch = it->data(kRoleIsSearch).toBool();
    const QString url = it->data(kRoleUrl).toString();
    const QString sub = it->data(kRoleSub).toString();
    hideSuggestions();
    m_edit->clearFocus();
    if (isSearch) {
        m_updating = true;
        m_edit->setText(sub);
        m_updating = false;
        if (m_search) emit urlSubmitted(m_search->resolve(sub));
    } else {
        emit urlSubmitted(QUrl(url));
    }
}

void OmniBox::onSuggestionActivated()
{
    activateRow(m_popup->currentRow());
}

void OmniBox::onReturnPressed()
{
    if (m_popup->isVisible() && m_popup->currentRow() >= 0) {
        QListWidgetItem *it = m_popup->currentItem();
        if (it && (it->flags() & Qt::ItemIsEnabled)) { activateRow(m_popup->currentRow()); return; }
    }
    const QString t = m_edit->text().trimmed();
    if (t.isEmpty()) return;
    hideSuggestions();
    m_edit->clearFocus();
    if (m_search) emit urlSubmitted(m_search->resolve(t));
}

void OmniBox::onEditFocusOut()
{
    hideSuggestions();
}
