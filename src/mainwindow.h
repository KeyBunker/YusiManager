#pragma once
#include <QtWidgets>
#include <QFileSystemWatcher>
#include "profile.h"
#include "profiles_model.h"
#include "filter_proxy.h"
#include "group_header_proxy.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QString sitesDir, QWidget *parent=nullptr);
    void bringToFront();
    QString sitesDirectory() const { return m_sitesDir; }
    void openGeneralSettings();

signals:
    void languageChangeRequested(bool reopenSettings);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void setupTray();
    void addProfile();
    void duplicateProfile();
    void editProfile();
    void removeProfile();
    void manageGroups();
    void selectProfile(const QString &id);
    void exportBackup();
    void importBackup();
    void reload();
    void mountOverSshfs();
    void openSelected();
    void setSitesDir(const QString &dir);

    void ensureAgentAndPreload(const QString &keyfile);

    QString autostartDir() const;
    QString autostartFilePath() const;
    bool isAutostartEnabled() const;
    bool setAutostartEnabled(bool on);
    QString autostartDesktopContent() const;

    QLineEdit *m_sitesEdit{};
    QLineEdit *m_filterEdit{};
    QTableView *m_table{};
    QLabel *m_statusLabel{};
    QPushButton *m_openBtn{};
    QComboBox *m_terminalCombo{};
    QPushButton *m_filesBtn{};
    QCheckBox *m_autostartCheck{};
    QString m_sitesDir;
    QFileSystemWatcher *m_watcher{};
    QSystemTrayIcon *m_tray{};
    QDialog *m_settingsDialog{};
    QAction *m_settingsAction{};
    QAction *m_addProfileAction{};
    QAction *m_editProfileAction{};
    QAction *m_removeProfileAction{};
    QAction *m_exportAction{};
    QAction *m_importAction{};

    ProfilesModel *m_source{};
    ProfilesFilterProxy *m_filter{};
    GroupHeaderProxy *m_group{};

    void updateSpans();
};
