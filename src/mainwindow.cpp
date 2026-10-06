#include "mainwindow.h"
#include "i18n.h"
#include "text_size.h"
#include "app_version.h"
#include "add_profile_dialog.h"
#include "manage_groups_dialog.h"
#include "ssh_settings_widget.h"
#include "terminal_utils.h"
#include "profile_backup.h"
#include <QSettings>
#include <QFileDialog>
#include <QStandardPaths>
#include <unistd.h>
#include <QUrl>
#include "profile_store.h"
#include <QShortcut>
#include <QHeaderView>
#include <QFileInfo>
#include <QFrame>
#include <QToolButton>
#include <QRegularExpression>
#include <algorithm>

namespace {
class ExportOptionsDialog : public QDialog {
public:
    QRadioButton *encrypt;
    QLineEdit *password;
    QLineEdit *repeat;
    explicit ExportOptionsDialog(QWidget *parent) : QDialog(parent) {
        setWindowTitle(I18n::text("Export backup"));
        setMinimumWidth(480);
        auto *layout = new QVBoxLayout(this);
        auto *noEncryption = new QRadioButton(I18n::text("No encryption"));
        noEncryption->setObjectName("backupNoEncryption");
        encrypt = new QRadioButton(I18n::text("Encrypt with password"));
        encrypt->setObjectName("backupEncryption");
        auto *choices = new QButtonGroup(this);
        choices->addButton(noEncryption);
        choices->addButton(encrypt);
        noEncryption->setChecked(true);
        layout->addWidget(noEncryption);
        layout->addWidget(encrypt);
        auto *description = new QLabel(I18n::text("No password is required for this backup."));
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *passwordFields = new QWidget;
        auto *form = new QFormLayout(passwordFields);
        form->setContentsMargins(0, 0, 0, 0);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        password = new QLineEdit;
        password->setObjectName("backupPassword");
        repeat = new QLineEdit;
        repeat->setObjectName("backupRepeatPassword");
        for (auto *field : {password, repeat}) field->setEchoMode(QLineEdit::Password);
        form->addRow(I18n::text("Password:"), password);
        form->addRow(I18n::text("Repeat password:"), repeat);
        layout->addWidget(passwordFields);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(I18n::text("Continue"));
        layout->addWidget(buttons);
        const auto updateChoice = [this, passwordFields, description](bool enabled) {
            passwordFields->setVisible(enabled);
            passwordFields->setEnabled(enabled);
            description->setText(enabled ? I18n::text("Use this password when importing the backup.")
                                         : I18n::text("No password is required for this backup."));
            if (enabled) password->setFocus();
            else { password->clear(); repeat->clear(); }
            this->layout()->activate();
            adjustSize();
        };
        connect(encrypt, &QRadioButton::toggled, this, updateChoice);
        updateChoice(false);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            if (encrypt->isChecked() && password->text().isEmpty()) {
                QMessageBox::warning(this, I18n::text("Export backup"), I18n::text("Enter a password or select No encryption."));
                password->setFocus(); return;
            }
            if (encrypt->isChecked() && password->text() != repeat->text()) {
                QMessageBox::warning(this, I18n::text("Export backup"), I18n::text("The passwords do not match."));
                repeat->setFocus(); return;
            }
            accept();
        });
    }
};
}

static void addSpanForHeaderRow(QTableView *view, int row) {
    if (!view) return;
    view->setSpan(row, 1, 1, view->model()->columnCount() - 1);
}

