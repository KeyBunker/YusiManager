#pragma once
#include <QByteArray>
#include <QString>

QString instanceServerName();
bool notifyRunningInstance(const QString &name, const QByteArray &msg);
