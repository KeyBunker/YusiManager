#pragma once
#include "profile_store.h"
namespace ProfileBackup {
bool readFile(const QString &fileName, QByteArray &bytes, QString *error);
bool encrypted(const QByteArray &bytes);
bool decode(const QByteArray &bytes, const QString &password, ProfileData &data, QString *error);
bool exportFile(const QString &directory, const QString &fileName, bool encrypt,
                const QString &password, QString *error);
}
