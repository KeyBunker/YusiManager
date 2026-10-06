#include "profile_backup.h"
#include "i18n.h"
#include "text_size.h"
#include "archive_crypto.h"
#include "app_version.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <openssl/crypto.h>
namespace {
std::span<const unsigned char> view(const QByteArray &bytes) {
    return {reinterpret_cast<const unsigned char *>(bytes.constData()), size_t(bytes.size())};
}
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
}
namespace ProfileBackup {
bool readFile(const QString &fileName, QByteArray &bytes, QString *error) {
    QFile f(fileName);
    if (!QFileInfo(fileName).isFile() || !f.open(QIODevice::ReadOnly)) return fail(error, I18n::text("Cannot read the selected file."));
    constexpr qint64 limit = ArchiveCrypto::MaxPayloadSize + ArchiveCrypto::MaxOverhead;
    bytes = f.read(limit + 1);
    if (f.error() != QFileDevice::NoError || bytes.size() > limit) return fail(error, I18n::text("File is unreadable or too large."));
    return true;
}
bool encrypted(const QByteArray &bytes) { return ArchiveCrypto::isEncrypted(view(bytes)); }
bool decode(const QByteArray &bytes, const QString &password, ProfileData &data, QString *error) {
    if (!encrypted(bytes)) {
        ProfileData decoded;
        if (!ProfileStore::parse(bytes, decoded, error)) return false;
        if (decoded.creatorVersion.isEmpty()) return fail(error, I18n::text("The backup has no creator version."));
        data = std::move(decoded);
        return true;
    }
    ArchiveCrypto::Bytes plain;
    std::string message;
    QByteArray secret = password.toUtf8();
    const bool ok = ArchiveCrypto::decrypt(view(bytes), {secret.constData(), size_t(secret.size())}, plain, message);
    OPENSSL_cleanse(secret.data(), size_t(secret.size()));
    if (!ok) return fail(error, I18n::text(message.c_str()));
    QByteArray xml(reinterpret_cast<const char *>(plain.data()), qsizetype(plain.size()));
    ProfileData decoded;
    bool parsed = ProfileStore::parse(xml, decoded, error);
    OPENSSL_cleanse(plain.data(), plain.size());
    OPENSSL_cleanse(xml.data(), size_t(xml.size()));
    const QString creator = QString::fromStdString(ArchiveCrypto::creatorVersion(view(bytes)));
    if (parsed && creator != decoded.creatorVersion)
        parsed = fail(error, I18n::text("The backup creator version does not match its encrypted contents."));
    if (parsed) data = std::move(decoded);
    return parsed;
}
bool exportFile(const QString &directory, const QString &fileName, bool encrypt,
                const QString &password, QString *error) {
    const QFileInfo target(fileName), source(ProfileStore::filePath(directory));
    if (target.isSymLink() || target.absoluteFilePath() == source.absoluteFilePath()
        || (target.exists() && target.canonicalFilePath() == source.canonicalFilePath()))
        return fail(error, I18n::text("Choose an export file separate from the profile storage file."));
    ProfileData data;
    if (!ProfileStore::load(directory, data, error)) return false;
    data.creatorVersion = YusiManagerVersion;
    data.language = I18n::language();
    data.textSize = TextSize::current();
    QByteArray bytes = ProfileStore::toXml(data);
    if (encrypt) {
        ArchiveCrypto::Bytes archive;
        std::string message;
        QByteArray secret = password.toUtf8();
        const bool ok = ArchiveCrypto::encrypt(view(bytes), {secret.constData(), size_t(secret.size())}, archive, message);
        OPENSSL_cleanse(secret.data(), size_t(secret.size()));
        OPENSSL_cleanse(bytes.data(), size_t(bytes.size()));
        if (!ok) return fail(error, I18n::text(message.c_str()));
        bytes = QByteArray(reinterpret_cast<const char *>(archive.data()), qsizetype(archive.size()));
    }
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) return fail(error, file.errorString());
    return true;
}
}
