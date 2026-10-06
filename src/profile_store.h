#pragma once

#include "profile.h"
#include <QByteArray>
#include <QStringList>
#include <QVector>
#include <QSet>

struct ProfileData {
    QStringList groups;
    QVector<Profile> profiles;
    QByteArray revision;
    QString creatorVersion;
    QString language;
    double textSize = 0;
};

namespace ProfileStore {
struct MergeResult { int groupsAdded = 0; int profilesAdded = 0; int profilesSkipped = 0; };
MergeResult merge(ProfileData &current, const ProfileData &incoming);
bool assignProfiles(ProfileData &data, const QSet<QString> &ids, const QString &group, int &moved, int &renamed, QString *error);
bool removeGroup(ProfileData &data, const QString &group, int &moved, int &renamed, QString *error);

inline constexpr qint64 MaxDocumentBytes = 16 * 1024 * 1024;
QString filePath(const QString &directory);
bool validName(const QString &name);
bool normalizeProfile(Profile &profile, QString *error);
bool parse(const QByteArray &xml, ProfileData &data, QString *error);
QByteArray toXml(const ProfileData &data);
bool revision(const QString &directory, QByteArray &revision, QString *error);
bool initialize(const QString &directory, QString *error);
bool load(const QString &directory, ProfileData &data, QString *error);
bool save(const QString &directory, const ProfileData &data, QString *error);
}
