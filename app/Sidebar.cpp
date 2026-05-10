#include "Sidebar.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QProgressBar>
#include <QSlider>
#include <QSignalBlocker>

static void updateLed(QLabel* led, bool on)
{
    if (on)
        led->setStyleSheet(
            "background:#4ec9b0; border-radius:4px;"
            " border:1px solid #6de8d0;");
    else
        led->setStyleSheet(
            "background:#1a3a32; border-radius:4px;"
            " border:1px solid #2a6b5e;");
}

static QLabel* makeLed(QLabel*& out)
{
    out = new QLabel;
    out->setFixedSize(8, 8);
    updateLed(out, false);
    return out;
}

static QFrame* hRule()
{
    auto* f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    f->setFrameShadow(QFrame::Sunken);
    return f;
}

static QPushButton* toggleBtn(const QString& text)
{
    auto* btn = new QPushButton(text);
    btn->setCheckable(true);
    btn->setFixedSize(52, 26);
    return btn;
}

Sidebar::Sidebar(QWidget* parent) : QWidget(parent)
{
    setObjectName("Sidebar");
    setMinimumWidth(120);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    // QSO Info section
    auto* qsoLabel = new QLabel("QSO INFO");
    qsoLabel->setObjectName("sectionLabel");
    layout->addWidget(qsoLabel);
    layout->addWidget(hRule());

    auto* qsoGrid = new QGridLayout;
    qsoGrid->setSpacing(4);
    auto addField = [&](const QString& lbl, QLineEdit*& edit, int row) {
        qsoGrid->addWidget(new QLabel(lbl), row, 0);
        edit = new QLineEdit;
        qsoGrid->addWidget(edit, row, 1);
    };
    addField("Call", m_callEdit, 0);
    addField("Name", m_nameEdit, 1);
    addField("QTH",  m_qthEdit,  2);
    addField("Grid", m_gridEdit, 3);
    addField("RST",  m_rstEdit,  4);
    layout->addLayout(qsoGrid);
    layout->addSpacing(4);

    // QSO action buttons: Lookup / Log / Clear
    auto* qsoActRow = new QHBoxLayout;
    auto* lookupBtn = new QPushButton("Lookup");
    auto* logBtn    = new QPushButton("Log");
    auto* clearBtn  = new QPushButton("Clear");
    lookupBtn->setFixedHeight(22);
    logBtn->setFixedHeight(22);
    clearBtn->setFixedHeight(22);
    qsoActRow->addWidget(lookupBtn);
    qsoActRow->addWidget(logBtn);
    qsoActRow->addWidget(clearBtn);
    layout->addLayout(qsoActRow);
    layout->addSpacing(6);

    // Tools section
    auto* toolLabel = new QLabel("TOOLS");
    toolLabel->setObjectName("sectionLabel");
    layout->addWidget(toolLabel);
    layout->addWidget(hRule());

    m_afcBtn  = toggleBtn("AFC");
    m_sqlBtn  = toggleBtn("SQL");
    m_rvBtn   = toggleBtn("RV");
    m_lkBtn   = toggleBtn("LK");
    m_spotBtn = toggleBtn("Spot");
    m_rxIdBtn = toggleBtn("RxID");
    m_txIdBtn = toggleBtn("TxID");

    // Tune — full-width, bold, sits at the bottom of the tools list
    m_tuneBtn = new QPushButton("TUNE");
    m_tuneBtn->setCheckable(true);
    m_tuneBtn->setFixedHeight(26);
    {
        QFont f = m_tuneBtn->font();
        f.setBold(true);
        m_tuneBtn->setFont(f);
    }

    // QGridLayout guarantees pixel-perfect column alignment for all 8 buttons.
    // Columns: 0=LED  1=btn  2=LED  3=btn
    auto* toolGrid = new QGridLayout;
    toolGrid->setHorizontalSpacing(3);
    toolGrid->setVerticalSpacing(4);
    toolGrid->setContentsMargins(0, 0, 0, 0);

    // Rows 0-2: paired buttons
    int r = 0;
    auto addRow = [&](QLabel*& led1, QPushButton* a, QLabel*& led2, QPushButton* b) {
        toolGrid->addWidget(makeLed(led1), r, 0, Qt::AlignVCenter);
        toolGrid->addWidget(a,             r, 1);
        toolGrid->addWidget(makeLed(led2), r, 2, Qt::AlignVCenter);
        toolGrid->addWidget(b,             r, 3);
        ++r;
    };
    addRow(m_rxIdLed, m_rxIdBtn, m_txIdLed, m_txIdBtn);
    addRow(m_spotLed, m_spotBtn, m_sqlLed,  m_sqlBtn);
    addRow(m_afcLed,  m_afcBtn,  m_lkLed,   m_lkBtn);

    // Row 3: RV alone — cols 2 & 3 intentionally empty (preserves grid alignment)
    toolGrid->addWidget(makeLed(m_rvLed), r, 0, Qt::AlignVCenter);
    toolGrid->addWidget(m_rvBtn,          r, 1);
    ++r;

    // Row 4: TUNE — LED in col 0, button spans cols 1-3 (two-button width)
    toolGrid->addWidget(makeLed(m_tuneLed), r, 0, Qt::AlignVCenter);
    toolGrid->addWidget(m_tuneBtn,          r, 1, 1, 3);

    layout->addLayout(toolGrid);

    layout->addSpacing(6);

    // Audio section
    auto* audioLabel = new QLabel("AUDIO");
    audioLabel->setObjectName("sectionLabel");
    layout->addWidget(audioLabel);
    layout->addWidget(hRule());

    // In/Out level meters side by side
    auto* metersRow = new QHBoxLayout;
    metersRow->setSpacing(6);

    auto makeMeter = [](const QString& labelText, QProgressBar*& meter) -> QWidget* {
        auto* col = new QWidget;
        auto* vb  = new QVBoxLayout(col);
        vb->setContentsMargins(0, 0, 0, 0);
        vb->setSpacing(2);
        meter = new QProgressBar;
        meter->setObjectName("LevelMeter");
        meter->setRange(0, 100);
        meter->setValue(0);
        meter->setTextVisible(false);
        meter->setOrientation(Qt::Vertical);
        meter->setFixedSize(16, 60);
        auto* lbl = new QLabel(labelText);
        lbl->setObjectName("dimLabel");
        lbl->setAlignment(Qt::AlignHCenter);
        vb->addWidget(meter, 0, Qt::AlignHCenter);
        vb->addWidget(lbl,   0, Qt::AlignHCenter);
        return col;
    };

    auto* inCol  = makeMeter("In",  m_inMeter);
    auto* outCol = makeMeter("Out", m_outMeter);
    metersRow->addStretch();
    metersRow->addWidget(inCol);
    metersRow->addWidget(outCol);
    metersRow->addStretch();
    layout->addLayout(metersRow);
    layout->addSpacing(4);

    // TX level slider
    auto* txLvlLabel = new QLabel("TX Level");
    txLvlLabel->setObjectName("dimLabel");
    layout->addWidget(txLvlLabel);
    m_txLevelSlider = new QSlider(Qt::Horizontal);
    m_txLevelSlider->setRange(0, 100);
    m_txLevelSlider->setValue(80);
    layout->addWidget(m_txLevelSlider);
    layout->addSpacing(6);

    // Squelch section: noise floor indicator + SQL threshold slider
    auto* squelchLabel = new QLabel("SQUELCH");
    squelchLabel->setObjectName("sectionLabel");
    layout->addWidget(squelchLabel);
    layout->addWidget(hRule());

    auto* noiseRowLabel = new QLabel("Noise Floor");
    noiseRowLabel->setObjectName("dimLabel");
    layout->addWidget(noiseRowLabel);

    m_noiseBar = new QProgressBar;
    m_noiseBar->setObjectName("SignalMeter");
    m_noiseBar->setRange(0, 100);
    m_noiseBar->setValue(0);
    m_noiseBar->setTextVisible(false);
    m_noiseBar->setMaximumHeight(8);
    layout->addWidget(m_noiseBar);
    layout->addSpacing(2);

    auto* sqlThreshLabel = new QLabel("SQL Threshold");
    sqlThreshLabel->setObjectName("dimLabel");
    layout->addWidget(sqlThreshLabel);

    m_sqlSidebarSlider = new QSlider(Qt::Horizontal);
    m_sqlSidebarSlider->setRange(0, 100);
    m_sqlSidebarSlider->setValue(35);
    layout->addWidget(m_sqlSidebarSlider);

    layout->addStretch();
    layout->addWidget(hRule());

    m_hideBtn = new QPushButton("◀ Hide");
    layout->addWidget(m_hideBtn);

    // Signals — toggle buttons (also update LEDs)
    connect(m_afcBtn,  &QPushButton::toggled, this, [this](bool on){
        updateLed(m_afcLed, on); emit afcToggled(on); });
    connect(m_sqlBtn,  &QPushButton::toggled, this, [this](bool on){
        updateLed(m_sqlLed, on); emit sqlToggled(on); });
    connect(m_rvBtn,   &QPushButton::toggled, this, [this](bool on){
        updateLed(m_rvLed, on); emit reverseToggled(on); });
    connect(m_lkBtn,   &QPushButton::toggled, this, [this](bool on){
        updateLed(m_lkLed, on); emit lockToggled(on); });
    connect(m_tuneBtn, &QPushButton::toggled, this, [this](bool on){
        updateLed(m_tuneLed, on); emit tuneToggled(on); });
    connect(m_spotBtn, &QPushButton::toggled, this, [this](bool on){
        updateLed(m_spotLed, on); emit spotToggled(on); });
    connect(m_rxIdBtn, &QPushButton::toggled, this, [this](bool on){
        updateLed(m_rxIdLed, on); emit rxIdToggled(on); });
    connect(m_txIdBtn, &QPushButton::toggled, this, [this](bool on){
        updateLed(m_txIdLed, on); emit txIdToggled(on); });
    connect(m_hideBtn, &QPushButton::clicked, this, &Sidebar::hideRequested);

    // Auto-lookup when tabbing out of the Call field
    connect(m_callEdit, &QLineEdit::editingFinished, this, [this]() {
        if (!m_callEdit->text().trimmed().isEmpty())
            emit lookupRequested();
    });

    // QSO action signals
    connect(lookupBtn, &QPushButton::clicked, this, &Sidebar::lookupRequested);
    connect(logBtn,    &QPushButton::clicked, this, &Sidebar::logQsoRequested);
    connect(clearBtn,  &QPushButton::clicked, this, [this]() {
        m_callEdit->clear();
        m_nameEdit->clear();
        m_qthEdit->clear();
        m_gridEdit->clear();
        m_rstEdit->clear();
        emit clearQsoRequested();
    });

    // TX level slider
    connect(m_txLevelSlider, &QSlider::valueChanged, this, &Sidebar::txLevelChanged);

    // SQL threshold slider
    connect(m_sqlSidebarSlider, &QSlider::valueChanged, this, [this](int v) {
        emit sqlLevelChanged(v);
    });
}

