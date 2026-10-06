#include "i18n.h"
#include "ssh_config.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QLockFile>
#include <QRegularExpression>
#include <QHash>

namespace {
const QByteArray Begin = "# BEGIN YusiManager SSH defaults";
const QByteArray Joined = "# BEGIN YusiManager SSH defaults (joined)";
const QByteArray End = "# END YusiManager SSH defaults";
constexpr qint64 Limit = 1024 * 1024;
QVector<SshConfig::Rule> rules() {
    return {{"IdentitiesOnly", "yes", {}, false}, {"PubkeyAuthentication", "yes", {}, false},
        {"ServerAliveInterval", "30", {}, false}, {"ServerAliveCountMax", "10", {}, false},
        {"ControlMaster", "auto", {}, false}, {"ControlPath", "~/.ssh/control-%r@%h:%p", {}, false},
        {"ControlPersist", "5m", {}, false}};
}
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
QString content(const QByteArray &line) {
    QString text = QString::fromUtf8(line).trimmed();
    bool quoted = false, escaped = false;
    for (int i = 0; i < text.size(); ++i) {
        if (escaped) { escaped = false; continue; }
        if (text[i] == '\\') { escaped = true; continue; }
        if (text[i] == '"') quoted = !quoted;
        if (text[i] == '#' && !quoted) { text.truncate(i); break; }
    }
    return text.trimmed();
}
QPair<QString, QString> directive(const QByteArray &line) {
    static const QRegularExpression re("^([^\\s=]+)(?:\\s*=\\s*|\\s+)(.*)$");
    const auto match = re.match(content(line));
    QString value = match.captured(2).trimmed();
    if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2) value = value.mid(1, value.size()-2);
    return {match.captured(1).toLower(), value};
}
bool read(const QString &path, QByteArray &bytes, bool &exists, QString *error) {
    const QFileInfo info(path);
    if (info.isSymLink()) return fail(error, I18n::text("The SSH config is a symbolic link. Edit its target manually."));
    exists = info.exists();
    if (!exists) { bytes.clear(); return true; }
    if (!info.isFile()) return fail(error, I18n::text("The SSH config is not a regular file."));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    bytes = file.read(Limit + 1);
    if (bytes.size() > Limit || file.error() != QFileDevice::NoError) return fail(error, I18n::text("Cannot read the SSH config, or it exceeds 1 MiB."));
    return true;
}
struct Parsed {
    QByteArray base;
    QHash<QString, QString> existing, managed;
    bool hasBlock = false, includes = false;
};
bool parse(const QByteArray &bytes, Parsed &parsed, QString *error) {
    parsed = {};
    qsizetype begin = -1, end = -1, position = 0;
    bool joined = false, inside = false, host = false;
    const auto defaults = rules();
    for (const QByteArray &line : bytes.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed == Begin || trimmed == Joined) {
            if (begin >= 0) return fail(error, I18n::text("Multiple YusiManager SSH blocks found. Review ~/.ssh/config manually."));
            begin = position; joined = trimmed == Joined; inside = true;
        } else if (trimmed == End) {
            if (!inside) return fail(error, I18n::text("Incomplete YusiManager SSH block. Review ~/.ssh/config manually."));
            end = qMin(position + line.size() + 1, bytes.size()); inside = false;
        } else if (inside && !content(line).isEmpty()) {
            const auto [key, value] = directive(line);
            if (key == "host" && value == "*" && !host && parsed.managed.isEmpty()) { host = true; }
            else {
                bool allowed = false;
                for (const auto &rule : defaults) if (key == rule.key.toLower() && value == rule.recommended) allowed = true;
                if (!host || !allowed || parsed.managed.contains(key))
                    return fail(error, I18n::text("The YusiManager SSH block was edited manually. Review it before using Add or Remove."));
                parsed.managed.insert(key, value);
            }
        }
        position += line.size() + 1;
    }
    if (inside || (begin >= 0 && !host)) return fail(error, I18n::text("Incomplete YusiManager SSH block. Review ~/.ssh/config manually."));
    parsed.hasBlock = begin >= 0;
    parsed.base = bytes;
    if (parsed.hasBlock) {
        for (const QByteArray &line : bytes.mid(end).split('\n')) {
            if (content(line).isEmpty()) continue;
            const auto [key, value] = directive(line);
            Q_UNUSED(value);
            if (key != "host" && key != "match") return fail(error, I18n::text("Options were added after the YusiManager block. Put them in an explicit Host or Match section before using Add or Remove."));
            break;
        }
        qsizetype start = begin;
        if (joined && end == bytes.size() && start > 0 && bytes[start-1] == '\n') --start;
        parsed.base.remove(start, end-start);
    }
    bool global = true;
    for (const QByteArray &line : parsed.base.split('\n')) {
        const auto [key, value] = directive(line);
        if (key == "host") global = value == "*";
        else if (key == "match") global = value.compare("all", Qt::CaseInsensitive) == 0;
        else if (key == "include") { parsed.includes = true; global = false; }
        else if (global && !key.isEmpty() && !parsed.existing.contains(key)) parsed.existing.insert(key, value);
    }
    return true;
}
bool write(const QString &path, const QByteArray &before, bool existed, const QByteArray &after, QString *error) {
    const QString directory = QFileInfo(path).absolutePath();
    const bool newDirectory = !QDir(directory).exists();
    if (!QDir().mkpath(directory)) return fail(error, I18n::text("Could not create the SSH directory."));
    if (newDirectory && !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return fail(error, I18n::text("Could not secure the SSH directory."));
    QLockFile lock(path + ".yusimanager.lock");
    if (!lock.tryLock(0)) return fail(error, I18n::text("SSH settings are being updated. Try again."));
    QByteArray current; bool exists;
    if (!read(path, current, exists, error)) return false;
    if (exists != existed || current != before) return fail(error, I18n::text("SSH config changed. Refresh and try again."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(after) != after.size() || !file.commit()) return fail(error, file.errorString());
    return true;
}
}

QString SshConfig::defaultPath() { return QDir::home().filePath(".ssh/config"); }
bool SshConfig::inspect(const QString &path, State &state, QString *error) {
    QByteArray bytes; bool exists; Parsed parsed;
    if (!read(path, bytes, exists, error) || !parse(bytes, parsed, error)) return false;
    state = {}; state.rules = rules(); state.managed = parsed.hasBlock; state.includes = parsed.includes;
    for (auto &rule : state.rules) {
        const QString key = rule.key.toLower();
        if (parsed.existing.contains(key)) rule.current = parsed.existing.value(key);
        else if (parsed.managed.contains(key)) { rule.current = parsed.managed.value(key); rule.managed = true; }
        else ++state.missing;
    }
    return true;
}
bool SshConfig::addMissing(const QString &path, QString *error) {
    QByteArray bytes; bool exists; Parsed parsed;
    if (!read(path, bytes, exists, error) || !parse(bytes, parsed, error)) return false;
    QByteArray entries;
    const QByteArray newline = bytes.contains("\r\n") ? QByteArray("\r\n") : QByteArray("\n");
    for (const auto &rule : rules()) {
        if (!parsed.existing.contains(rule.key.toLower())) entries += "  " + rule.key.toUtf8() + ' ' + rule.recommended.toUtf8() + newline;
    }
    if (entries.isEmpty()) return true;
    QByteArray result = parsed.base;
    const bool join = !result.isEmpty() && !result.endsWith('\n');
    if (join) result += '\n';
    result += (join ? Joined : Begin) + newline + "Host *" + newline + entries + End + newline;
    if (result == bytes) return true;
    return write(path, bytes, exists, result, error);
}
bool SshConfig::removeAdded(const QString &path, QString *error) {
    QByteArray bytes; bool exists; Parsed parsed;
    if (!read(path, bytes, exists, error) || !parse(bytes, parsed, error)) return false;
    if (!parsed.hasBlock) return true;
    return write(path, bytes, exists, parsed.base, error);
}
