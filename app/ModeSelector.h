#pragma once

#include <QDialog>
#include <QStringList>
#include <QMap>

class QLineEdit;
class QVBoxLayout;
class QGridLayout;
class QWidget;
class QPushButton;
class QComboBox;
class QLabel;
class QFrame;

struct ModeEntry {
    QString fullName;
    QString family;
    QString speed;
};

class ModeSelector : public QDialog {
    Q_OBJECT

public:
    explicit ModeSelector(const QStringList& allModes,
                          const QString& currentMode,
                          QWidget* parent = nullptr);

    QString selectedMode() const { return m_selected; }

signals:
    void modeSelected(const QString& modeName);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void parseModes(const QStringList& allModes);
    void buildUi();
    void rebuildGrid(const QString& filter);
    void rebuildFavorites();
    void showFamilyPanel(const QString& family);
    void hideFamilyPanel();
    QPushButton* makeFamilyCard(const QString& family, int count);
    void applyFilter(const QString& filter);
    void addFavorite(const QString& modeName);
    void removeFavorite(const QString& modeName);
    int  gridColumns() const;

    QLineEdit*   m_search = nullptr;
    QWidget*     m_favSection = nullptr;
    QVBoxLayout* m_favLayout = nullptr;
    QWidget*     m_favPills = nullptr;
    QGridLayout* m_grid = nullptr;
    QWidget*     m_gridWidget = nullptr;

    QWidget*     m_panel = nullptr;
    QLabel*      m_panelFamily = nullptr;
    QComboBox*   m_panelSpeed = nullptr;
    QPushButton* m_panelFav = nullptr;
    QPushButton* m_panelSelect = nullptr;
    QLabel*      m_panelAllSpeeds = nullptr;

    QMap<QString, QList<ModeEntry>> m_families;
    QStringList m_familyNames;
    QStringList m_favorites;
    QString     m_currentMode;
    QString     m_selected;
    QString     m_selectedFamily;
    QPushButton* m_selectedCard = nullptr;
};