QString Sidebar::callsign() const { return m_callEdit->text(); }
QString Sidebar::name()     const { return m_nameEdit->text(); }
QString Sidebar::qth()      const { return m_qthEdit->text(); }
QString Sidebar::grid()     const { return m_gridEdit->text(); }
QString Sidebar::rst()      const { return m_rstEdit->text(); }

void Sidebar::setCallsign(const QString& s)      { m_callEdit->setText(s); }
void Sidebar::setOperatorName(const QString& s)  { m_nameEdit->setText(s); }
void Sidebar::setQth(const QString& s)           { m_qthEdit->setText(s); }
void Sidebar::setGrid(const QString& s)          { m_gridEdit->setText(s); }

void Sidebar::setAfc(bool enabled)
{
    QSignalBlocker b(m_afcBtn);
    m_afcBtn->setChecked(enabled);
    updateLed(m_afcLed, enabled);
}

void Sidebar::setSql(bool enabled)
{
    QSignalBlocker b(m_sqlBtn);
    m_sqlBtn->setChecked(enabled);
    updateLed(m_sqlLed, enabled);
}

void Sidebar::setReverse(bool enabled)
{
    QSignalBlocker b(m_rvBtn);
    m_rvBtn->setChecked(enabled);
    updateLed(m_rvLed, enabled);
}

void Sidebar::setLock(bool enabled)
{
    QSignalBlocker b(m_lkBtn);
    m_lkBtn->setChecked(enabled);
    updateLed(m_lkLed, enabled);
}

void Sidebar::setTune(bool enabled)
{
    QSignalBlocker b(m_tuneBtn);
    m_tuneBtn->setChecked(enabled);
    updateLed(m_tuneLed, enabled);
}

void Sidebar::setAudioInputLevel(int pct)
{
    m_inMeter->setValue(pct);
}

void Sidebar::setNoiseLevel(int pct)
{
    m_noiseBar->setValue(pct);
}

void Sidebar::setTxId(bool enabled)
{
    QSignalBlocker b(m_txIdBtn);
    m_txIdBtn->setChecked(enabled);
    updateLed(m_txIdLed, enabled);
}

void Sidebar::setRxId(bool enabled)
{
    QSignalBlocker b(m_rxIdBtn);
    m_rxIdBtn->setChecked(enabled);
    updateLed(m_rxIdLed, enabled);
}

void Sidebar::setSpot(bool enabled)
{
    QSignalBlocker b(m_spotBtn);
    m_spotBtn->setChecked(enabled);
    updateLed(m_spotLed, enabled);
}
