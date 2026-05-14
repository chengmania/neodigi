#include "ModeSelector.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QSettings>
#include <QRegularExpression>
#include <QMenu>
#include <QFrame>
#include <QComboBox>
#include <QResizeEvent>
#include <QKeyEvent>
#include <algorithm>

// ---------------------------------------------------------------------------
// Family parsing rules
// ---------------------------------------------------------------------------

struct ParseRule {
    QRegularExpression re;
    QString            family;
    int                speedGroup;
};

static const ParseRule kRules[] = {
    { QRegularExpression("^BPSK(\\d+)$",             QRegularExpression::CaseInsensitiveOption), "PSK",      1 },
    { QRegularExpression("^QPSK(\\d+)$",             QRegularExpression::CaseInsensitiveOption), "QPSK",     1 },
    { QRegularExpression("^8PSK(\\d+F?)$",            QRegularExpression::CaseInsensitiveOption), "8PSK",     1 },
    { QRegularExpression("^RTTY(?:-(\\d+))?$",       QRegularExpression::CaseInsensitiveOption), "RTTY",     1 },
    { QRegularExpression("^Cont(?:estia)?[- ](.+)$",  QRegularExpression::CaseInsensitiveOption), "Contestia",1 },
    { QRegularExpression("^OLIVIA[- ](.+)$",          QRegularExpression::CaseInsensitiveOption), "Olivia",   1 },
    { QRegularExpression("^DominoEX(\\d+)$",         QRegularExpression::CaseInsensitiveOption), "DominoEX", 1 },
    { QRegularExpression("^MT63[-]?(.+)$",            QRegularExpression::CaseInsensitiveOption), "MT63",     1 },
    { QRegularExpression("^THOR(\\d+)$",             QRegularExpression::CaseInsensitiveOption), "Thor",     1 },
    { QRegularExpression("^THROB(\\d+)$",            QRegularExpression::CaseInsensitiveOption), "Throb",    1 },
    { QRegularExpression("^(?:FELDHELL|FELD HELL)$", QRegularExpression::CaseInsensitiveOption), "Hell",     0 },
    { QRegularExpression("^FSK$",                    QRegularExpression::CaseInsensitiveOption), "Hell",     0 },
    { QRegularExpression("^Hellschreiber$",           QRegularExpression::CaseInsensitiveOption), "Hell",     0 },
    { QRegularExpression("^WEFAX[-]?(\\d+)$",        QRegularExpression::CaseInsensitiveOption), "WEFAX",    1 },
    { QRegularExpression("^MFSK(\\d+)$",             QRegularExpression::CaseInsensitiveOption), "MFSK",     1 },
    { QRegularExpression("^CW$",                     QRegularExpression::CaseInsensitiveOption), "CW",       0 },
    { QRegularExpression("^NULL$",                   QRegularExpression::CaseInsensitiveOption), "NULL",     0 },
    { QRegularExpression("^PSK(\\d+)$",              QRegularExpression::CaseInsensitiveOption), "PSK",      1 },
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QString canonicalMode(const QString& raw)
{
    QString u = raw.trimmed();
    if (u.startsWith("Olivia", Qt::CaseInsensitive))
        return u; // keep as-is
    if (u.startsWith("Contestia", Qt::CaseInsensitive))
        return u;
    return u.toUpper();
}

static bool parseMode(const QString& fullName, ModeEntry& out)
{
    out.fullName = fullName;
    const QString c = canonicalMode(fullName);

    for (const auto& rule : kRules) {
        auto m = rule.re.match(c);
        if (!m.hasMatch()) continue;
        out.family = rule.family;
        if (rule.speedGroup > 0)
            out.speed = m.captured(rule.speedGroup).trimmed();
        else
            out.speed = fullName;
        return true;
    }
    out.family = "Other";
    out.speed = fullName;
    return true;
}

// ---------------------------------------------------------------------------
// Styling helpers
// ---------------------------------------------------------------------------

static const char* kCardStyle = R"(
    QPushButton {
        background-color: #2d2d2d;
        border: 1px solid #3c3c3c;
        border-radius: 8px;
        color: #d4d4d4;
        font-size: 13px;
        font-weight: bold;
        min-width: 100px;
        min-height: 60px;
        padding: 4px 8px;
    }
    QPushButton:hover {
        background-color: #2a6b5e;
        border-color: #4ec9b0;
    }
)";

