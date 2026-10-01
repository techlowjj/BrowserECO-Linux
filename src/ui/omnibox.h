#pragma once
#include <QWidget>
#include <QString>
#include <QUrl>
#include <QListWidget>
#include <QVector>

class QLineEdit;
class QToolButton;
class QLabel;
class HistoryManager;
class SearchEngineManager;

/*
 * OmniBox — barre d'adresse "pro" :
 *   [favicon/verrou]  texte de recherche ou URL        [moteur ▾]
 *
 * + liste de suggestions (historique local 0 octet + API DDG ~100 octets)
 * + icone de securite (cadenas HTTPS / avertissement HTTP)
 * + menu de selection du moteur avec poids data affiche
 */
class OmniBox : public QWidget {
    Q_OBJECT
public:
    explicit OmniBox(QWidget *parent = nullptr);
    ~OmniBox() override;

    void setSearchManager(SearchEngineManager *m);
    void setHistoryManager(HistoryManager *h);

    QString text() const;
    void setText(const QString &t);
    void selectAll();
    QLineEdit *lineEdit() const { return m_edit; }

    // Affiche l'icone de securite + favicon pour l'URL courante
    void setCurrentUrl(const QUrl &url);
    // Change le libelle du badge moteur
    void refreshEngineBadge();
    // Ferme la liste de suggestions (changement d'onglet, navigation...)
    void dismissSuggestions();
    // La liste de suggestions est-elle ouverte ? (raccourci Échap)
    bool suggestionsVisible() const { return m_popupActive && m_popup && m_popup->isVisible(); }
    // Force l'affichage de l'URL courante meme si la zone a du texte et le
    // focus : sans cela, changer d'onglet laissait l'URL du precedent onglet.
    void setCurrentUrlForced(const QUrl &url);

signals:
    void urlSubmitted(const QUrl &url);
    void focusGained();
    void focusLost();
    void engineMenuRequested(QPoint globalPos);

protected:
    void focusInEvent(QFocusEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
    void onTextEdited(const QString &text);
    void onReturnPressed();
    void onSuggestionsReady(const QString &text, const QStringList &items, quint64 token);
    void onSuggestionActivated();
    void hideSuggestions();
    void onEditFocusOut();

private:
    void buildSuggestions(const QString &prefix);
    void activateRow(int row);
    void showPopup();

    QLineEdit *m_edit = nullptr;
    QLabel *m_secIcon = nullptr;
    QToolButton *m_engineBtn = nullptr;
    QListWidget *m_popup = nullptr;
    QWidget *m_popupHost = nullptr;

    SearchEngineManager *m_search = nullptr;
    HistoryManager *m_history = nullptr;

    quint64 m_token = 0;
    bool m_updating = false;
    bool m_popupActive = false;
    QVector<QPair<QString, QString>> m_suggestions; // (label, url vide = recherche)
};
