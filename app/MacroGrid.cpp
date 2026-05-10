#include "MacroGrid.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QMenu>

// Build a MacroSet with exactly MACROS_PER_SET entries (pads with empty slots)
static MacroSet makeSet(const QString& name,
                        std::initializer_list<QPair<QString,QString>> items)
{
    MacroSet s;
    s.name = name;
    for (auto& p : items)
        s.macros.append(p);
    while (s.macros.size() < MacroGrid::MACROS_PER_SET)
        s.macros.append({QString("M%1").arg(s.macros.size() + 1, 2, 10, QChar('0')), ""});
    return s;
}

MacroGrid::MacroGrid(QWidget* parent)
    : QWidget(parent)
    , m_activeSet(0)
{
    setObjectName("MacroGrid");

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->setSpacing(3);

    // Set-selector row
    auto* topRow = new QHBoxLayout;
    auto* setsLabel = new QLabel("Sets:");
    setsLabel->setObjectName("sectionLabel");
    topRow->addWidget(setsLabel);
    vbox->addLayout(topRow);

    m_setRow = new QWidget;
    m_setRow->setLayout(new QHBoxLayout);
    m_setRow->layout()->setContentsMargins(0, 0, 0, 0);
    static_cast<QHBoxLayout*>(m_setRow->layout())->setSpacing(3);
    vbox->addWidget(m_setRow);

    // 2-row × 6-col macro button area
    m_macroContainer = new QWidget;
    m_macroContainer->setLayout(new QGridLayout);
    auto* gl = static_cast<QGridLayout*>(m_macroContainer->layout());
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setSpacing(3);
    vbox->addWidget(m_macroContainer);

    // Default macro data
    m_sets = {
        makeSet("Set1", {
            {"CQ",   "CQ CQ CQ DE KC3SMW KC3SMW KC3SMW PSE K\n"},
            {"QRZ?", "QRZ? DE KC3SMW K\n"},
            {"ANS",  "DE KC3SMW GA OM 73 TU SK\n"},
            {"RST",  "UR RST 599 599 BK\n"},
            {"QSO",  "TNX FER QSO 73 DE KC3SMW SK\n"},
            {"73",   "73 73 DE KC3SMW SK\n"},
            {"SK",   "SK SK DE KC3SMW CL\n"},
            {"QTH",  "QTH IS PITTSBURGH PA BK\n"},
            {"Name", "NAME IS RYAN BK\n"},
            {"MSG",  ""},
            {"BRK",  "BRK BRK DE KC3SMW BK\n"},
            {"QRN",  "QRN QRN DE KC3SMW BK\n"},
        }),
        makeSet("Set2", {}),
        makeSet("Set3", {}),
        makeSet("Set4", {}),
        makeSet("EmComm", {
            {"PRIOR", "PRIORITY TRAFFIC: "},
            {"ICS213",""},
            {"ICS214",""},
            {"FEMA",  ""},
            {"CHKIN", "CHECK-IN DE KC3SMW BK\n"},
            {"CHKOUT","CHECK-OUT DE KC3SMW BK\n"},
            {"RELAY", ""},
            {"RQST",  "REQUEST: "},
            {"CANCL", "CANCEL: "},
            {"ACK",   "ACKNOWLEDGE: "},
            {"NACK",  "NEGATIVE: "},
            {"END",   "END OF MESSAGE DE KC3SMW BK\n"},
        }),
    };

    rebuildGrid();
}

void MacroGrid::loadSet(int index)
{
    if (index < 0 || index >= m_sets.size()) return;
    m_activeSet = index;
    rebuildGrid();
}