static const char* kCardSelectedStyle = R"(
    QPushButton {
        background-color: #4ec9b0;
        border: 2px solid #4ec9b0;
        border-radius: 8px;
        color: #1e1e1e;
        font-size: 13px;
        font-weight: bold;
        min-width: 100px;
        min-height: 60px;
        padding: 4px 8px;
    }
    QPushButton:hover {
        background-color: #4ec9b0;
    }
)";

static const char* kPillStyle = R"(
    QPushButton {
        background-color: #2a2520;
        color: #ce9178;
        border: 1px solid #ce9178;
        border-radius: 10px;
        padding: 2px 12px;
        font-size: 11px;
        min-height: 20px;
    }
    QPushButton:hover {
        background-color: #3a3020;
    }
)";

static const char* kSectionHeaderStyle = R"(
    color: #6a6a6a;
    font-size: 11px;
    font-weight: bold;
    letter-spacing: 1px;
    padding: 4px 0;
)";

static QPushButton* makeStarButton()
{
    auto* btn = new QPushButton(QString::fromUtf8("\u2606"));
    btn->setFixedSize(28, 28);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip("Add to favorites");
    btn->setStyleSheet(
        "QPushButton { background: transparent; color: #6a6a6a;"
        " border: 1px solid #3c3c3c; border-radius: 4px; font-size: 16px; }"
        "QPushButton:hover { background: #2a2520; color: #ce9178; border-color: #ce9178; }");
    return btn;
}

// ---------------------------------------------------------------------------
// ModeSelector
// ---------------------------------------------------------------------------

ModeSelector::ModeSelector(const QStringList& allModes,
                           const QString& currentMode,
                           QWidget* parent)
    : QDialog(parent)
    , m_currentMode(currentMode)
{
    setWindowTitle("Select Mode");
    setMinimumSize(480, 420);
    resize(560, 580);

    QSettings s("KC3SMW", "neodigi");
    m_favorites = s.value("modeSelector/favorites").toStringList();

    parseModes(allModes);
    buildUi();

    if (m_search)
        m_search->setFocus();
}

void ModeSelector::parseModes(const QStringList& allModes)
{
    m_families.clear();
    for (const auto& raw : allModes) {
        ModeEntry e;
        if (!parseMode(raw, e)) continue;
        m_families[e.family].append(e);
    }

    // Sort families alphabetically with "Other" last
    m_familyNames = m_families.keys();
    std::sort(m_familyNames.begin(), m_familyNames.end());
    if (m_familyNames.removeOne("Other"))
        m_familyNames.append("Other");

    // Sort speeds within each family: numeric then alpha
    for (auto it = m_families.begin(); it != m_families.end(); ++it) {
        auto& list = it.value();
        std::sort(list.begin(), list.end(), [](const ModeEntry& a, const ModeEntry& b) {
            if (a.speed.isEmpty() != b.speed.isEmpty())
                return a.speed.isEmpty(); // empty speeds last
            bool aOk, bOk;
            int ai = a.speed.toInt(&aOk);
            int bi = b.speed.toInt(&bOk);
            if (aOk && bOk) return ai < bi;
            return a.speed < b.speed;
        });
    }
}

// ---------------------------------------------------------------------------
// Ui construction
// ---------------------------------------------------------------------------

