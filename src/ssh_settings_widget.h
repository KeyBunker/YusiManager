#pragma once
#include <QWidget>
class QLabel;
class QTableWidget;
class QPushButton;
class SshSettingsWidget : public QWidget {
public:
    explicit SshSettingsWidget(QWidget *parent = nullptr);
    void refresh();
private:
    void apply(bool adding);
    QTableWidget *m_table{};
    QLabel *m_status{};
    QLabel *m_note{};
    QPushButton *m_add{};
    QPushButton *m_remove{};
};
