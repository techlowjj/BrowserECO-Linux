#pragma once
#include <QFrame>
#include <QVector>

class QToolButton;
class QSlider;
class QLabel;
class QCheckBox;
class EcoInterceptor;
class SearchEngineManager;

/*
 * EcoPanel — popover "Data saver" :
 *   - 3 bascules eco (data saver / images / ultra)
 *   - curseur qualite WebP
 *   - compteur par categorie + estimation des octets economises
 *   - selection du moteur de recherche (meme liste que le badge de l'omnibox)
 *   - raccourcis utiles
 */
class EcoPanel : public QFrame {
    Q_OBJECT
public:
    explicit EcoPanel(QWidget *parent = nullptr);

    void setInterceptor(EcoInterceptor *interceptor);
    void setSearchManager(SearchEngineManager *s);

    // Synchronise l'etat des bascules avec l'etat reel
    void setDataSaverChecked(bool on);
    void setImagesChecked(bool on);
    void setUltraChecked(bool on);
    void setFaviconsChecked(bool on);
    void setQualityValue(int v);

    // Rafraichit compteurs / etats
    void refresh();

signals:
    void dataSaverToggled(bool on);
    void imagesToggled(bool on);
    void ultraToggled(bool on);
    void faviconsToggled(bool on);
    void autoFallbackToggled(bool on);
    void remoteSuggestToggled(bool on);
    void qualityChanged(int value);
    void engineChanged(const QString &engineId);
    void searxInstanceRequested();
    void providerRequested();
    void openHistoryRequested();
    void clearCacheRequested();
    void shortcutsRequested();
    void aboutRequested();

private slots:
    void onSliderMoved(int v);
    void emitEngine(int actionId);

private:
    void buildUi();

    EcoInterceptor *m_interceptor = nullptr;
    SearchEngineManager *m_search = nullptr;

    QToolButton *m_dataSaver = nullptr;
    QToolButton *m_images = nullptr;
    QToolButton *m_ultra = nullptr;
    QToolButton *m_favicons = nullptr;
    QToolButton *m_autoFallback = nullptr;
    QToolButton *m_remoteSuggest = nullptr;
    QToolButton *m_searchBtn = nullptr;
    QSlider *m_quality = nullptr;
    QLabel *m_qualityValue = nullptr;
    QLabel *m_saved = nullptr;
    QLabel *m_ratio = nullptr;
    QVector<QLabel *> m_catLabels;
    QVector<QWidget *> m_rows;
};
