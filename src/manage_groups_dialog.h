#pragma once
#include <QDialog>
#include "profile_store.h"
class QLineEdit;
class QComboBox;
class QTreeWidget;
class QLabel;
class QPushButton;
class ManageGroupsDialog : public QDialog {
public:
    explicit ManageGroupsDialog(const QString &directory, QWidget *parent = nullptr);
private:
    bool refresh();
    bool save(const ProfileData &changed, const QString &message);
    void addGroup();
    void assignProfiles();
    void removeGroup();
    QString m_directory;
    ProfileData m_data;
    QLineEdit *m_name{};
    QComboBox *m_target{};
    QComboBox *m_removeGroup{};
    QTreeWidget *m_profiles{};
    QLabel *m_status{};
    QPushButton *m_add{};
    QPushButton *m_assign{};
    QPushButton *m_remove{};
};
