#include "profile_store.h"
#include "text_size.h"
#include "i18n.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHash>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {
bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}

void assignHomeGroup(ProfileData &data) {
    QString home = "Home";
    bool exists = false;
    for (const QString &group : data.groups) {
        if (group.compare(home, Qt::CaseInsensitive) == 0) { home = group; exists = true; break; }
    }
    for (Profile &profile : data.profiles) {
        if (!profile.group.isEmpty()) continue;
        if (!exists) { data.groups.append(home); exists = true; }
        profile.group = home;
    }
}

bool singleLine(const QString &text) {
    for (QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f
            || c == QChar::LineSeparator || c == QChar::ParagraphSeparator) return false;
    }
    return true;
}

bool readBytes(const QString &directory, QByteArray &bytes, bool allowMissing, QString *error) {
    const QString path = ProfileStore::filePath(directory);
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink() && allowMissing) { bytes.clear(); return true; }
    if (info.isSymLink() || !info.isFile()) return fail(error, I18n::text("The profile store is missing or is not a regular file:\n") + path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    bytes = file.read(ProfileStore::MaxDocumentBytes + 1);
    if (file.error() != QFileDevice::NoError) return fail(error, file.errorString());
    if (bytes.size() > ProfileStore::MaxDocumentBytes) return fail(error, I18n::text("The profile store exceeds 16 MiB."));
    return true;
}

bool nextElement(QXmlStreamReader &xml) {
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (!xml.namespaceUri().isEmpty() || !xml.prefix().isEmpty()) xml.raiseError(I18n::text("Namespaces are not supported."));
            return !xml.hasError();
        }
        if (token == QXmlStreamReader::EndElement || token == QXmlStreamReader::EndDocument) return false;
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
            xml.raiseError(I18n::text("DTD and entity declarations are not allowed."));
        if (token == QXmlStreamReader::Characters && !xml.isWhitespace()) xml.raiseError(I18n::text("Unexpected text."));
    }
    return false;
}

bool attributes(QXmlStreamReader &xml, const QStringList &allowed) {
    for (const auto &attribute : xml.attributes()) {
        if (!attribute.namespaceUri().isEmpty() || !allowed.contains(attribute.name().toString())) {
            xml.raiseError(I18n::text("Unknown attribute: ") + attribute.name().toString());
            return false;
        }
    }
    return true;
}

bool readProfile(QXmlStreamReader &xml, const QString &group, ProfileData &data) {
    if (!attributes(xml, {"id", "name"})) return false;
    Profile profile;
    profile.group = group;
    profile.id = xml.attributes().value("id").toString();
    profile.name = xml.attributes().value("name").toString().trimmed();
    QSet<QString> seen;
    while (nextElement(xml)) {
        const QString field = xml.name().toString();
        if (seen.contains(field) || !attributes(xml, {})) {
            xml.raiseError(I18n::text("Duplicate or invalid profile field: ") + field);
            return false;
        }
        seen.insert(field);
        QString *value = nullptr;
        if (field == "host") value = &profile.host;
        else if (field == "user") value = &profile.user;
        else if (field == "port") value = &profile.port;
        else if (field == "keyfile") value = &profile.keyfile;
        else if (field == "remotedir") value = &profile.remotedir;
        else if (field == "homedir") value = &profile.homedir;
        else if (field == "colorprofile") value = &profile.colorProfile;
        else { xml.raiseError(I18n::text("Unknown profile field: ") + field); return false; }
        *value = xml.readElementText().trimmed();
    }
    if (xml.hasError()) return false;
    QString error;
    if (!ProfileStore::normalizeProfile(profile, &error)) { xml.raiseError(error); return false; }
    if (profile.id.isEmpty()) {
        profile.id = QUuid::createUuidV5(QUuid(), (group + QChar::Null + profile.name).toUtf8())
                         .toString(QUuid::WithoutBraces);
    }
    if (!ProfileStore::validName(profile.id)) { xml.raiseError(I18n::text("Invalid profile id.")); return false; }
    data.profiles.append(profile);
    return true;
}