MainWindow::MainWindow(QString sitesDir, QWidget *parent)
    : QMainWindow(parent), m_sitesDir(std::move(sitesDir))
{
    resize(920, 560);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *top = new QHBoxLayout;
    m_sitesEdit = new QLineEdit(m_sitesDir);
    m_sitesEdit->setClearButtonEnabled(true);
    m_sitesEdit->setPlaceholderText(defaultProfilesDirectory());

    auto *browseBtn = new QToolButton;
    browseBtn->setText(I18n::text("Browse…"));
    browseBtn->setIcon(QIcon::fromTheme("folder-open"));
    browseBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    browseBtn->setAutoRaise(true);
    browseBtn->setCursor(Qt::PointingHandCursor);
    browseBtn->setToolTip(I18n::text("Choose a directory that contains your server profiles"));

    m_filterEdit = new QLineEdit;
    m_filterEdit->setObjectName("profileSearch");
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setPlaceholderText(I18n::text("Search profiles…  Ctrl+F"));
    m_filterEdit->setAccessibleName(I18n::text("Search profiles"));

    m_openBtn  = new QPushButton(I18n::text("Connect"));
    m_openBtn->setIcon(QIcon::fromTheme("network-connect"));
    m_openBtn->setToolTip(I18n::text("Open an SSH session in your selected terminal"));

    m_filesBtn = new QPushButton(I18n::text("File Browser"));
    m_filesBtn->setIcon(QIcon::fromTheme("folder-remote"));

    auto *menuButton = new QToolButton;
    menuButton->setText("⋯");
    menuButton->setToolTip(I18n::text("Profiles, settings, import and export"));
    menuButton->setAccessibleName(I18n::text("Profiles, settings, import and export"));
    menuButton->setAutoRaise(true);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto *appMenu = new QMenu(menuButton);
    m_addProfileAction = appMenu->addAction(QIcon::fromTheme("list-add"), I18n::text("Add profile…"));
    m_addProfileAction->setObjectName("addProfileAction");
    m_addProfileAction->setShortcut(QKeySequence::New);
    connect(m_addProfileAction, &QAction::triggered, this, &MainWindow::addProfile);
    appMenu->addSeparator();
    auto *duplicateProfileAction = appMenu->addAction(QIcon(":/icons/duplicate.png"), I18n::text("Duplicate"));
    duplicateProfileAction->setObjectName("duplicateProfileAction");
    connect(duplicateProfileAction, &QAction::triggered, this, &MainWindow::duplicateProfile);
    m_editProfileAction = appMenu->addAction(QIcon(":/icons/edit.png"), I18n::text("Edit"));
    m_editProfileAction->setObjectName("editProfileAction");
    connect(m_editProfileAction, &QAction::triggered, this, &MainWindow::editProfile);
    m_removeProfileAction = appMenu->addAction(QIcon::fromTheme("list-remove"), I18n::text("Remove profile…"));
    m_removeProfileAction->setObjectName("removeProfileAction");
    connect(m_removeProfileAction, &QAction::triggered, this, &MainWindow::removeProfile);
    auto *manageGroupsAction = appMenu->addAction(QIcon(":/icons/group.png"), I18n::text("Manage groups…"));
    manageGroupsAction->setObjectName("manageGroupsAction");
    connect(manageGroupsAction, &QAction::triggered, this, &MainWindow::manageGroups);
    appMenu->addSeparator();
    m_importAction = appMenu->addAction(QIcon(":/icons/import.png"), I18n::text("Import"));
    m_exportAction = appMenu->addAction(QIcon(":/icons/export.png"), I18n::text("Export"));
    appMenu->addSeparator();
    m_settingsAction = appMenu->addAction(QIcon(":/icons/settings.png"), I18n::text("Settings…"));
    m_exportAction->setObjectName("exportBackupAction");
    m_importAction->setObjectName("importBackupAction");
    menuButton->setMenu(appMenu);
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportBackup);
    connect(m_importAction, &QAction::triggered, this, &MainWindow::importBackup);

    top->addWidget(m_filterEdit, 1);
    top->addWidget(m_openBtn);
    top->addWidget(m_filesBtn);
    top->addWidget(menuButton);

    m_table = new QTableView;
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    m_source = new ProfilesModel(this);
    m_filter = new ProfilesFilterProxy(this);
    m_filter->setSourceModel(m_source);
    m_group = new GroupHeaderProxy(this);
    m_group->setSourceModel(m_filter);
    m_table->setModel(m_group);
    connect(m_group, &QAbstractItemModel::modelReset, this, [this]{ updateSpans(); });

    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        const QModelIndex idx = m_table->indexAt(pos);
        if (!idx.isValid()) return;
        if (idx.data(Qt::UserRole+3).toBool()) return;
        const QString cellText = idx.data(Qt::DisplayRole).toString();
        m_table->selectRow(idx.row());
        QMenu menu(this);
        QAction *actSSH = menu.addAction(QIcon::fromTheme("network-connect"), I18n::text("Connect"));
        QAction *actKru = menu.addAction(QIcon::fromTheme("folder-remote"), I18n::text("File Browser"));
        menu.addSeparator();
        QAction *actCopy = menu.addAction(QIcon(":/icons/copy.png"), I18n::text("Copy"));
        actCopy->setObjectName("copyCellAction");
        actCopy->setIconVisibleInMenu(true);
        connect(actCopy, &QAction::triggered, this, [cellText]{
            QGuiApplication::clipboard()->setText(cellText);
        });
        QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
        if (!chosen) return;
        if (chosen == actSSH) openSelected();
        else if (chosen == actKru) mountOverSshfs();
    });

    auto *bottom = new QHBoxLayout;

    m_terminalCombo = new QComboBox;
    m_terminalCombo->setEditable(true);
    for (const QString &t : findTerminalCandidates()) if (!t.isEmpty()) m_terminalCombo->addItem(t);
    m_terminalCombo->setCurrentText(detectTerminal());
    int idx = m_terminalCombo->findText("konsole", Qt::MatchFixedString);
    if (idx >= 0) {
        QString konsoleText = m_terminalCombo->itemText(idx);
        m_terminalCombo->removeItem(idx);
        m_terminalCombo->insertItem(0, konsoleText);
        m_terminalCombo->setCurrentIndex(0);
    }

    m_statusLabel = new QLabel;
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bottom->addWidget(m_statusLabel);

    m_autostartCheck = new QCheckBox(I18n::text("Start at login"));
    m_autostartCheck->setToolTip(I18n::text("Automatically launch YusiManager when you log in."));

    m_settingsDialog = new QDialog(this);
    m_settingsDialog->setWindowTitle(I18n::text("Settings"));
    m_settingsDialog->resize(800, 650);
    m_settingsDialog->setMinimumSize(640, 520);
    auto *settingsLayout = new QVBoxLayout(m_settingsDialog);
    settingsLayout->setContentsMargins(20, 18, 20, 18);
    settingsLayout->setSpacing(14);
    auto *settingsTitle = new QLabel(I18n::text("Settings"));
    QFont headingFont = settingsTitle->font();
    headingFont.setPointSizeF(TextSize::current() + 6);
    headingFont.setBold(true);
    settingsTitle->setFont(headingFont);
    TextSize::onChange(settingsTitle, [settingsTitle] {
        QFont headingFont = QApplication::font();
        headingFont.setPointSizeF(TextSize::current() + 6);
        headingFont.setBold(true);
        settingsTitle->setFont(headingFont);
    });
    settingsLayout->addWidget(settingsTitle);
    auto *settingsVersion = new QLabel(QString("YusiManager %1 · KeyBunker.org").arg(YusiManagerVersion));
    settingsVersion->setWordWrap(true);
    settingsLayout->addWidget(settingsVersion);
    auto *settingsTabs = new QTabWidget;
    settingsTabs->setObjectName("settingsTabs");
    const auto addSettingsTab = [settingsTabs](QWidget *page, const QString &title) {
        page->layout()->setSizeConstraint(QLayout::SetMinAndMaxSize);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(page);
        settingsTabs->addTab(scroll, title);
    };
    auto *generalPage = new QWidget;
    auto *generalLayout = new QVBoxLayout(generalPage);
    generalLayout->setContentsMargins(18, 18, 18, 18);
    generalLayout->setSpacing(18);
    auto *storageGroup = new QGroupBox(I18n::text("Profile storage"));
    auto *storageLayout = new QVBoxLayout(storageGroup);
    auto *storageHelp = new QLabel(I18n::text("Groups and profiles are stored in profiles.yusidata in this folder."));
    storageHelp->setWordWrap(true);
    storageLayout->addWidget(storageHelp);
    auto *directoryRow = new QHBoxLayout;
    directoryRow->addWidget(m_sitesEdit, 1);
    directoryRow->addWidget(browseBtn);
    storageLayout->addLayout(directoryRow);
    generalLayout->addWidget(storageGroup);
    auto *connectionGroup = new QGroupBox(I18n::text("Connections"));
    auto *connectionLayout = new QFormLayout(connectionGroup);
    connectionLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    connectionLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    connectionLayout->addRow(I18n::text("Terminal:"), m_terminalCombo);
    generalLayout->addWidget(connectionGroup);
    auto *startupGroup = new QGroupBox(I18n::text("Startup"));
    auto *startupLayout = new QVBoxLayout(startupGroup);
    startupLayout->addWidget(m_autostartCheck);
    generalLayout->addWidget(startupGroup);
    auto *languageGroup = new QGroupBox(I18n::text("Language"));
    auto *languageLayout = new QFormLayout(languageGroup);
    languageLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *languageChoice = new QComboBox;
    languageChoice->setObjectName("interfaceLanguage");
    for (const QString &code : I18n::languages()) languageChoice->addItem(I18n::languageName(code), code);
    languageChoice->setCurrentIndex(languageChoice->findData(I18n::language()));
    languageLayout->addRow(I18n::text("Interface language:"), languageChoice);
    auto *languageHelp = new QLabel(I18n::text("The language changes immediately and is included in backups."));
    languageHelp->setWordWrap(true);
    languageLayout->addRow(languageHelp);
    auto *textSizeChoice = new QDoubleSpinBox;
    textSizeChoice->setObjectName("interfaceTextSize");
    textSizeChoice->setRange(TextSize::Minimum, TextSize::Maximum);
    textSizeChoice->setDecimals(1);
    textSizeChoice->setValue(TextSize::current());
    languageLayout->addRow(I18n::text("Text size (pt):"), textSizeChoice);
    connect(textSizeChoice, &QDoubleSpinBox::editingFinished, this, [this, textSizeChoice] {
        if (textSizeChoice->value() == TextSize::current()) return;
        if (!TextSize::set(textSizeChoice->value())) {
            textSizeChoice->setValue(TextSize::current());
            QMessageBox::warning(this, I18n::text("Text size"), I18n::text("Could not save the selected text size."));
        }
    });
    TextSize::onChange(textSizeChoice, [textSizeChoice] {
        textSizeChoice->setValue(TextSize::current());
    });
    generalLayout->addWidget(languageGroup);
    connect(languageChoice, &QComboBox::currentIndexChanged, this, [this, languageChoice] {
        const QString code = languageChoice->currentData().toString();
        if (code == I18n::language()) return;
        if (!I18n::setLanguage(code)) {
            const QSignalBlocker blocked(languageChoice);
            languageChoice->setCurrentIndex(languageChoice->findData(I18n::language()));
            QMessageBox::warning(this, I18n::text("Language"), I18n::text("Could not load or save the selected language."));
            return;
        }
        m_settingsDialog->accept();
        emit languageChangeRequested(true);
    });
    generalLayout->addStretch();
    addSettingsTab(generalPage, I18n::text("General"));
    auto *sshSettings = new SshSettingsWidget;
    addSettingsTab(sshSettings, I18n::text("SSH configuration"));
    settingsLayout->addWidget(settingsTabs, 1);
    auto *settingsFooter = new QHBoxLayout;
    auto *settingsSaveHint = new QLabel(I18n::text("Changes are saved when applied."));
    settingsSaveHint->setWordWrap(true);
    settingsFooter->addWidget(settingsSaveHint);
    settingsFooter->addStretch();
    auto *settingsButtons = new QDialogButtonBox(QDialogButtonBox::Close);
    settingsFooter->addWidget(settingsButtons);
    settingsLayout->addLayout(settingsFooter);
    connect(settingsButtons, &QDialogButtonBox::rejected, m_settingsDialog, &QDialog::reject);
    connect(m_settingsAction, &QAction::triggered, this, [this, sshSettings]{
        if (QApplication::activeModalWidget()) return;
        sshSettings->refresh();
        m_settingsDialog->exec();
    });

    const QString savedTerminal = QSettings().value("terminal").toString();
    if (!savedTerminal.isEmpty()) m_terminalCombo->setCurrentText(savedTerminal);
    connect(m_terminalCombo, &QComboBox::currentTextChanged, this, [](const QString &terminal){
        QSettings().setValue("terminal", terminal);
    });

    layout->addLayout(top);
    layout->addWidget(m_table, 1);
    layout->addLayout(bottom);
    setCentralWidget(central);

    setupTray();

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::reload);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::reload);

    connect(browseBtn, &QToolButton::clicked, this, [this]{
        QString dir = QFileDialog::getExistingDirectory(m_settingsDialog, I18n::text("Choose data folder"), m_sitesEdit->text());
        if (!dir.isEmpty()) { m_sitesEdit->setText(dir); setSitesDir(dir); }
    });
    connect(m_sitesEdit, &QLineEdit::editingFinished, this, [this]{ setSitesDir(m_sitesEdit->text()); });
    connect(m_openBtn, &QPushButton::clicked, this, &MainWindow::openSelected);
    connect(m_filesBtn, &QPushButton::clicked, this, &MainWindow::mountOverSshfs);

    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex &idx){
        if (!idx.isValid() || idx.data(Qt::UserRole+3).toBool()) return;
        openSelected();
    });

    auto *scEnter  = new QShortcut(QKeySequence(Qt::Key_Return), m_table);
    auto *scReturn = new QShortcut(QKeySequence(Qt::Key_Enter),  m_table);
    connect(scEnter,  &QShortcut::activated, this, &MainWindow::openSelected);
    connect(scReturn, &QShortcut::activated, this, &MainWindow::openSelected);
    auto *scKrusader = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), m_table);
    connect(scKrusader, &QShortcut::activated, this, &MainWindow::mountOverSshfs);
    auto *scFilter = new QShortcut(QKeySequence::Find, this);
    connect(scFilter, &QShortcut::activated, this, [this]{ m_filterEdit->setFocus(); m_filterEdit->selectAll(); });

    m_autostartCheck->setChecked(isAutostartEnabled());
    connect(m_autostartCheck, &QCheckBox::toggled, this, [this](bool on){
        if (!setAutostartEnabled(on)) {
            QSignalBlocker b(m_autostartCheck);
            m_autostartCheck->setChecked(isAutostartEnabled());
            QMessageBox::warning(this, I18n::text("Autostart"), on ? I18n::text("Could not enable autostart.") : I18n::text("Could not disable autostart."));
        }
    });

    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString &s){
        m_group->setHideEmptyGroups(!s.trimmed().isEmpty());
        m_filter->setNeedle(s);
        updateSpans();
        m_statusLabel->setText(QString(I18n::text("Profiles: %1")).arg(m_filter->rowCount()));
        if (m_tray) m_tray->setToolTip(QString(I18n::text("YusiManager — Profiles: %1")).arg(m_filter->rowCount()));
    });

    setSitesDir(m_sitesDir);
}

