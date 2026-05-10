#pragma once

#include <QWidget>
#include <QVector>
#include <QPair>
#include <QString>

struct MacroSet {
    QString name;
    QVector<QPair<QString, QString>> macros;  // label, text — always 12 entries
};

class MacroGrid : public QWidget {
    Q_OBJECT

public:
    static constexpr int MACROS_PER_SET = 12;

    explicit MacroGrid(QWidget* parent = nullptr);

    void loadSet(int index);
    void openFullEditor(QWidget* dialogParent);

    const QVector<MacroSet>& sets() const { return m_sets; }

signals:
    void macroTriggered(const QString& text);

private:
    void rebuildGrid();
    void editMacro(int setIdx, int macroIdx);

    QVector<MacroSet> m_sets;
    int               m_activeSet;
    QWidget*          m_setRow;
    QWidget*          m_macroContainer;
};