void MacroGrid::rebuildGrid()
{
    // --- Clear set-selector row ---
    QLayout* sl = m_setRow->layout();
    while (sl->count()) {
        QLayoutItem* item = sl->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    for (int i = 0; i < m_sets.size(); ++i) {
        auto* btn = new QPushButton(m_sets[i].name);
        btn->setCheckable(true);
        btn->setChecked(i == m_activeSet);
        btn->setFixedHeight(22);
        int idx = i;
        connect(btn, &QPushButton::clicked, this, [this, idx]() { loadSet(idx); });
        sl->addWidget(btn);
    }
    static_cast<QHBoxLayout*>(sl)->addStretch();

    // --- Clear macro grid ---
    QLayout* gl = m_macroContainer->layout();
    while (gl->count()) {
        QLayoutItem* item = gl->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    // Rebuild 2 rows × 6 cols
    auto* grid = static_cast<QGridLayout*>(gl);
    const MacroSet& active = m_sets[m_activeSet];

    for (int mi = 0; mi < MACROS_PER_SET; ++mi) {
        const auto& [label, text] = active.macros[mi];

        auto* btn = new QPushButton(label.isEmpty() ? QString("M%1").arg(mi+1) : label);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setFixedHeight(26);

        QString capturedText = text;
        connect(btn, &QPushButton::clicked, this, [this, capturedText]() {
            emit macroTriggered(capturedText);
        });

        int capturedSet = m_activeSet;
        int capturedIdx = mi;
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QPushButton::customContextMenuRequested, this,
                [this, capturedSet, capturedIdx]() {
            editMacro(capturedSet, capturedIdx);
        });

        grid->addWidget(btn, mi / 6, mi % 6);
    }
}

void MacroGrid::editMacro(int setIdx, int macroIdx)
{
    if (setIdx < 0 || setIdx >= m_sets.size()) return;
    if (macroIdx < 0 || macroIdx >= m_sets[setIdx].macros.size()) return;

    auto& [label, text] = m_sets[setIdx].macros[macroIdx];

    QDialog dlg(this);
    dlg.setWindowTitle(
        QString("Edit Macro — %1 / Slot %2").arg(m_sets[setIdx].name).arg(macroIdx + 1));
    dlg.setFixedSize(440, 150);

    auto* form      = new QFormLayout(&dlg);
    auto* labelEdit = new QLineEdit(label, &dlg);
    auto* textEdit  = new QLineEdit(text,  &dlg);
    labelEdit->setMaxLength(10);
    form->addRow("Button label:", labelEdit);
    form->addRow("Message text:", textEdit);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        label = labelEdit->text();
        text  = textEdit->text();
        rebuildGrid();
    }
}

void MacroGrid::openFullEditor(QWidget* dialogParent)
{
    QDialog dlg(dialogParent);
    dlg.setWindowTitle("Edit Macros — All Sets");
    dlg.resize(700, 520);

    auto* vbox = new QVBoxLayout(&dlg);
    auto* tabs = new QTabWidget(&dlg);

    // Build one tab per set, each with a 12-row table
    QVector<QTableWidget*> tables;
    for (int si = 0; si < m_sets.size(); ++si) {
        auto* tw = new QTableWidget(MACROS_PER_SET, 2, &dlg);
        tw->setHorizontalHeaderLabels({"Label (≤10 chars)", "Message Text"});
        tw->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        tw->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        tw->verticalHeader()->setDefaultSectionSize(22);
        tw->verticalHeader()->hide();

        for (int mi = 0; mi < MACROS_PER_SET; ++mi) {
            tw->setItem(mi, 0, new QTableWidgetItem(m_sets[si].macros[mi].first));
            tw->setItem(mi, 1, new QTableWidgetItem(m_sets[si].macros[mi].second));
        }
        tabs->addTab(tw, m_sets[si].name);
        tables.append(tw);
    }
    vbox->addWidget(tabs);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    vbox->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        for (int si = 0; si < m_sets.size(); ++si) {
            for (int mi = 0; mi < MACROS_PER_SET; ++mi) {
                auto* li = tables[si]->item(mi, 0);
                auto* ti = tables[si]->item(mi, 1);
                if (li) m_sets[si].macros[mi].first  = li->text();
                if (ti) m_sets[si].macros[mi].second = ti->text();
            }
        }
        rebuildGrid();
    }
}
