#pragma once
#include <QString>
#include <QVector>
namespace SshConfig {
struct Rule { QString key; QString recommended; QString current; bool managed = false; };
struct State { QVector<Rule> rules; int missing = 0; bool managed = false; bool includes = false; };
QString defaultPath();
bool inspect(const QString &path, State &state, QString *error);
bool addMissing(const QString &path, QString *error);
bool removeAdded(const QString &path, QString *error);
}
