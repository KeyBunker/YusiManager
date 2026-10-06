#include "single_instance.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <unistd.h>

QString instanceServerName() {
    QString key = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()
                  + "|" + QString::number(getuid());
    return QString("YusiManager-%1").arg(qHash(key));
}

bool notifyRunningInstance(const QString &name, const QByteArray &msg) {
    QLocalSocket sock;
    sock.connectToServer(name, QIODevice::WriteOnly);
    if (!sock.waitForConnected(150)) return false;
    sock.write(msg);
    sock.flush();
    sock.waitForBytesWritten(150);
    sock.disconnectFromServer();
    return true;
}