void writeProfile(QXmlStreamWriter &xml, const Profile &profile) {
    xml.writeStartElement("profile");
    xml.writeAttribute("id", profile.id);
    xml.writeAttribute("name", profile.name);
    xml.writeTextElement("host", profile.host);
    xml.writeTextElement("user", profile.user);
    xml.writeTextElement("port", profile.port);
    xml.writeTextElement("keyfile", profile.keyfile);
    xml.writeTextElement("remotedir", profile.remotedir);
    xml.writeTextElement("homedir", profile.homedir);
    xml.writeTextElement("colorprofile", profile.colorProfile);
    xml.writeEndElement();
}
}

QString ProfileStore::filePath(const QString &directory) {
    return QDir(directory).filePath("profiles.yusidata");
}

bool ProfileStore::validName(const QString &name) {
    return !name.trimmed().isEmpty() && singleLine(name);
}

bool ProfileStore::normalizeProfile(Profile &profile, QString *error) {
    if (!validName(profile.name)) return fail(error, I18n::text("Enter a profile name without line breaks."));
    for (const QString &value : {profile.host, profile.user, profile.port, profile.keyfile,
                                 profile.remotedir, profile.homedir}) {
        if (!singleLine(value)) return fail(error, I18n::text("Profile fields cannot contain control characters or line breaks."));
    }
    const auto whitespace = [](const QString &text) {
        for (QChar c : text) if (c.isSpace()) return true;
        return false;
    };
    if (profile.host.isEmpty() || whitespace(profile.host) || profile.host.startsWith('-')
        || profile.host.contains('/') || profile.host.contains('\\') || profile.host.contains('@')
        || profile.host.contains('?') || profile.host.contains('#'))
        return fail(error, I18n::text("Enter a hostname or IP address without a username, port or path."));
    if (profile.host.contains(':') || profile.host.contains('[') || profile.host.contains(']')) {
        QHostAddress address(normalizeHostForSSH(profile.host));
        if (address.protocol() != QAbstractSocket::IPv6Protocol) return fail(error, I18n::text("Invalid IPv6 host; enter the port in the port field."));
        profile.host = address.toString();
    }
    if (profile.user.isEmpty() || profile.user.startsWith('-') || whitespace(profile.user)
        || profile.user.contains('@') || profile.user.contains('/') || profile.user.contains('\\'))
        return fail(error, I18n::text("Enter a valid SSH username."));
    if (profile.port.isEmpty()) profile.port = "22";
    bool ok = false;
    const int port = profile.port.toInt(&ok);
    if (!ok || port < 1 || port > 65535) return fail(error, I18n::text("The port must be between 1 and 65535."));
    profile.port = QString::number(port);
    if (profile.colorProfile.isEmpty()) profile.colorProfile = "Yusi-1";
    if (!terminalColorProfiles().contains(profile.colorProfile))
        return fail(error, I18n::text("Choose a terminal color profile from Yusi-1 to Yusi-20."));
    if (profile.keyfile.isEmpty()) profile.keyfile = "agent";
    if (profile.remotedir.isEmpty()) profile.remotedir = "/";
    if (profile.homedir.isEmpty()) profile.homedir = QDir::homePath();
    return true;
}

