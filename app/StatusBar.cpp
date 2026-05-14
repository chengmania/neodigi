#include "StatusBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QProgressBar>
#include <QFrame>

static QLabel* indicator(const QString& text, bool active)
{
    auto* lbl = new QLabel(text);
    lbl->setStyleSheet(active
        ? "color: #4ec9b0;"
        : "color: #6a6a6a;");
    return lbl;
}

StatusBar::StatusBar(QWidget* parent) : QWidget(parent)
{
    setObjectName("StatusBar");
    setFixedHeight(28);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(8);

    m_flrigIndicator = indicator("○fldigi", false);
    m_afcIndicator   = indicator("●AFC",   true);
    m_rvIndicator    = indicator("○RV",    false);

    layout->addWidget(m_flrigIndicator);
    layout->addWidget(m_afcIndicator);
    layout->addWidget(m_rvIndicator);

    auto* sep1 = new QFrame; sep1->setFrameShape(QFrame::VLine); layout->addWidget(sep1);

    auto* sqlLabel = new QLabel("SQL");
    sqlLabel->setObjectName("dimLabel");
    layout->addWidget(sqlLabel);

    m_sqlSlider = new QSlider(Qt::Horizontal);
    m_sqlSlider->setRange(0, 100);
    m_sqlSlider->setValue(35);
    m_sqlSlider->setFixedWidth(80);
    layout->addWidget(m_sqlSlider);

    m_sqlValue = new QLabel("35");
    m_sqlValue->setFixedWidth(24);
    layout->addWidget(m_sqlValue);

    connect(m_sqlSlider, &QSlider::valueChanged, this, [this](int v) {
        m_sqlValue->setText(QString::number(v));
        emit sqlLevelChanged(v);
    });

    // Signal meter
    m_signalMeter = new QProgressBar;
    m_signalMeter->setObjectName("SignalMeter");
    m_signalMeter->setRange(0, 100);
    m_signalMeter->setValue(0);
    m_signalMeter->setTextVisible(false);
    m_signalMeter->setFixedWidth(50);
    m_signalMeter->setMaximumHeight(14);
    layout->addWidget(m_signalMeter);

    auto* sep2 = new QFrame; sep2->setFrameShape(QFrame::VLine); layout->addWidget(sep2);

    m_modemLabel = new QLabel("BPSK-31");
    m_modemLabel->setObjectName("modemLabel");
    layout->addWidget(m_modemLabel);

    auto* sep3 = new QFrame; sep3->setFrameShape(QFrame::VLine); layout->addWidget(sep3);

    m_trxLabel = new QLabel("RX");
    m_trxLabel->setObjectName("trxLabel");
    layout->addWidget(m_trxLabel);

    layout->addStretch();
}

void StatusBar::setFldigiConnected(bool connected)
{
    m_flrigIndicator->setText(connected ? "●fldigi" : "○fldigi");
    m_flrigIndicator->setStyleSheet(connected ? "color: #4ec9b0;" : "color: #6a6a6a;");
}

void StatusBar::setAfc(bool enabled)
{
    m_afcIndicator->setText(enabled ? "●AFC" : "○AFC");
    m_afcIndicator->setStyleSheet(enabled ? "color: #4ec9b0;" : "color: #6a6a6a;");
}

void StatusBar::setReverse(bool enabled)
{
    m_rvIndicator->setText(enabled ? "●RV" : "○RV");
    m_rvIndicator->setStyleSheet(enabled ? "color: #4ec9b0;" : "color: #6a6a6a;");
}

void StatusBar::setSqlLevel(int level)
{
    m_sqlSlider->setValue(level);
}

void StatusBar::setModemName(const QString& name)
{
    m_modemLabel->setText(name);
}

void StatusBar::setTrxState(const QString& state)
{
    m_trxLabel->setText(state);
    m_trxLabel->setStyleSheet(state == "TX" ? "color: #f44747; font-weight: bold;"
                                            : "color: #4ec9b0;");
}

void StatusBar::setSignalLevel(int pct)
{
    m_signalMeter->setValue(pct);
}
