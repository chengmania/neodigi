#pragma once

#include <QWidget>

class QPropertyAnimation;
class QCheckBox;
class QComboBox;
class QPushButton;

class SettingsDrawer : public QWidget {
    Q_OBJECT

public:
    explicit SettingsDrawer(QWidget* parent = nullptr);

    bool isDarkTheme() const;
    bool isSidebarVisible() const;

public slots:
    void open();
    void close();
    void toggle();

signals:
    void themeChanged(bool dark);
    void sidebarVisibilityChanged(bool visible);
    void wfSpeedChanged(int fps);
    void configureRigRequested();
    void audioDevicesRequested();

private:
    bool  m_open;
    QPropertyAnimation* m_anim;
    QPushButton* m_closeBtn;
    QPushButton* m_themeBtn;
    QCheckBox*   m_sidebarCheck;
    QComboBox*   m_wfSpeedCombo;
    QPushButton* m_rigBtn;
    QPushButton* m_audioBtn;
    QPushButton* m_aboutBtn;

    void reposition();
};