void MainWindow::openGeneralSettings() {
    m_settingsDialog->findChild<QTabWidget *>("settingsTabs")->setCurrentIndex(0);
    m_settingsAction->trigger();
}

void MainWindow::bringToFront() {
    if (isHidden() || isMinimized()) showNormal();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (m_tray && m_tray->isVisible()) {
        hide();
        e->ignore();
        return;
    }
    QMainWindow::closeEvent(e);
}

void MainWindow::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(qApp->windowIcon());
    m_tray->setToolTip("YusiManager");

    auto *menu = new QMenu(this);
    QAction *actShow = menu->addAction(qApp->windowIcon(), I18n::text("Open YusiManager"));
    menu->addSeparator();
    menu->addAction(m_addProfileAction);
    menu->addSeparator();
    menu->addAction(m_settingsAction);
    QAction *actQuit = menu->addAction(QIcon::fromTheme("application-exit"), I18n::text("Close YusiManager"));

    connect(actShow, &QAction::triggered, this, [this]{ bringToFront(); });
    connect(actQuit, &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r){
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
            if (isHidden() || isMinimized()) showNormal(); else hide();
        }
    });
    m_tray->show();
}

void MainWindow::addProfile() {
    if (QApplication::activeModalWidget()) return;
    AddProfileDialog dialog(m_sitesDir, this);
    m_addProfileAction->setEnabled(false);
    m_removeProfileAction->setEnabled(false);
    m_settingsAction->setEnabled(false);
    m_exportAction->setEnabled(false);
    m_importAction->setEnabled(false);
    const bool saved = dialog.exec() == QDialog::Accepted;
    m_addProfileAction->setEnabled(true);
    m_removeProfileAction->setEnabled(true);
    m_settingsAction->setEnabled(true);
    m_exportAction->setEnabled(true);
    m_importAction->setEnabled(true);
    if (!saved) return;

    m_filterEdit->clear();
    reload();
    selectProfile(dialog.savedProfileId());
    m_statusLabel->setText(I18n::text("Profile added."));
}