bool ProfileStore::parse(const QByteArray &bytes, ProfileData &data, QString *error) {
    if (error) error->clear();
    if (bytes.isEmpty() || bytes.size() > MaxDocumentBytes) return fail(error, I18n::text("The XML store is empty or exceeds 16 MiB."));
    ProfileData parsed;
    QXmlStreamReader xml(bytes);
    if (!nextElement(xml) || xml.name() != QLatin1String("yusimanager")
        || xml.attributes().value("version") != QLatin1String("1") || !attributes(xml, {"version", "creatorVersion", "language", "textSize"}))
        return fail(error, I18n::text("Expected a <yusimanager version=\"1\"> XML document."));
    parsed.language = xml.attributes().value("language").toString();
    if (parsed.language == "zh_CN" || parsed.language == "ja") parsed.language.clear();
    if (!parsed.language.isEmpty() && !I18n::isSupported(parsed.language))
        return fail(error, I18n::text("Unsupported interface language in the backup."));
    if (xml.attributes().hasAttribute("textSize")) {
        bool ok = false;
        parsed.textSize = xml.attributes().value("textSize").toDouble(&ok);
        if (!ok || !TextSize::valid(parsed.textSize))
            return fail(error, I18n::text("Invalid text size in the backup."));
    }
    parsed.creatorVersion = xml.attributes().value("creatorVersion").toString();
    if (xml.attributes().hasAttribute("creatorVersion")
        && (!validName(parsed.creatorVersion) || parsed.creatorVersion.size() > 32))
        return fail(error, I18n::text("Invalid creator version."));
    QSet<QString> groups;
    while (nextElement(xml)) {
        if (xml.name() == QLatin1String("profile")) {
            if (!readProfile(xml, {}, parsed)) break;
        } else if (xml.name() == QLatin1String("group")) {
            if (!attributes(xml, {"name"})) break;
            const QString group = xml.attributes().value("name").toString().trimmed();
            if (!validName(group) || groups.contains(group.toCaseFolded())) { xml.raiseError(I18n::text("Invalid or duplicate group name.")); break; }
            groups.insert(group.toCaseFolded());
            parsed.groups.append(group);
            while (nextElement(xml)) {
                if (xml.name() != QLatin1String("profile")) { xml.raiseError(I18n::text("A group may contain only profiles.")); break; }
                if (!readProfile(xml, group, parsed)) break;
            }
        } else { xml.raiseError(I18n::text("Unknown element: ") + xml.name().toString()); break; }
    }
    if (!xml.hasError() && nextElement(xml)) xml.raiseError(I18n::text("Unexpected content after the document."));
    if (xml.hasError()) return fail(error, QString(I18n::text("Invalid XML at line %1: %2")).arg(xml.lineNumber()).arg(xml.errorString()));
    assignHomeGroup(parsed);
    QSet<QString> ids, names;
    for (const Profile &profile : parsed.profiles) {
        const QString name = profile.group.toCaseFolded() + QChar::Null + profile.name.toCaseFolded();
        if (ids.contains(profile.id) || names.contains(name)) return fail(error, I18n::text("Duplicate profile id or profile name in a group."));
        ids.insert(profile.id);
        names.insert(name);
    }
    parsed.revision = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    data = std::move(parsed);
    return true;
}

