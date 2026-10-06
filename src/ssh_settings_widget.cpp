#include "i18n.h"
#include "ssh_settings_widget.h"
#include "ssh_config.h"
#include "text_size.h"
#include <QtWidgets>

SshSettingsWidget::SshSettingsWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    auto *title = new QLabel(I18n::text("Recommended SSH settings"));
    auto font = title->font(); font.setBold(true); font.setPointSizeF(TextSize::current()+2); title->setFont(font);
    TextSize::onChange(title, [title] {
        QFont headingFont = QApplication::font();
        headingFont.setPointSizeF(TextSize::current() + 2);
        headingFont.setBold(true);
        title->setFont(headingFont);
    });
    layout->addWidget(title);
    auto *description = new QLabel(I18n::text("Keep SSH connections alive and reuse connections for fewer hardware-key touches."));
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *path = new QLineEdit(SshConfig::defaultPath());
    path->setReadOnly(true);
    path->setAccessibleName(I18n::text("SSH configuration file"));
    layout->addWidget(path);
    m_table = new QTableWidget(0, 4);
    m_table->setObjectName("sshRulesTable");
    m_table->setHorizontalHeaderLabels({I18n::text("Setting"), I18n::text("Recommended"), I18n::text("Current"), I18n::text("Status")});
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table, 1);
    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    m_note = new QLabel;
    m_note->setWordWrap(true);
    m_note->setTextFormat(Qt::PlainText);
    layout->addWidget(m_note);
    auto *buttons = new QHBoxLayout;
    auto *refreshButton = new QPushButton(I18n::text("Refresh"));
    m_add = new QPushButton(I18n::text("Add missing rules"));
    m_add->setObjectName("addSshRulesButton");
    m_remove = new QPushButton(I18n::text("Remove added rules"));
    m_remove->setObjectName("removeSshRulesButton");
    for (auto *button : {refreshButton, m_add, m_remove}) button->setAutoDefault(false);
    buttons->addWidget(refreshButton);
    buttons->addStretch();
    buttons->addWidget(m_remove);
    buttons->addWidget(m_add);
    layout->addLayout(buttons);
    connect(refreshButton, &QPushButton::clicked, this, &SshSettingsWidget::refresh);
    connect(m_add, &QPushButton::clicked, this, [this] { apply(true); });
    connect(m_remove, &QPushButton::clicked, this, [this] { apply(false); });
}

void SshSettingsWidget::refresh() {
    SshConfig::State state;
    QString error;
    const bool ok = SshConfig::inspect(SshConfig::defaultPath(), state, &error);
    m_add->setEnabled(ok && state.missing > 0);
    m_remove->setEnabled(ok && state.managed);
    m_table->setRowCount(ok ? state.rules.size() : 0);
    if (!ok) { m_status->setText(error); m_note->clear(); return; }
    for (int row = 0; row < state.rules.size(); ++row) {
        const auto &rule = state.rules[row];
        const QString status = rule.managed ? I18n::text("Added by YusiManager") : rule.current.isEmpty() ? I18n::text("Missing") : I18n::text("Existing — kept");
        const QStringList values{rule.key, rule.recommended, rule.current.isEmpty() ? QStringLiteral("—") : rule.current, status};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values[column]);
            item->setToolTip(values[column]);
            m_table->setItem(row, column, item);
        }
    }
    m_status->setText(state.missing ? QString(I18n::text("%1 missing defaults can be added under Host *.")).arg(state.missing)
                                    : I18n::text("All seven defaults are already configured."));
    m_note->setText(I18n::text("Existing values are kept. Remove only deletes the block added by YusiManager. Host-specific settings keep priority.")
        + (state.includes ? QString(I18n::text("\nInclude directives detected: included files are not inspected or edited; their earlier settings may override these defaults.")) : QString()));
}

void SshSettingsWidget::apply(bool adding) {
    if (!adding && QMessageBox::question(this, I18n::text("Remove SSH rules"), I18n::text("Remove the SSH defaults added by YusiManager? Existing settings will be kept."),
                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QString error;
    const bool ok = adding ? SshConfig::addMissing(SshConfig::defaultPath(), &error)
                           : SshConfig::removeAdded(SshConfig::defaultPath(), &error);
    if (!ok) QMessageBox::warning(this, I18n::text("SSH settings"), error);
    refresh();
}
