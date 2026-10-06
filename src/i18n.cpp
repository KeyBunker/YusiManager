#include "i18n.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QTranslator>
#include <memory>

namespace {
class AppTranslator : public QTranslator {
public:
    QJsonObject messages;
    QJsonObject widgetMessages;
    bool isEmpty() const override { return false; }
    QString translate(const char *context, const char *source, const char *, int) const override {
        if (qstrcmp(context, "YusiManager") == 0)
            return messages.value(QString::fromUtf8(source)).toString();
        static const QStringList widgetContexts {"QPlatformTheme", "QDialogButtonBox", "QFileDialog",
            "QFileSystemModel", "QLineEdit", "QWidgetTextControl", "QFileIconProvider"};
        if (!widgetContexts.contains(QString::fromUtf8(context))) return {};
        QString key = QString::fromUtf8(source);
        const bool mnemonic = key.contains('&');
        key.remove('&');
        QString value = widgetMessages.value(key).toString();
        if (value.isEmpty()) value = messages.value(key).toString();
        if (mnemonic && !value.isEmpty()) value.prepend('&');
        return value;
    }
};
QString activeLanguage = "en";
std::unique_ptr<AppTranslator> appTranslator;
}

QString I18n::text(const char *source) {
    return QCoreApplication::translate("YusiManager", source);
}

QString I18n::language() { return activeLanguage; }
QStringList I18n::languages() { return {"en", "es", "nl", "de", "fr", "pt_BR", "it", "pl"}; }
bool I18n::isSupported(const QString &code) { return languages().contains(code); }
QString I18n::languageName(const QString &code) {
    const QStringList names {"English", "Español", "Nederlands", "Deutsch", "Français", "Português (Brasil)", "Italiano", "Polski"};
    return names.value(languages().indexOf(code), code);
}

bool I18n::setLanguage(const QString &code, bool persist) {
    if (!isSupported(code)) return false;
    auto nextApp = std::make_unique<AppTranslator>();
    if (code != "en") {
        QFile file(":/translations/" + code + ".json");
        if (!file.open(QIODevice::ReadOnly)) return false;
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject() || document.object().isEmpty()) return false;
        nextApp->messages = document.object();
        QFile widgets(":/translations/widgets_" + code + ".json");
        if (!widgets.open(QIODevice::ReadOnly)) return false;
        const auto widgetDocument = QJsonDocument::fromJson(widgets.readAll());
        if (!widgetDocument.isObject() || widgetDocument.object().isEmpty()) return false;
        nextApp->widgetMessages = widgetDocument.object();
    }
    if (persist) {
        QSettings settings;
        settings.setValue("language", code);
        settings.sync();
        if (settings.status() != QSettings::NoError) return false;
    }
    if (appTranslator) QCoreApplication::removeTranslator(appTranslator.get());
    appTranslator = std::move(nextApp);
    QCoreApplication::installTranslator(appTranslator.get());
    activeLanguage = code;
    QLocale::setDefault(QLocale(code));
    return true;
}