QByteArray ProfileStore::toXml(const ProfileData &input) {
    ProfileData data = input;
    assignHomeGroup(data);
    QByteArray bytes;
    QXmlStreamWriter xml(&bytes);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(2);
    xml.writeStartDocument();
    xml.writeStartElement("yusimanager");
    xml.writeAttribute("version", "1");
    if (!data.creatorVersion.isEmpty()) xml.writeAttribute("creatorVersion", data.creatorVersion);
    if (!data.language.isEmpty()) xml.writeAttribute("language", data.language);
    if (data.textSize != 0) xml.writeAttribute("textSize", QString::number(data.textSize, 'g', 15));
    QHash<QString, QVector<const Profile *>> grouped;
    for (const Profile &profile : data.profiles) {
        if (profile.group.isEmpty()) writeProfile(xml, profile);
        else grouped[profile.group].append(&profile);
    }
    for (const QString &group : data.groups) {
        xml.writeStartElement("group");
        xml.writeAttribute("name", group);
        const auto profiles = grouped.constFind(group);
        if (profiles != grouped.cend()) {
            for (const Profile *profile : profiles.value()) writeProfile(xml, *profile);
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    return xml.hasError() ? QByteArray() : bytes;
}

bool ProfileStore::revision(const QString &directory, QByteArray &result, QString *error) {
    QByteArray bytes;
    if (!readBytes(directory, bytes, true, error)) return false;
    result = QFileInfo::exists(filePath(directory)) ? QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) : QByteArray();
    return true;
}

bool ProfileStore::initialize(const QString &directory, QString *error) {
    if (QFileInfo::exists(filePath(directory)) || QFileInfo(filePath(directory)).isSymLink()) return true;
    return save(directory, ProfileData(), error);
}

bool ProfileStore::load(const QString &directory, ProfileData &data, QString *error) {
    QByteArray bytes;
    return readBytes(directory, bytes, false, error) && parse(bytes, data, error);
}

bool ProfileStore::save(const QString &directory, const ProfileData &data, QString *error) {
    if (error) error->clear();
    for (const Profile &profile : data.profiles) {
        if (!profile.group.isEmpty() && !data.groups.contains(profile.group)) return fail(error, I18n::text("A profile refers to an unknown group."));
    }
    const QByteArray bytes = toXml(data);
    ProfileData validated;
    if (!parse(bytes, validated, error)) return false;
    if (!QDir().mkpath(directory)) return fail(error, I18n::text("Could not create the data folder."));
    QLockFile lock(filePath(directory) + ".lock");
    if (!lock.tryLock(0)) return fail(error, I18n::text("The profile store is being updated. Please try again."));
    QByteArray current;
    if (!revision(directory, current, error)) return false;
    if (current != data.revision) return fail(error, I18n::text("The profile store changed. Reload and try again; your changes were not saved."));
    QSaveFile output(filePath(directory));
    if (!output.open(QIODevice::WriteOnly)) return fail(error, output.errorString());
    if (!output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || output.write(bytes) != bytes.size() || !output.commit()) return fail(error, output.errorString());
    return true;
}

ProfileStore::MergeResult ProfileStore::merge(ProfileData &current, const ProfileData &incoming) {
    MergeResult result;
    QHash<QString, QString> groups;
    QSet<QString> ids, names;
    const auto nameKey = [](const Profile &p) { return p.group.toCaseFolded() + QChar::Null + p.name.toCaseFolded(); };
    for (const QString &group : current.groups) groups.insert(group.toCaseFolded(), group);
    for (const Profile &p : current.profiles) { ids.insert(p.id); names.insert(nameKey(p)); }
    for (const QString &group : incoming.groups) {
        const QString key = group.toCaseFolded();
        if (groups.contains(key)) continue;
        groups.insert(key, group);
        current.groups.append(group);
        ++result.groupsAdded;
    }
    for (Profile p : incoming.profiles) {
        p.group = groups.value(p.group.toCaseFolded(), p.group);
        if (ids.contains(p.id) || names.contains(nameKey(p))) { ++result.profilesSkipped; continue; }
        ids.insert(p.id);
        names.insert(nameKey(p));
        current.profiles.append(p);
        ++result.profilesAdded;
    }
    return result;
}

bool ProfileStore::removeGroup(ProfileData &data, const QString &group, int &moved, int &renamed, QString *error) {
    moved = renamed = 0;
    if (group.compare("Home", Qt::CaseInsensitive) == 0) return fail(error, I18n::text("Home is the default group and cannot be removed."));
    if (!data.groups.contains(group)) return fail(error, I18n::text("The selected group no longer exists."));
    QString home = "Home";
    for (const QString &name : data.groups) if (name.compare(home, Qt::CaseInsensitive) == 0) { home = name; break; }
    QSet<QString> ids;
    for (const Profile &p : data.profiles) if (p.group == group) ids.insert(p.id);
    if (!assignProfiles(data, ids, home, moved, renamed, error)) return false;
    data.groups.removeAll(group);
    return true;
}

bool ProfileStore::assignProfiles(ProfileData &data, const QSet<QString> &ids, const QString &group,
                                  int &moved, int &renamed, QString *error) {
    moved = renamed = 0;
    if (!data.groups.contains(group) && group.compare("Home", Qt::CaseInsensitive) != 0)
        return fail(error, I18n::text("The target group no longer exists."));
    QSet<QString> available;
    for (const Profile &p : data.profiles) available.insert(p.id);
    if (!(ids - available).isEmpty()) return fail(error, I18n::text("A selected profile no longer exists."));
    QSet<QString> names;
    for (const Profile &p : data.profiles) if (p.group == group) names.insert(p.name.toCaseFolded());
    for (Profile &p : data.profiles) {
        if (!ids.contains(p.id) || p.group == group) continue;
        if (!data.groups.contains(group)) data.groups.append(group);
        if (names.contains(p.name.toCaseFolded())) {
            const QString base = p.name + " (" + p.group + ")";
            p.name = base;
            int suffix = 2;
            while (names.contains(p.name.toCaseFolded())) p.name = base + " " + QString::number(suffix++);
            ++renamed;
        }
        names.insert(p.name.toCaseFolded());
        p.group = group;
        ++moved;
    }
    return true;
}
