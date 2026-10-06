#include "i18n.h"
#include "manage_groups_dialog.h"
#include <QtWidgets>
#include <algorithm>

ManageGroupsDialog::ManageGroupsDialog(const QString &directory, QWidget *parent)
    : QDialog(parent), m_directory(directory) {
    setWindowTitle(I18n::text("Manage groups"));
    resize(720, 620);
    auto *layout = new QVBoxLayout(this);
    auto *addSection = new QGroupBox(I18n::text("Add group"));
    auto *addLayout = new QHBoxLayout(addSection);
    m_name = new QLineEdit;
    m_name->setObjectName("newGroupName");
    m_name->setPlaceholderText(I18n::text("New group name"));
    m_add = new QPushButton(I18n::text("Add group"));
    m_add->setObjectName("addGroupButton");
    addLayout->addWidget(m_name, 1);
    addLayout->addWidget(m_add);
    layout->addWidget(addSection);

    auto *assignSection = new QGroupBox(I18n::text("Assign profiles to a group"));
    auto *assignLayout = new QVBoxLayout(assignSection);
    assignLayout->addWidget(new QLabel(I18n::text("Check the profiles you want to move, then choose their group.")));
    m_profiles = new QTreeWidget;
    m_profiles->setObjectName("groupProfiles");
    m_profiles->setHeaderLabels({I18n::text("Profile"), I18n::text("Current group"), I18n::text("Host")});
    m_profiles->setRootIsDecorated(false);
    m_profiles->setAlternatingRowColors(true);
    m_profiles->setSelectionMode(QAbstractItemView::NoSelection);
    m_profiles->header()->setSectionResizeMode(QHeaderView::Stretch);
    assignLayout->addWidget(m_profiles, 1);
    auto *targetLayout = new QHBoxLayout;
    targetLayout->addWidget(new QLabel(I18n::text("Group:")));
    m_target = new QComboBox;
    m_target->setObjectName("targetGroup");
    targetLayout->addWidget(m_target, 1);
    m_assign = new QPushButton(I18n::text("Assign profiles"));
    m_assign->setObjectName("assignProfilesButton");
    targetLayout->addWidget(m_assign);
    assignLayout->addLayout(targetLayout);
    layout->addWidget(assignSection, 1);

    auto *removeSection = new QGroupBox(I18n::text("Remove group"));
    auto *removeLayout = new QVBoxLayout(removeSection);
    removeLayout->addWidget(new QLabel(I18n::text("Profiles in the removed group move to Home. Home cannot be removed.")));
    auto *removeRow = new QHBoxLayout;
    m_removeGroup = new QComboBox;
    m_removeGroup->setObjectName("removeGroupChoice");
    removeRow->addWidget(m_removeGroup, 1);
    m_remove = new QPushButton(I18n::text("Remove group"));
    m_remove->setObjectName("removeGroupButton");
    removeRow->addWidget(m_remove);
    removeLayout->addLayout(removeRow);
    layout->addWidget(removeSection);
    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    for (auto *button : {m_add, m_assign, m_remove}) button->setAutoDefault(false);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_add, &QPushButton::clicked, this, &ManageGroupsDialog::addGroup);
    connect(m_name, &QLineEdit::returnPressed, m_add, &QPushButton::click);
    connect(m_assign, &QPushButton::clicked, this, &ManageGroupsDialog::assignProfiles);
    connect(m_remove, &QPushButton::clicked, this, &ManageGroupsDialog::removeGroup);
    refresh();
}