void MainWindow::duplicateProfile() {
    if (QApplication::activeModalWidget()) return;
    const QModelIndexList selection = m_table->selectionModel()->selectedRows();
    if (selection.isEmpty() || selection.first().data(Qt::UserRole + 3).toBool()) {
        QMessageBox::information(this, I18n::text("Duplicate profile"), I18n::text("Select a profile in the list first."));
        return;
    }
    const QModelIndex sourceIndex = m_filter->mapToSource(m_group->mapToSource(selection.first()));
    if (!sourceIndex.isValid() || sourceIndex.row() >= m_source->rowCount()) return;
    const Profile profile = m_source->at(sourceIndex.row());

    ProfileData data;
    QString error;
    if (!ProfileStore::load(m_sitesDir, data, &error)) {
        QMessageBox::warning(this, I18n::text("Duplicate failed"), error); return;
    }
    const auto current = std::find_if(data.profiles.cbegin(), data.profiles.cend(),
        [&](const Profile &p) { return p.id == profile.id; });
    if (current == data.profiles.cend() || *current != profile) {
        QMessageBox::warning(this, I18n::text("Duplicate profile"), I18n::text("The profile changed or disappeared. Select it again before duplicating it."));
        return;
    }

    const QRegularExpression suffix(R"( \( #([0-9]+) \)$)");
    const QString baseName = QString(profile.name).remove(suffix);
    qint64 nextNumber = 2;
    QSet<QString> names;
    for (const Profile &p : data.profiles) {
        if (p.group.compare(profile.group, Qt::CaseInsensitive) != 0) continue;
        names.insert(p.name.toCaseFolded());
        const auto match = suffix.match(p.name);
        if (match.hasMatch() && p.name.left(match.capturedStart()).compare(baseName, Qt::CaseInsensitive) == 0) {
            nextNumber = std::max(nextNumber, qint64(match.captured(1).toInt()) + 1);
        }
    }
    Profile duplicate = profile;
    duplicate.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    do {
        duplicate.name = baseName + QString(" ( #%1 )").arg(nextNumber++);
    } while (names.contains(duplicate.name.toCaseFolded()));
    data.profiles.append(duplicate);
    if (!ProfileStore::save(m_sitesDir, data, &error)) {
        QMessageBox::warning(this, I18n::text("Duplicate failed"), error); return;
    }

    m_filterEdit->clear();
    reload();
    selectProfile(duplicate.id);
    m_statusLabel->setText(I18n::text("Profile duplicated: ") + duplicate.name);
}

void MainWindow::editProfile() {
    if (QApplication::activeModalWidget()) return;
    const QModelIndexList selection = m_table->selectionModel()->selectedRows();
    if (selection.isEmpty() || selection.first().data(Qt::UserRole + 3).toBool()) {
        QMessageBox::information(this, I18n::text("Edit profile"), I18n::text("Select a profile in the list first."));
        return;
    }
    const QModelIndex sourceIndex = m_filter->mapToSource(m_group->mapToSource(selection.first()));
    if (!sourceIndex.isValid() || sourceIndex.row() >= m_source->rowCount()) return;
    const Profile profile = m_source->at(sourceIndex.row());
    AddProfileDialog dialog(m_sitesDir, profile, this);
    if (dialog.exec() != QDialog::Accepted) return;

    m_filterEdit->clear();
    reload();
    selectProfile(dialog.savedProfileId());
    m_statusLabel->setText(I18n::text("Profile updated."));
}

