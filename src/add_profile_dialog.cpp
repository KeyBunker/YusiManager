#include "i18n.h"
#include "add_profile_dialog.h"
#include "profile.h"
#include "profile_store.h"

#include <QtWidgets>
#include <algorithm>

namespace {
enum GroupMode { NoGroup, ExistingGroup, NewGroup };
}

AddProfileDialog::AddProfileDialog(const QString &directory, QWidget *parent)
    : QDialog(parent), m_directory(directory)
{
    setWindowTitle(I18n::text("Add profile"));
    setMinimumWidth(540);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);

    m_name = new QLineEdit;
    m_name->setPlaceholderText(I18n::text("e.g. Web server"));
    m_name->setObjectName("profileName");
    form->addRow(I18n::text("Profile name:"), m_name);

    m_groupMode = new QComboBox;
    m_groupMode->addItem(I18n::text("Home (default)"), NoGroup);
    m_groupMode->addItem(I18n::text("Existing group"), ExistingGroup);
    m_groupMode->addItem(I18n::text("New group…"), NewGroup);
    form->addRow(I18n::text("Group:"), m_groupMode);

    m_existingGroup = new QComboBox;
    ProfileData stored;
    QString loadError;
    ProfileStore::load(m_directory, stored, &loadError);
    QStringList groups = stored.groups;
    groups.sort(Qt::CaseInsensitive);
    m_existingGroup->addItems(groups);
    m_existingGroup->setPlaceholderText(I18n::text("No groups available"));
    form->addRow(I18n::text("Existing group:"), m_existingGroup);

    m_newGroup = new QLineEdit;
    m_newGroup->setPlaceholderText(I18n::text("e.g. Production"));
    form->addRow(I18n::text("New group name:"), m_newGroup);
    const auto updateGroupFields = [this]{
        const int mode = m_groupMode->currentData().toInt();
        m_existingGroup->setEnabled(mode == ExistingGroup);
        m_newGroup->setEnabled(mode == NewGroup);
    };
    connect(m_groupMode, &QComboBox::currentIndexChanged, this, updateGroupFields);
    updateGroupFields();

    m_host = new QLineEdit;
    m_host->setPlaceholderText(I18n::text("Hostname or IP address, without a port"));
    form->addRow(I18n::text("Host:"), m_host);
    m_user = new QLineEdit;
    form->addRow(I18n::text("SSH user:"), m_user);
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(22);
    form->addRow(I18n::text("Port:"), m_port);

    m_colorProfile = new QComboBox;
    m_colorProfile->setObjectName("terminalColorProfile");
    m_colorProfile->addItems(terminalColorProfiles());
    m_colorProfile->setToolTip(I18n::text("Create a matching profile in your terminal. Terminals without named profiles use their own colors."));
    form->addRow(I18n::text("Terminal color profile:"), m_colorProfile);

    m_keyFile = new QLineEdit;
    m_keyFile->setPlaceholderText(I18n::text("Leave empty to use the SSH agent"));
    auto *keyRow = new QHBoxLayout;
    auto *keyBrowse = new QPushButton(I18n::text("Browse…"));
    keyBrowse->setAutoDefault(false);
    keyRow->addWidget(m_keyFile, 1);
    keyRow->addWidget(keyBrowse);
    form->addRow(I18n::text("Key file:"), keyRow);
    connect(keyBrowse, &QPushButton::clicked, this, [this]{
        const QString path = QFileDialog::getOpenFileName(this, I18n::text("Choose SSH key"),
            m_keyFile->text().isEmpty() ? QDir::home().filePath(".ssh") : expandPath(m_keyFile->text()));
        if (!path.isEmpty()) m_keyFile->setText(path);
    });

    m_remoteDirectory = new QLineEdit("/");
    form->addRow(I18n::text("Remote folder:"), m_remoteDirectory);
    m_localDirectory = new QLineEdit(QDir::homePath());
    auto *localRow = new QHBoxLayout;
    auto *localBrowse = new QPushButton(I18n::text("Browse…"));
    localBrowse->setAutoDefault(false);
    localRow->addWidget(m_localDirectory, 1);
    localRow->addWidget(localBrowse);
    form->addRow(I18n::text("Local folder:"), localRow);
    connect(localBrowse, &QPushButton::clicked, this, [this]{
        const QString path = QFileDialog::getExistingDirectory(this, I18n::text("Choose local folder"),
                                                                expandPath(m_localDirectory->text()));
        if (!path.isEmpty()) m_localDirectory->setText(path);
    });

    auto *location = new QLabel(I18n::text("Storage file: ") + QDir::toNativeSeparators(ProfileStore::filePath(m_directory)));
    location->setTextFormat(Qt::PlainText);
    location->setWordWrap(true);
    layout->addWidget(location);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &AddProfileDialog::saveProfile);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_name->setFocus();
}

