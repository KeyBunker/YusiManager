#pragma once
#include <QStringList>
#include "profile.h"

QStringList findTerminalCandidates();
QString detectTerminal();
QStringList buildSshCommand(const Profile &p);
QString shellQuoteArg(const QString &a);
QStringList wrapInTerminal(const QString &terminal,
                           const QStringList &sshCmd,
                           const QString &title = QString(),
                           bool hold = false,
                           const QString &terminalProfile = QString());