void MainWindow::removeProfile() {
    if (QApplication::activeModalWidget()) return;
    const QModelIndexList selection = m_table->selectionModel()->selectedRows();
    if (selection.isEmpty() || selection.first().data(Qt::UserRole + 3).toBool()) {
        QMessageBox::information(this, I18n::text("Remove profile"), I18n::text("Select a profile in the list first."));
        return;
    }
    const QModelIndex sourceIndex = m_filter->mapToSource(m_group->mapToSource(selection.first()));
    if (!sourceIndex.isValid() || sourceIndex.row() >= m_source->rowCount()) return;
    const Profile profile = m_source->at(sourceIndex.row());

    QDialog dialog(this);
    dialog.setWindowTitle(I18n::text("Remove profile"));
    dialog.setMinimumSize(640, 340);
    auto *layout = new QVBoxLayout(&dialog);
    auto *details = new QLabel(QString(I18n::text("Host: %1@%2\nGroup: %3\n\n") + I18n::text("This permanently removes the selected profile from the XML store. Other profiles are kept."))
        .arg(profile.user, profile.host, profile.group.isEmpty() ? QStringLiteral("Home") : profile.group));
    details->setTextFormat(Qt::PlainText);
    details->setWordWrap(true);
    details->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    layout->addWidget(details);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(profile.name);
    name->setReadOnly(true);
    form->addRow(I18n::text("Profile name:"), name);
    auto *confirmation = new QLineEdit;
    confirmation->setObjectName("removeProfileConfirmation");
    confirmation->setPlaceholderText(I18n::text("Type the exact profile name above"));
    form->addRow(I18n::text("Confirm name:"), confirmation);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *remove = buttons->addButton(I18n::text("Remove profile"), QDialogButtonBox::DestructiveRole);
    remove->setEnabled(false);
    remove->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(confirmation, &QLineEdit::textChanged, &dialog, [remove, profile](const QString &text){
        remove->setEnabled(text == profile.name);
    });
    connect(remove, &QPushButton::clicked, &dialog, [&]{
        if (confirmation->text() != profile.name) return;
        ProfileData data;
        QString error;
        if (!ProfileStore::load(m_sitesDir, data, &error)) {
            QMessageBox::warning(&dialog, I18n::text("Remove failed"), error); return;
        }
        const auto current = std::find_if(data.profiles.begin(), data.profiles.end(),
            [&](const Profile &p) { return p.id == profile.id; });
        if (current == data.profiles.end() || *current != profile) {
            QMessageBox::warning(&dialog, I18n::text("Remove profile"), I18n::text("The profile changed or disappeared. Select it again before removing it."));
            dialog.reject(); return;
        }
        data.profiles.erase(current);
        if (!ProfileStore::save(m_sitesDir, data, &error)) {
            QMessageBox::warning(&dialog, I18n::text("Remove failed"), error); return;
        }
        dialog.accept();
    });
    const QList<QAction *> actions{m_addProfileAction, m_removeProfileAction, m_settingsAction,
                                   m_exportAction, m_importAction};
    for (QAction *action : actions) action->setEnabled(false);
    confirmation->setFocus();
    const bool removed = dialog.exec() == QDialog::Accepted;
    for (QAction *action : actions) action->setEnabled(true);
    if (!removed) return;
    reload();
    m_statusLabel->setText(I18n::text("Profile removed: ") + profile.name);
}

void MainWindow::selectProfile(const QString &id) {
    if (id.isEmpty()) return;
    for (int row = 0; row < m_group->rowCount(); ++row) {
        const QModelIndex index = m_group->index(row, 0);
        if (index.data(Qt::UserRole + 1).toString() == id) {
            m_table->selectRow(row);
            m_table->scrollTo(index);
            break;
        }
    }
}

void MainWindow::manageGroups() {
    if (QApplication::activeModalWidget()) return;
    ManageGroupsDialog dialog(m_sitesDir, this);
    dialog.exec();
    reload();
}

void MainWindow::exportBackup() {
    if (QApplication::activeModalWidget()) return;
    ExportOptionsDialog options(this);
    if (options.exec() != QDialog::Accepted) return;
    const bool encrypted = options.encrypt->isChecked();
    const QString fileName = "Yusi-" + QDate::currentDate().toString("yyyyMMdd")
        + (encrypted ? "-enc.yusi" : ".yusi");
    QFileDialog dialog(this, I18n::text("Export"), QDir::home().filePath(fileName));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(I18n::text("YusiManager files (*.yusi)"));
    dialog.setDefaultSuffix("yusi");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = ProfileBackup::exportFile(m_sitesDir, dialog.selectedFiles().first(), encrypted, options.password->text(), &error);
    QApplication::restoreOverrideCursor();
    options.password->clear(); options.repeat->clear();
    if (!ok) QMessageBox::warning(this, I18n::text("Export failed"), error);
    else m_statusLabel->setText(I18n::text("Export complete."));
}

