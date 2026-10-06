#pragma once

#include <QDialog>
#include <QString>
#include <optional>
#include "profile.h"

class QComboBox;
class QLineEdit;
class QSpinBox;

class AddProfileDialog : public QDialog {
public:
    explicit AddProfileDialog(const QString &directory, QWidget *parent = nullptr);
    AddProfileDialog(const QString &directory, const Profile &profile, QWidget *parent = nullptr);
    QString savedProfileId() const { return m_savedProfileId; }

private:
    void saveProfile();
    void showError(const QString &message, QWidget *field = nullptr);

    QString m_directory;
    QString m_savedProfileId;
    std::optional<Profile> m_originalProfile;
    QLineEdit *m_name{};
    QLineEdit *m_host{};
    QLineEdit *m_user{};
    QSpinBox *m_port{};
    QLineEdit *m_keyFile{};
    QLineEdit *m_remoteDirectory{};
    QLineEdit *m_localDirectory{};
    QComboBox *m_colorProfile{};
    QComboBox *m_groupMode{};
    QComboBox *m_existingGroup{};
    QLineEdit *m_newGroup{};
};
