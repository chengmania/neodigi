#include "SettingsDrawer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QPropertyAnimation>
#include <QFrame>
#include <QMessageBox>

SettingsDrawer::SettingsDrawer(QWidget* parent)
    : QWidget(parent)
    , m_open(false)
    , m_anim(nullptr)
{
    setObjectName("SettingsDrawer");
    setFixedWidth(220);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // Header row
    auto* header = new QHBoxLayout;
    auto* title = new QLabel("SETTINGS");
    title->setObjectName("sectionLabel");
    m_closeBtn = new QPushButton("✕");
    m_closeBtn->setFixedSize(24, 24);
    m_closeBtn->setFlat(true);
    header->addWidget(title);
    header->addStretch();
    header->addWidget(m_closeBtn);
    layout->addLayout(header);

    auto* sep = new QFrame; sep->setFrameShape(QFrame::HLine); layout->addWidget(sep);

    // Theme toggle
    auto* themeRow = new QHBoxLayout;
    themeRow->addWidget(new QLabel("Theme"));
    m_themeBtn = new QPushButton("◑ Dark");
    m_themeBtn->setCheckable(true);
    m_themeBtn->setChecked(true);
    m_themeBtn->setFixedWidth(72);
    themeRow->addStretch();
    themeRow->addWidget(m_themeBtn);
    layout->addLayout(themeRow);

    // Sidebar visibility
    auto* sbRow = new QHBoxLayout;
    sbRow->addWidget(new QLabel("Sidebar"));
    m_sidebarCheck = new QCheckBox;
    m_sidebarCheck->setChecked(true);
    sbRow->addStretch();
    sbRow->addWidget(m_sidebarCheck);
    layout->addLayout(sbRow);

    // Waterfall speed
    auto* wfRow = new QHBoxLayout;
    wfRow->addWidget(new QLabel("WF Speed"));
    m_wfSpeedCombo = new QComboBox;
    m_wfSpeedCombo->addItems({"Slow", "Normal", "Fast"});
    m_wfSpeedCombo->setCurrentIndex(1);
    m_wfSpeedCombo->setFixedWidth(80);
    wfRow->addStretch();
    wfRow->addWidget(m_wfSpeedCombo);
    layout->addLayout(wfRow);

    auto* sep2 = new QFrame; sep2->setFrameShape(QFrame::HLine); layout->addWidget(sep2);

    m_rigBtn   = new QPushButton("Configure rig");
    m_audioBtn = new QPushButton("Audio devices");
    m_aboutBtn = new QPushButton("About neodigi");
    layout->addWidget(m_rigBtn);
    layout->addWidget(m_audioBtn);
    layout->addStretch();
    layout->addWidget(m_aboutBtn);

    // Connections
    connect(m_closeBtn, &QPushButton::clicked, this, &SettingsDrawer::close);

    connect(m_themeBtn, &QPushButton::toggled, this, [this](bool dark) {
        m_themeBtn->setText(dark ? "◑ Dark" : "◑ Light");
        emit themeChanged(dark);
    });

    connect(m_sidebarCheck, &QCheckBox::toggled, this, &SettingsDrawer::sidebarVisibilityChanged);

    connect(m_wfSpeedCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        static const int fps[] = {10, 30, 60};
        emit wfSpeedChanged(fps[idx]);
    });

    connect(m_rigBtn,   &QPushButton::clicked, this, &SettingsDrawer::configureRigRequested);
    connect(m_audioBtn, &QPushButton::clicked, this, &SettingsDrawer::audioDevicesRequested);

    connect(m_aboutBtn, &QPushButton::clicked, this, [this]() {
        QMessageBox::about(this, "About neodigi",
            "neodigi v0.1.0\n"
            "Modern Qt6 UI for fldigi modem\n\n"
            "Author: chengmania KC3SMW\n"
            "License: GPL v3");
    });

    hide();
}

bool SettingsDrawer::isDarkTheme() const    { return m_themeBtn->isChecked(); }
bool SettingsDrawer::isSidebarVisible() const { return m_sidebarCheck->isChecked(); }

void SettingsDrawer::open()
{
    if (m_open) return;
    m_open = true;
    reposition();
    show();
    raise();
}

void SettingsDrawer::close()
{
    if (!m_open) return;
    m_open = false;
    hide();
}

void SettingsDrawer::toggle()
{
    if (m_open) close(); else open();
}

void SettingsDrawer::reposition()
{
    if (!parentWidget()) return;
    int x = parentWidget()->width() - width();
    int y = 0;
    move(x, y);
    resize(width(), parentWidget()->height());
}
