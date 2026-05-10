#pragma once

#include <QWidget>

class QLabel;
class QSlider;
class QProgressBar;

class StatusBar : public QWidget {
    Q_OBJECT

public:
    explicit StatusBar(QWidget* parent = nullptr);

    void setFlrigConnected(bool connected);
    void setAfc(bool enabled);
    void setReverse(bool enabled);
    void setSqlLevel(int level);
    void setModemName(const QString& name);
    void setTrxState(const QString& state);
    void setSignalLevel(int pct);

signals:
    void sqlLevelChanged(int level);

private:
    QLabel*       m_flrigIndicator;
    QLabel*       m_afcIndicator;
    QLabel*       m_rvIndicator;
    QSlider*      m_sqlSlider;
    QLabel*       m_sqlValue;
    QProgressBar* m_signalMeter;
    QLabel*       m_modemLabel;
    QLabel*       m_trxLabel;
};