bool ManageGroupsDialog::refresh() {
    QString error;
    const bool ok = ProfileStore::load(m_directory, m_data, &error);
    m_add->setEnabled(ok);
    m_name->setEnabled(ok);
    m_assign->setEnabled(ok && !m_data.profiles.isEmpty());
    m_profiles->setEnabled(ok);
    m_target->setEnabled(ok);
    m_remove->setEnabled(false);
    if (!ok) { m_status->setText(error); return false; }
    const QString selectedTarget = m_target->currentText();
    const QString selectedRemoval = m_removeGroup->currentText();
    QStringList groups = m_data.groups;
    if (!groups.contains("Home", Qt::CaseInsensitive)) groups.append("Home");
    groups.sort(Qt::CaseInsensitive);
    m_target->clear();
    m_target->addItems(groups);
    if (groups.contains(selectedTarget)) m_target->setCurrentText(selectedTarget);
    m_removeGroup->clear();
    for (const QString &group : groups) if (group.compare("Home", Qt::CaseInsensitive) != 0) m_removeGroup->addItem(group);
    if (m_removeGroup->findText(selectedRemoval) >= 0) m_removeGroup->setCurrentText(selectedRemoval);
    m_remove->setEnabled(m_removeGroup->count() > 0);
    m_removeGroup->setEnabled(m_removeGroup->count() > 0);
    m_profiles->clear();
    for (const Profile &p : m_data.profiles) {
        auto *item = new QTreeWidgetItem(m_profiles, {p.name, p.group, p.host});
        item->setData(0, Qt::UserRole, p.id);
        item->setCheckState(0, Qt::Unchecked);
    }
    m_profiles->sortItems(0, Qt::AscendingOrder);
    return true;
}

bool ManageGroupsDialog::save(const ProfileData &changed, const QString &message) {
    QString error;
    if (!ProfileStore::save(m_directory, changed, &error)) {
        QMessageBox::warning(this, I18n::text("Manage groups"), error);
        refresh();
        return false;
    }
    if (refresh()) m_status->setText(message);
    return true;
}

void ManageGroupsDialog::addGroup() {
    const QString name = m_name->text().trimmed();
    if (!ProfileStore::validName(name) || m_data.groups.contains(name, Qt::CaseInsensitive)) {
        QMessageBox::warning(this, I18n::text("Add group"), I18n::text("Enter a valid, unique group name.")); return;
    }
    ProfileData changed = m_data;
    changed.groups.append(name);
    if (save(changed, I18n::text("Group added: ") + name)) {
        m_name->clear();
        m_target->setCurrentText(name);
    }
}

void ManageGroupsDialog::assignProfiles() {
    QSet<QString> ids;
    for (int i = 0; i < m_profiles->topLevelItemCount(); ++i) {
        const auto *item = m_profiles->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked) ids.insert(item->data(0, Qt::UserRole).toString());
    }
    if (ids.isEmpty()) { m_status->setText(I18n::text("Check one or more profiles first.")); return; }
    ProfileData changed = m_data;
    QString error;
    int moved = 0, renamed = 0;
    if (!ProfileStore::assignProfiles(changed, ids, m_target->currentText(), moved, renamed, &error)) {
        QMessageBox::warning(this, I18n::text("Assign profiles"), error); return;
    }
    save(changed, QString(I18n::text("Profiles moved: %1. Names adjusted: %2.")).arg(moved).arg(renamed));
}

void ManageGroupsDialog::removeGroup() {
    if (m_removeGroup->currentIndex() < 0) return;
    const QString group = m_removeGroup->currentText();
    ProfileData changed = m_data;
    QString error;
    int moved = 0, renamed = 0;
    if (!ProfileStore::removeGroup(changed, group, moved, renamed, &error)) {
        QMessageBox::warning(this, I18n::text("Remove group"), error); return;
    }
    QMessageBox confirmation(QMessageBox::Question, I18n::text("Remove group"),
        QString(I18n::text("Are you sure you want to remove %1?\n%2 profiles will move to Home.\n%3 conflicting names will receive a group suffix; all profiles are kept.")).arg(group).arg(moved).arg(renamed),
        QMessageBox::Yes | QMessageBox::No, this);
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::No);
    if (confirmation.exec() != QMessageBox::Yes) return;
    save(changed, QString(I18n::text("Group removed: %1. Profiles moved to Home: %2.")).arg(group).arg(moved));
}