void MainWindow::importBackup() {
    if (QApplication::activeModalWidget()) return;
    const QString fileName = QFileDialog::getOpenFileName(this, I18n::text("Import"), QDir::homePath(), I18n::text("YusiManager files (*.yusi)"));
    if (fileName.isEmpty()) return;
    QByteArray bytes;
    ProfileData current;
    QString error;
    if (!ProfileBackup::readFile(fileName, bytes, &error)
        || !ProfileStore::load(m_sitesDir, current, &error)) {
        QMessageBox::warning(this, I18n::text("Import failed"), error); return;
    }
    QString password;
    if (ProfileBackup::encrypted(bytes)) {
        ExportOptionsDialog sizing(this);
        QInputDialog prompt(this);
        prompt.setWindowTitle(I18n::text("Encrypted import"));
        prompt.setLabelText(I18n::text("Password:"));
        prompt.setTextEchoMode(QLineEdit::Password);
        prompt.setMinimumWidth(sizing.minimumWidth());
        prompt.resize(sizing.minimumWidth(), prompt.sizeHint().height());
        if (prompt.exec() != QDialog::Accepted) return;
        password = prompt.textValue();
    }
    ProfileData data;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool decoded = ProfileBackup::decode(bytes, password, data, &error);
    QApplication::restoreOverrideCursor();
    password.clear();
    if (!decoded) { QMessageBox::warning(this, I18n::text("Import failed"), error); return; }
    const auto result = ProfileStore::merge(current, data);
    QDialog confirmation(this);
    confirmation.setWindowTitle(I18n::text("Import"));
    confirmation.setObjectName("importConfirmation");
    auto *confirmationLayout = new QVBoxLayout(&confirmation);
    confirmationLayout->setContentsMargins(20, 16, 20, 16);
    confirmationLayout->setSpacing(10);
    auto *heading = new QHBoxLayout;
    heading->setSpacing(12);
    auto *importIcon = new QLabel;
    importIcon->setPixmap(QIcon(":/icons/import.png").pixmap(32, 32));
    heading->addWidget(importIcon, 0, Qt::AlignTop);
    auto *headingText = new QVBoxLayout;
    headingText->setSpacing(4);
    auto *title = new QLabel(I18n::text("Import backup"));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(TextSize::current() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setWordWrap(true);
    headingText->addWidget(title);
    auto *backupName = new QLabel(QFileInfo(fileName).fileName());
    backupName->setTextFormat(Qt::PlainText);
    backupName->setWordWrap(true);
    backupName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    backupName->setToolTip(fileName);
    headingText->addWidget(backupName);
    heading->addLayout(headingText, 1);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);
    contentLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    contentLayout->addLayout(heading);
    auto *summary = new QFrame;
    summary->setObjectName("importSummary");
    summary->setStyleSheet("QFrame#importSummary { background: palette(base); border: 1px solid palette(mid); border-radius: 8px; }");
    auto *summaryLayout = new QGridLayout(summary);
    summaryLayout->setContentsMargins(14, 8, 14, 8);
    summaryLayout->setHorizontalSpacing(16);
    summaryLayout->setVerticalSpacing(4);
    const auto addCount = [summaryLayout](int column, int count, const QString &label) {
        auto *number = new QLabel(QString::number(count));
        QFont numberFont = number->font();
        numberFont.setPointSizeF(TextSize::current() + 4);
        numberFont.setBold(true);
        number->setFont(numberFont);
        summaryLayout->addWidget(number, 0, column, Qt::AlignTop);
        auto *description = new QLabel(label);
        description->setWordWrap(true);
        summaryLayout->addWidget(description, 1, column, Qt::AlignTop);
        summaryLayout->setColumnStretch(column, 1);
    };
    addCount(0, result.profilesAdded, I18n::text("New profiles"));
    addCount(1, result.groupsAdded, I18n::text("New groups"));
    addCount(2, result.profilesSkipped, I18n::text("Skipped"));
    contentLayout->addWidget(summary);
    auto *preserved = new QLabel(I18n::text("Existing data will be kept."));
    preserved->setWordWrap(true);
    contentLayout->addWidget(preserved);
    auto *preferences = new QLabel(I18n::text("Restore from backup"));
    QFont sectionFont = preferences->font();
    sectionFont.setBold(true);
    preferences->setFont(sectionFont);
    preferences->setWordWrap(true);
    contentLayout->addWidget(preferences);
    auto *options = new QFormLayout;
    options->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    options->setRowWrapPolicy(QFormLayout::WrapLongRows);
    options->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    options->setHorizontalSpacing(16);
    options->setVerticalSpacing(10);
    const auto addChoice = [options](const QString &label, const QString &value, const char *name) {
        auto *choice = new QComboBox;
        choice->setObjectName(name);
        choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        choice->setMinimumContentsLength(6);
        choice->addItem(I18n::text("No"), false);
        if (!value.isEmpty()) choice->addItem(QString(I18n::text("Yes (%1)")).arg(value), true);
        choice->setCurrentIndex(0);
        choice->setEnabled(!value.isEmpty());
        options->addRow(label, choice);
        return choice;
    };
    auto *languageChoice = addChoice(I18n::text("Interface language:"),
        data.language.isEmpty() ? QString() : I18n::languageName(data.language), "importLanguage");
    auto *textSizeChoice = addChoice(I18n::text("Text size (pt):"),
        data.textSize == 0 ? QString() : QString::number(data.textSize) + " pt", "importTextSize");
    contentLayout->addLayout(options);
    auto *detailsButton = new QToolButton;
    detailsButton->setObjectName("importDetailsButton");
    detailsButton->setText(I18n::text("Backup details"));
    detailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    detailsButton->setArrowType(Qt::RightArrow);
    detailsButton->setAutoRaise(true);
    detailsButton->setCheckable(true);
    contentLayout->addWidget(detailsButton, 0, Qt::AlignLeft);
    auto *details = new QWidget;
    auto *detailsLayout = new QFormLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    detailsLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    detailsLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto *creator = new QLabel("YusiManager " + data.creatorVersion);
    creator->setTextFormat(Qt::PlainText);
    creator->setWordWrap(true);
    detailsLayout->addRow(I18n::text("Created with:"), creator);
    auto *storage = new QLineEdit(QDir::toNativeSeparators(ProfileStore::filePath(m_sitesDir)));
    storage->setObjectName("importStoragePath");
    storage->setReadOnly(true);
    storage->setCursorPosition(0);
    storage->setToolTip(storage->text());
    detailsLayout->addRow(I18n::text("Storage file: "), storage);
    contentLayout->addWidget(details);
    details->hide();
    connect(detailsButton, &QToolButton::toggled, &confirmation, [detailsButton, details](bool expanded) {
        detailsButton->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        details->setVisible(expanded);
    });
    contentLayout->addStretch();
    scroll->setWidget(content);
    confirmationLayout->addWidget(scroll, 1);
    auto *separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    confirmationLayout->addWidget(separator);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(I18n::text("Import"));
    buttons->button(QDialogButtonBox::Ok)->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    confirmationLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &confirmation, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &confirmation, &QDialog::reject);
    const QSize available = (confirmation.screen()->availableGeometry().size() - QSize(48, 80)).expandedTo(QSize(1, 1));
    if (buttons->minimumSizeHint().width() + 40 > available.width())
        buttons->setOrientation(Qt::Vertical);
    confirmation.setMaximumSize(available);
    confirmation.resize(QSize(620, 460).boundedTo(available));
    if (confirmation.exec() != QDialog::Accepted) return;
    const bool restoreLanguage = languageChoice->currentData().toBool();
    const bool restoreTextSize = textSizeChoice->currentData().toBool();
    const bool languageStored = restoreLanguage && current.language != data.language;
    const bool textSizeStored = restoreTextSize && current.textSize != data.textSize;
    if (restoreLanguage) current.language = data.language;
    if (restoreTextSize) current.textSize = data.textSize;
    if ((result.groupsAdded || result.profilesAdded || languageStored || textSizeStored)
        && !ProfileStore::save(m_sitesDir, current, &error)) {
        QMessageBox::warning(this, I18n::text("Import failed"), error); return;
    }
    bool languageChanged = restoreLanguage && data.language != I18n::language();
    if (restoreLanguage && !I18n::setLanguage(data.language)) {
        QMessageBox::warning(this, I18n::text("Language"), I18n::text("Profiles were imported, but the interface language could not be restored."));
        languageChanged = false;
    }
    if (restoreTextSize && !TextSize::set(data.textSize))
        QMessageBox::warning(this, I18n::text("Text size"), I18n::text("Profiles were imported, but the text size could not be restored."));
    m_filterEdit->clear();
    reload();
    m_statusLabel->setText(QString(I18n::text("Import complete. Added: %1; skipped: %2.")).arg(result.profilesAdded).arg(result.profilesSkipped));
    if (languageChanged) emit languageChangeRequested(false);
}

