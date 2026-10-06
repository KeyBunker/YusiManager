#pragma once
#include <QString>
#include <QStringList>

namespace I18n {
QString text(const char *source);
QString language();
QStringList languages();
QString languageName(const QString &code);
bool isSupported(const QString &code);
bool setLanguage(const QString &code, bool persist = true);
}
