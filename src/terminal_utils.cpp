#include "terminal_utils.h"
#include <QStandardPaths>
#include <QFileInfo>

QStringList findTerminalCandidates() {
    return {
        qEnvironmentVariable("TERMINAL"),
        "gnome-terminal", "konsole", "xfce4-terminal", "kitty", "alacritty",
        "tilix", "mate-terminal", "xterm", "lxterminal", "terminator", "urxvt", "st"
    };
}

QString detectTerminal() {
    for (const QString &t : findTerminalCandidates()) {
        if (t.isEmpty()) continue;
        if (!QStandardPaths::findExecutable(t).isEmpty()) return t;
    }
    return {};
}

QStringList buildSshCommand(const Profile &p) {
    QString host = normalizeHostForSSH(p.host);
    QStringList cmd{ "ssh", "-p", p.port, "-o", "IdentitiesOnly=yes",
                     "-o", "ServerAliveInterval=30", "-o", "ServerAliveCountMax=3" };
    if (!p.keyfile.isEmpty() && p.keyfile.toLower() != "agent") cmd << "-i" << expandPath(p.keyfile);
    cmd << QString("%1@%2").arg(p.user, host);
    return cmd;
}

QString shellQuoteArg(const QString &a) {
    QString q = a;
    q.replace("'", "'\\''");
    return "'" + q + "'";
}

QStringList wrapInTerminal(const QString &terminal,
                           const QStringList &sshCmd,
                           const QString &title,
                           bool hold,
                           const QString &terminalProfile)
{
    if (sshCmd.isEmpty() || terminal.isEmpty()
        || QStandardPaths::findExecutable(terminal).isEmpty()) return {};

    QStringList command = sshCmd;
    if (hold) {
        command = QStringList{"bash", "--noprofile", "--norc", "-c",
            "\"$@\"; status=$?; printf '\\n[enter] om te sluiten'; read -r; exit \"$status\"",
            "yusi-hold"} << sshCmd;
    }
    QStringList quoted;
    for (const QString &arg : command) quoted << shellQuoteArg(arg);
    const QString commandText = quoted.join(" ");
    const QString kind = QFileInfo(terminal).fileName();
    QStringList args;
    const auto addTitle = [&](const QString &flag) {
        if (!title.isEmpty()) args << flag << title;
    };

    if (kind == "konsole") {
        args << "--separate";
        if (!terminalProfile.isEmpty()) args << "--profile" << terminalProfile;
        if (!title.isEmpty()) {
            args << "-p" << "LocalTabTitleFormat=" + title
                 << "-p" << "RemoteTabTitleFormat=" + title;
        }
        args << "-e" << command;
    } else if (kind == "gnome-terminal") {
        args << "--window";
        if (!terminalProfile.isEmpty()) args << "--profile" << terminalProfile;
        addTitle("--title");
        args << "--" << command;
    } else if (kind == "mate-terminal") {
        args << "--window";
        if (!terminalProfile.isEmpty()) args << "--profile" << terminalProfile;
        addTitle("--title");
        args << "-x" << command;
    } else if (kind == "xfce4-terminal") {
        args << "--disable-server";
        addTitle("--title");
        args << "--execute" << command;
    } else if (kind == "kitty") {
        addTitle("--title");
        args << "--" << command;
    } else if (kind == "alacritty") {
        addTitle("--title");
        args << "-e" << command;
    } else if (kind == "tilix") {
        args << "--action=app-new-window";
        if (!terminalProfile.isEmpty()) args << "--profile" << terminalProfile;
        addTitle("--title");
        args << "-e" << command;
    } else if (kind == "xterm" || kind == "urxvt" || kind == "st") {
        addTitle("-T");
        args << "-e" << command;
    } else if (kind == "lxterminal") {
        if (!terminalProfile.isEmpty()) args << "--profile=" + terminalProfile;
        if (!title.isEmpty()) args << "--title=" + QString(title).replace(',', QChar(0xFF0C));
        args << "--command=" + commandText;
    } else if (kind == "terminator") {
        if (!terminalProfile.isEmpty()) args << "--profile" << terminalProfile;
        addTitle("--title");
        args << "-x" << command;
    } else {
        return {};
    }
    return QStringList{terminal} << args;
}