void MainWindow::reload() {
    const QModelIndexList selection = m_table->selectionModel()->selectedRows();
    const QString selectedPath = selection.isEmpty() ? QString()
        : selection.first().data(Qt::UserRole + 1).toString();
    const QStringList watchedDirectories = m_watcher->directories();
    const QStringList watchedFiles = m_watcher->files();
    if (!watchedDirectories.isEmpty()) m_watcher->removePaths(watchedDirectories);
    if (!watchedFiles.isEmpty()) m_watcher->removePaths(watchedFiles);
    m_watcher->addPath(m_sitesDir);
    const QString path = ProfileStore::filePath(m_sitesDir);
    if (QFileInfo::exists(path)) m_watcher->addPath(path);
    ProfileData data;
    QString error;
    if (!ProfileStore::load(m_sitesDir, data, &error)) {
        m_statusLabel->setText(I18n::text("Could not load profile store: ") + error);
        return;
    }
    QVector<Profile> list = std::move(data.profiles);

    m_group->setGroups(data.groups);
    m_source->setProfiles(std::move(list));

    m_filter->setNeedle(m_filterEdit->text());
    updateSpans();
    selectProfile(selectedPath);

    m_statusLabel->setText(QString(I18n::text("Profiles: %1")).arg(m_filter->rowCount()));
    if (m_tray) m_tray->setToolTip(QString(I18n::text("YusiManager — Profiles: %1")).arg(m_filter->rowCount()));
}

void MainWindow::updateSpans() {
    m_table->clearSpans();
    for (int r=0;r<m_group->rowCount();++r) {
        QModelIndex idx = m_group->index(r, 0);
        if (idx.data(Qt::UserRole+3).toBool()) addSpanForHeaderRow(m_table, r);
    }
}

void MainWindow::openSelected() {
    auto sel = m_table->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    QModelIndex proxyIdx = sel.first();
    if (proxyIdx.data(Qt::UserRole+3).toBool()) return;
    QModelIndex srcIdx = m_source->index(m_filter->mapToSource(m_group->mapToSource(proxyIdx)).row(), 0);
    int r = srcIdx.row();
    if (r < 0 || r >= m_source->rowCount()) return;

    const Profile &p = m_source->at(r);
    ensureAgentAndPreload(p.keyfile);

    QString term = m_terminalCombo->currentText().trimmed();
    if (term.isEmpty() || QStandardPaths::findExecutable(term).isEmpty())
        term = detectTerminal();

    const QStringList sshCmd = buildSshCommand(p);
    const QString title = QString("%1 (%2) %3").arg(p.name, normalizeHostForSSH(p.host), p.user);
    const QStringList finalCmd = wrapInTerminal(term, sshCmd, title, false, p.colorProfile);
    if (finalCmd.isEmpty()) {
        QMessageBox::warning(this, I18n::text("Terminal unavailable"),
            I18n::text("Select an installed, supported terminal from the list."));
        return;
    }

    const bool started = QProcess::startDetached(finalCmd.first(), finalCmd.mid(1));
    if (!started) QMessageBox::warning(this, I18n::text("Failed to start"), I18n::text("Could not start terminal/SSH. Check terminal name and path."));
}

void MainWindow::setSitesDir(const QString &dir) {
    QString d = expandPath(dir);
    if (d.isEmpty()) d = defaultProfilesDirectory();
    d = QFileInfo(d).absoluteFilePath();
    if (!QDir().mkpath(d)) {
        m_sitesEdit->setText(m_sitesDir);
        QMessageBox::warning(this, I18n::text("Data folder"),
                             QString(I18n::text("Could not create or open the data folder:\n%1")).arg(d));
        return;
    }
    QString error;
    if (!ProfileStore::initialize(d, &error)) {
        QMessageBox::warning(this, I18n::text("Profile store"), error); return;
    }
    m_sitesEdit->setText(d);
    m_statusLabel->setToolTip(d);

    if (m_sitesDir == d) {
        reload();
        return;
    }
    m_source->setProfiles({});
    m_group->setGroups({});
    m_sitesDir = d;
    if (isAutostartEnabled()) setAutostartEnabled(true);
    reload();
}

void MainWindow::ensureAgentAndPreload(const QString &) {
    QString uid = QString::number(getuid());
    const QStringList candidates = {
        "/run/user/"+uid+"/gnupg/S.gpg-agent.ssh",
        "/run/user/"+uid+"/keyring/ssh",
        "/run/user/"+uid+"/ssh-agent.socket",
        "/keyring/ssh"
    };
    for (const QString &c : candidates) {
        if (QFile::exists(c)) { qputenv("SSH_AUTH_SOCK", c.toLocal8Bit()); break; }
    }
    qputenv("SSH_ASKPASS_REQUIRE", "never");
    qunsetenv("SSH_ASKPASS");
    qunsetenv("GIT_ASKPASS");
}