AddProfileDialog::AddProfileDialog(const QString &directory, const Profile &profile, QWidget *parent)
    : AddProfileDialog(directory, parent)
{
    m_originalProfile = profile;
    setWindowTitle(I18n::text("Edit profile"));
    m_name->setText(profile.name);
    m_host->setText(profile.host);
    m_user->setText(profile.user);
    m_port->setValue(profile.port.toInt());
    m_colorProfile->setCurrentText(profile.colorProfile);
    m_keyFile->setText(profile.keyfile == "agent" ? QString() : profile.keyfile);
    m_remoteDirectory->setText(profile.remotedir);
    m_localDirectory->setText(profile.homedir);
    if (!profile.group.isEmpty() && profile.group.compare("Home", Qt::CaseInsensitive) != 0) {
        m_groupMode->setCurrentIndex(m_groupMode->findData(ExistingGroup));
        m_existingGroup->setCurrentIndex(m_existingGroup->findText(profile.group, Qt::MatchExactly));
    }
}

void AddProfileDialog::showError(const QString &message, QWidget *field) {
    QMessageBox box(QMessageBox::Warning, windowTitle(), message, QMessageBox::Ok, this);
    box.setTextFormat(Qt::PlainText);
    box.exec();
    if (field) field->setFocus();
}

void AddProfileDialog::saveProfile() {
    ProfileData data;
    QString error;
    if (!ProfileStore::load(m_directory, data, &error)) { showError(error); return; }
    auto current = data.profiles.end();
    if (m_originalProfile) {
        current = std::find_if(data.profiles.begin(), data.profiles.end(),
            [this](const Profile &p) { return p.id == m_originalProfile->id; });
        if (current == data.profiles.end() || *current != *m_originalProfile) {
            showError(I18n::text("The profile changed or disappeared. Reopen it before editing."));
            return;
        }
    }
    Profile p;
    p.id = m_originalProfile ? m_originalProfile->id : QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = m_name->text().trimmed();
    p.host = m_host->text().trimmed();
    p.user = m_user->text().trimmed();
    p.port = QString::number(m_port->value());
    p.colorProfile = m_colorProfile->currentText();
    p.keyfile = m_keyFile->text().trimmed();
    p.remotedir = m_remoteDirectory->text().trimmed();
    p.homedir = m_localDirectory->text().trimmed();
    const int mode = m_groupMode->currentData().toInt();
    if (mode == ExistingGroup) {
        p.group = m_existingGroup->currentText();
        if (p.group.isEmpty() || !data.groups.contains(p.group)) {
            showError(I18n::text("The selected group is no longer available.")); return;
        }
    } else if (mode == NewGroup) {
        p.group = m_newGroup->text().trimmed();
        if (!ProfileStore::validName(p.group) || data.groups.contains(p.group, Qt::CaseInsensitive)) {
            showError(I18n::text("Enter a valid, unique group name."), m_newGroup); return;
        }
        data.groups.append(p.group);
    }
    if (!ProfileStore::normalizeProfile(p, &error)) { showError(error); return; }
    if (m_originalProfile) *current = p;
    else data.profiles.append(p);
    if (!ProfileStore::save(m_directory, data, &error)) { showError(error); return; }
    m_savedProfileId = p.id;
    accept();
}
