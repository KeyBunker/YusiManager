#include "profile.h"
#include <QDir>

QStringList terminalColorProfiles() {
    QStringList profiles;
    for (int i = 1; i <= 20; ++i) profiles << QString("Yusi-%1").arg(i);
    return profiles;
}

QString defaultProfilesDirectory() {
    return QDir::home().filePath(".yusimanager");
}

QString trimAll(const QString &s) {
    QString t = s;
    t.replace("\r", "");
    return t.trimmed();
}

QString expandPath(QString p) {
    p = trimAll(p);
    if (p.startsWith("~")) p.replace(0, 1, QDir::homePath());
    return p;
}

QString normalizeHostForSSH(QString h) {
    if (h.startsWith('[') && h.endsWith(']')) h = h.mid(1, h.size() - 2);
    return h;
}