void MainWindow::mountOverSshfs() {
    auto sel = m_table->selectionModel()->selectedRows();
    if (sel.isEmpty()) {
        QMessageBox::information(this, I18n::text("No profile"), I18n::text("Select a profile first."));
        return;
    }
    QModelIndex proxyIdx = sel.first();
    if (proxyIdx.data(Qt::UserRole+3).toBool()) return;
    QModelIndex srcIdx = m_source->index(m_filter->mapToSource(m_group->mapToSource(proxyIdx)).row(), 0);
    int r = srcIdx.row();
    if (r < 0 || r >= m_source->rowCount()) return;

    const Profile &p = m_source->at(r);

    ensureAgentAndPreload(p.keyfile);

    bool okPort = false;
    const int port = p.port.toInt(&okPort);
    if (!okPort || port <= 0 || port > 65535) {
        QMessageBox::warning(this, I18n::text("Invalid port"), QString(I18n::text("Port is invalid: %1")).arg(p.port));
        return;
    }

    auto hostForUri = [](QString h) {
        h = h.trimmed();
        if (h.startsWith('[') && h.endsWith(']')) h = h.mid(1, h.size()-2);
        return h;
    };
    QString remoteDir = p.remotedir.trimmed();
    if (remoteDir.isEmpty()) remoteDir = "/";
    if (!remoteDir.startsWith('/')) remoteDir.prepend('/');

    QUrl url;
    url.setScheme("fish");
    url.setUserName(p.user);
    url.setHost(hostForUri(p.host));
    url.setPort(port);
    url.setPath(remoteDir);
    const QString fishUrl = url.toString(QUrl::FullyEncoded);

    const bool haveKrusader = !QStandardPaths::findExecutable("krusader").isEmpty();
    if (haveKrusader) {
        QString leftPath = expandPath(p.homedir);
        if (leftPath.isEmpty()) leftPath = QDir::homePath();

        const QStringList args{
            QString("--left=%1").arg(leftPath),
            QString("--right=%1").arg(fishUrl)
        };
        const bool started = QProcess::startDetached("krusader", args);
        if (!started) {
            QMessageBox::warning(this, I18n::text("Failed to start"),
                                 QString(I18n::text("Could not launch Krusader with:\n--left=%1\n--right=%2")).arg(leftPath, fishUrl));
        }
        return;
    }

    QString opener;
    const QStringList fmCandidates = { "dolphin", "kioexec" };
    for (const QString &c : fmCandidates) {
        if (!QStandardPaths::findExecutable(c).isEmpty()) { opener = c; break; }
    }
    const bool haveXdgOpen = !QStandardPaths::findExecutable("xdg-open").isEmpty();
    if (opener.isEmpty() && !haveXdgOpen) {
        QMessageBox::warning(this, I18n::text("No opener found"),
                             I18n::text("No Krusader/Dolphin/kioexec/xdg-open found."));
        return;
    }

    bool started = false;
    if (!opener.isEmpty()) started = QProcess::startDetached(opener, { fishUrl });
    else                   started = QProcess::startDetached("xdg-open", { fishUrl });

    if (!started) {
        QMessageBox::warning(this, I18n::text("Failed to open"),
                             QString(I18n::text("Could not start %1 for %2.")).arg(opener.isEmpty() ? "xdg-open" : opener, fishUrl));
        return;
    }

    m_statusLabel->setText(QString(I18n::text("Opened: %1")).arg(fishUrl));
    if (m_tray) m_tray->showMessage("YusiManager", QString(I18n::text("Opened: %1")).arg(fishUrl), QSystemTrayIcon::Information, 2000);
}

QString MainWindow::autostartDir() const {
    QString cfg = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (cfg.isEmpty()) cfg = QDir::homePath() + "/.config";
    return cfg + "/autostart";
}

QString MainWindow::autostartFilePath() const {
    return autostartDir() + "/YusiManager.desktop";
}

bool MainWindow::isAutostartEnabled() const {
    return QFileInfo::exists(autostartFilePath());
}

QString MainWindow::autostartDesktopContent() const {
    const QString exe = QCoreApplication::applicationFilePath();
    QString execLine;
    if (m_sitesDir.trimmed().isEmpty()) {
        execLine = QString("\"%1\" --minimized").arg(exe);
    } else {
        execLine = QString("\"%1\" \"%2\" --minimized").arg(exe, m_sitesDir);
    }

    QString content;
    content += "[Desktop Entry]\n";
    content += "Type=Application\n";
    content += "Version=1.0\n";
    content += "Name=YusiManager\n";
    content += "Comment=Manage SSH profiles and quick-launch terminals\n";
    content += "TryExec=" + exe + "\n";
    content += "Exec=" + execLine + "\n";
    content += "Icon=" + QCoreApplication::applicationDirPath() + "/yusimanager.png\n";
    content += "StartupWMClass=YusiManager\n";
    content += "Terminal=false\n";
    content += "X-GNOME-Autostart-enabled=true\n";
    content += "X-GNOME-Autostart-Phase=Application\n";
    content += "X-GNOME-Autostart-Delay=3\n";
    content += "X-KDE-StartupNotify=false\n";
    content += "OnlyShowIn=GNOME;KDE;XFCE;LXQt;LXDE;MATE;Cinnamon;Unity;\n";
    return content;
}

bool MainWindow::setAutostartEnabled(bool on) {
    const QString dir = autostartDir();
    const QString filePath = autostartFilePath();

    if (on) {
        QDir d;
        if (!d.mkpath(dir)) return false;

        QFile f(filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return false;

        QByteArray data = autostartDesktopContent().toUtf8();
        if (f.write(data) != data.size()) {
            f.close();
            return false;
        }
        f.close();
        return true;
    } else {
        if (!QFileInfo::exists(filePath)) return true;
        return QFile::remove(filePath);
    }
}