void ModeSelector::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(6);

    // Search bar
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QString::fromUtf8("\xF0\x9F\x94\x8D  Search modes\xE2\x80\xA6"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedHeight(32);
    outer->addWidget(m_search);

    // Scroll area
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll, 1);

    auto* scrollContent = new QWidget;
    auto* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 4, 0, 4);
    scrollLayout->setSpacing(4);

    // Favorites section
    m_favSection = new QWidget;
    m_favLayout = new QVBoxLayout(m_favSection);
    m_favLayout->setContentsMargins(0, 0, 0, 0);
    m_favLayout->setSpacing(2);
    auto* favHeader = new QLabel("FAVORITES");
    favHeader->setStyleSheet(kSectionHeaderStyle);
    m_favLayout->addWidget(favHeader);
    rebuildFavorites();
    scrollLayout->addWidget(m_favSection);

    // Separator
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #3c3c3c;");
    scrollLayout->addWidget(sep);

    // Modes header
    auto* modesHeader = new QLabel("MODES");
    modesHeader->setStyleSheet(kSectionHeaderStyle);
    scrollLayout->addWidget(modesHeader);

    // Family grid
    m_gridWidget = new QWidget;
    m_grid = new QGridLayout(m_gridWidget);
    m_grid->setContentsMargins(0, 4, 0, 4);
    m_grid->setSpacing(8);
    scrollLayout->addWidget(m_gridWidget);

    // Bottom panel (hidden by default)
    m_panel = new QWidget;
    m_panel->setVisible(false);
    m_panel->setStyleSheet("background: #252526; border: 1px solid #3c3c3c; border-radius: 6px;");
    auto* ph = new QHBoxLayout(m_panel);
    ph->setContentsMargins(12, 8, 12, 8);
    ph->setSpacing(8);

    m_panelFamily = new QLabel;
    m_panelFamily->setStyleSheet("font-size: 15px; font-weight: bold; color: #d4d4d4; min-width: 80px;");
    ph->addWidget(m_panelFamily);

    auto* speedBox = new QVBoxLayout;
    speedBox->setSpacing(2);
    auto* comboRow = new QHBoxLayout;
    comboRow->setSpacing(4);
    m_panelSpeed = new QComboBox;
    m_panelSpeed->setMinimumWidth(120);
    m_panelSpeed->setStyleSheet(
        "QComboBox { background: #2d2d2d; color: #d4d4d4; border: 1px solid #3c3c3c;"
        " border-radius: 4px; padding: 4px 8px; font-size: 13px; min-height: 24px; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background: #2d2d2d; color: #d4d4d4;"
        " selection-background-color: #2a6b5e; }");
    comboRow->addWidget(m_panelSpeed);
    m_panelAllSpeeds = new QLabel;
    m_panelAllSpeeds->setStyleSheet("color: #6a6a6a; font-size: 10px;");
    comboRow->addWidget(m_panelAllSpeeds);
    comboRow->addStretch();
    speedBox->addLayout(comboRow);
    ph->addLayout(speedBox, 1);

    auto* btnBox = new QHBoxLayout;
    btnBox->setSpacing(6);
    m_panelFav = makeStarButton();
    btnBox->addWidget(m_panelFav);

    m_panelSelect = new QPushButton("Select");
    m_panelSelect->setFixedHeight(28);
    m_panelSelect->setStyleSheet(
        "QPushButton { background: #2a6b5e; color: #4ec9b0; border: 1px solid #4ec9b0;"
        " border-radius: 4px; padding: 4px 16px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: #4ec9b0; color: #1e1e1e; }");
    btnBox->addWidget(m_panelSelect);
    ph->addLayout(btnBox);

    scrollLayout->addWidget(m_panel);
    scroll->setWidget(scrollContent);

    // Connections
    connect(m_search, &QLineEdit::textChanged, this, &ModeSelector::applyFilter);

    // Populate initial grid
    rebuildGrid(QString());

    // Panel connections
    connect(m_panelSelect, &QPushButton::clicked, this, [this]() {
        const QString family = m_selectedFamily;
        if (family.isEmpty()) return;

        const auto& entries = m_families[family];
        QString speed = m_panelSpeed->currentText();
        if (speed.isEmpty()) {
            if (!entries.isEmpty()) {
                m_selected = entries.first().fullName;
            }
        } else {
            for (const auto& e : entries) {
                if (e.speed == speed) {
                    m_selected = e.fullName;
                    break;
                }
            }
        }
        if (!m_selected.isEmpty()) {
            emit modeSelected(m_selected);
            accept();
        }
    });

    connect(m_panelFav, &QPushButton::clicked, this, [this]() {
        const QString speed = m_panelSpeed ? m_panelSpeed->currentText() : QString();
        const auto& entries = m_families[m_selectedFamily];
        for (const auto& e : entries) {
            if (speed.isEmpty() || e.speed == speed) {
                addFavorite(e.fullName);
                break;
            }
        }
    });

    connect(m_panelSpeed, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        // Check if already favorited
        const auto& entries = m_families[m_selectedFamily];
        for (const auto& e : entries) {
            if (e.speed == text) {
                const bool isFav = m_favorites.contains(e.fullName);
                m_panelFav->setText(isFav ? QString::fromUtf8("\u2605") : QString::fromUtf8("\u2606"));
                break;
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

int ModeSelector::gridColumns() const
{
    int w = m_gridWidget ? m_gridWidget->width() : width();
    if (w < 10) w = width();
    return std::max(2, w / 140);
}

void ModeSelector::rebuildGrid(const QString& filter)
{
    // Clear grid
    QLayoutItem* child;
    while ((child = m_grid->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    if (!m_selectedFamily.isEmpty()) {
        // Re-highlight selected card
        m_selectedCard = nullptr;
        hideFamilyPanel();
        m_selectedFamily.clear();
    }

    int cols = gridColumns();
    int row = 0, col = 0;

    for (const auto& family : m_familyNames) {
        const auto& entries = m_families[family];

        // Filter check
        if (!filter.isEmpty()) {
            bool match = family.contains(filter, Qt::CaseInsensitive);
            if (!match) {
                for (const auto& e : entries) {
                    if (e.fullName.contains(filter, Qt::CaseInsensitive)) {
                        match = true;
                        break;
                    }
                }
            }
            if (!match) continue;
        }

        auto* card = makeFamilyCard(family, entries.size());
        m_grid->addWidget(card, row, col);
        col++;
        if (col >= cols) { col = 0; row++; }
    }

    // Fill remaining space
    if (m_grid->itemAtPosition(row, col) == nullptr) {
        auto* spacer = new QWidget;
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_grid->addWidget(spacer, row, col);
    }
}

QPushButton* ModeSelector::makeFamilyCard(const QString& family, int count)
{
    auto* card = new QPushButton(family);
    card->setCursor(Qt::PointingHandCursor);
    card->setStyleSheet(kCardStyle);

    // Show count on card if > 1 variant
    if (count > 1) {
        card->setText(family + QString("\n%1 variants").arg(count));
    }

    connect(card, &QPushButton::clicked, this, [this, family, card]() {
        // Unselect previous card
        if (m_selectedCard && m_selectedCard != card) {
            m_selectedCard->setStyleSheet(kCardStyle);
        }
        card->setStyleSheet(kCardSelectedStyle);
        m_selectedCard = card;
        showFamilyPanel(family);
    });

    return card;
}

// ---------------------------------------------------------------------------
// Favorites
// ---------------------------------------------------------------------------

void ModeSelector::rebuildFavorites()
{
    // Remove old pills widget
    if (m_favPills) {
        m_favPills->deleteLater();
        m_favPills = nullptr;
    }

    m_favPills = new QWidget;
    auto* hl = new QHBoxLayout(m_favPills);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(4);

    for (const auto& fav : m_favorites) {
        auto* pill = new QPushButton(fav);
        pill->setStyleSheet(kPillStyle);
        pill->setCursor(Qt::PointingHandCursor);

        connect(pill, &QPushButton::clicked, this, [this, fav]() {
            m_selected = fav;
            emit modeSelected(fav);
            accept();
        });

        pill->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(pill, &QPushButton::customContextMenuRequested, this, [this, fav, pill](const QPoint& pos) {
            QMenu menu;
            auto* act = menu.addAction("Remove from favorites");
            connect(act, &QAction::triggered, this, [this, fav]() {
                removeFavorite(fav);
            });
            menu.exec(pill->mapToGlobal(pos));
        });

        hl->addWidget(pill);
    }
    hl->addStretch();

    m_favSection->layout()->addWidget(m_favPills);
    m_favSection->setVisible(true);
}

// ---------------------------------------------------------------------------
// Bottom panel
// ---------------------------------------------------------------------------

void ModeSelector::showFamilyPanel(const QString& family)
{
    m_selectedFamily = family;
    const auto& entries = m_families[family];

    m_panelFamily->setText(family);

    // Collect distinct speeds
    QStringList speeds;
    bool hasSpeeds = false;
    for (const auto& e : entries) {
        if (!e.speed.isEmpty()) {
            if (!speeds.contains(e.speed))
                speeds.append(e.speed);
            hasSpeeds = true;
        }
    }

    // All speeds hint
    QStringList allSpeeds;
    for (const auto& e : entries) {
        if (!e.speed.isEmpty() && !allSpeeds.contains(e.speed))
            allSpeeds.append(e.speed);
    }
    m_panelAllSpeeds->setText(allSpeeds.isEmpty() ? "" : allSpeeds.join(" / "));

    if (hasSpeeds) {
        m_panelSpeed->clear();
        m_panelSpeed->addItems(speeds);

        // Try to select current mode's speed
        for (const auto& e : entries) {
            if (e.fullName == m_currentMode && !e.speed.isEmpty()) {
                int idx = m_panelSpeed->findText(e.speed);
                if (idx >= 0) m_panelSpeed->setCurrentIndex(idx);
                break;
            }
        }

        m_panelSpeed->setVisible(true);
        m_panelAllSpeeds->setVisible(true);
    } else {
        m_panelSpeed->setVisible(false);
        m_panelAllSpeeds->setVisible(false);
    }

    // Check if current selection is favorited
    bool isFav = false;
    const QString currentSpeed = m_panelSpeed->isVisible() ? m_panelSpeed->currentText() : QString();
    for (const auto& e : entries) {
        if ((currentSpeed.isEmpty() && e.speed.isEmpty()) || e.speed == currentSpeed) {
            if (m_favorites.contains(e.fullName)) {
                isFav = true;
            }
            break;
        }
    }
    m_panelFav->setText(isFav ? QString::fromUtf8("\u2605") : QString::fromUtf8("\u2606"));

    m_panel->setVisible(true);
}

void ModeSelector::hideFamilyPanel()
{
    m_panel->setVisible(false);
    m_selectedFamily.clear();
}

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

void ModeSelector::applyFilter(const QString& filter)
{
    rebuildGrid(filter);
    // If panel is showing but family no longer matches, hide
    if (!m_selectedFamily.isEmpty()) {
        bool stillVisible = m_selectedFamily.contains(filter, Qt::CaseInsensitive);
        if (!stillVisible && !filter.isEmpty()) {
            const auto& entries = m_families[m_selectedFamily];
            for (const auto& e : entries) {
                if (e.fullName.contains(filter, Qt::CaseInsensitive)) {
                    stillVisible = true;
                    break;
                }
            }
        }
        if (!stillVisible)
            hideFamilyPanel();
    }
}

// ---------------------------------------------------------------------------
// Favorites persistence
// ---------------------------------------------------------------------------

void ModeSelector::addFavorite(const QString& modeName)
{
    if (m_favorites.contains(modeName)) return;
    m_favorites.append(modeName);
    QSettings s("KC3SMW", "neodigi");
    s.setValue("modeSelector/favorites", m_favorites);
    rebuildFavorites();
}

void ModeSelector::removeFavorite(const QString& modeName)
{
    m_favorites.removeAll(modeName);
    QSettings s("KC3SMW", "neodigi");
    s.setValue("modeSelector/favorites", m_favorites);
    rebuildFavorites();
}

// ---------------------------------------------------------------------------
// Key events
// ---------------------------------------------------------------------------

void ModeSelector::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape && m_search && !m_search->text().isEmpty()) {
        m_search->clear();
        return;
    }
    QDialog::keyPressEvent(e);
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void ModeSelector::resizeEvent(QResizeEvent* e)
{
    QDialog::resizeEvent(e);
    if (m_search && m_grid) {
        QString filter = m_search->text();
        rebuildGrid(filter);
    }
}
