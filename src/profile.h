#pragma once
#include <QString>
#include <QStringList>

struct Profile {
    QString group;
    QString name;
    QString host;
    QString user;
    QString port;
    QString keyfile;
    QString remotedir;
    QString homedir;
    QString id;
    QString colorProfile = "Yusi-1";
    bool operator==(const Profile &) const = default;
};

QStringList terminalColorProfiles();
QString trimAll(const QString &s);
QString expandPath(QString p);
QString defaultProfilesDirectory();
QString normalizeHostForSSH(QString h);
