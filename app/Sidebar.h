#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;
class QProgressBar;
class QSlider;

class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);

    QString callsign() const;
    QString name()     const;
    QString qth()      const;
    QString grid()     const;
    QString rst()      const;

    // Populated from Station Information settings
    void setCallsign(const QString& s);
    void setOperatorName(const QString& s);
    void setQth(const QString& s);
    void setGrid(const QString& s);

    void setAudioInputLevel(int pct);

public slots:
    void setAfc(bool enabled);
    void setSql(bool enabled);
    void setReverse(bool enabled);
    void setLock(bool enabled);
    void setTune(bool enabled);
    void setTxId(bool enabled);
    void setRxId(bool enabled);
    void setSpot(bool enabled);

    void setNoiseLevel(int pct);

signals:
    void afcToggled(bool enabled);
    void sqlToggled(bool enabled);
    void reverseToggled(bool enabled);
    void lockToggled(bool enabled);
    void tuneToggled(bool enabled);
    void spotToggled(bool enabled);
    void sqlLevelChanged(int level);
    void rxIdToggled(bool enabled);
    void txIdToggled(bool enabled);
    void hideRequested();
    void txLevelChanged(int level);
    void lookupRequested();
    void autoLookupRequested(const QString& call);
    void logQsoRequested();
    void clearQsoRequested();

private:
    QLineEdit*   m_callEdit;
    QLineEdit*   m_nameEdit;
    QLineEdit*   m_qthEdit;
    QLineEdit*   m_gridEdit;
    QLineEdit*   m_rstEdit;
    QPushButton* m_afcBtn;
    QPushButton* m_sqlBtn;
    QPushButton* m_rvBtn;
    QPushButton* m_lkBtn;
    QPushButton* m_tuneBtn;
    QPushButton* m_spotBtn;
    QPushButton* m_rxIdBtn;
    QPushButton* m_txIdBtn;
    QPushButton* m_hideBtn;
    QProgressBar* m_inMeter;
    QProgressBar* m_outMeter;
    QSlider*      m_txLevelSlider;
    QProgressBar* m_noiseBar;
    QSlider*      m_sqlSidebarSlider;

    QLabel* m_afcLed;
    QLabel* m_sqlLed;
    QLabel* m_rvLed;
    QLabel* m_lkLed;
    QLabel* m_tuneLed;
    QLabel* m_spotLed;
    QLabel* m_rxIdLed;
    QLabel* m_txIdLed;
};
