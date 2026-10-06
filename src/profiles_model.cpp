#include "i18n.h"
#include "profiles_model.h"
#include <algorithm>
#include <utility>

ProfilesModel::ProfilesModel(QObject *parent) : QAbstractTableModel(parent) {}

int ProfilesModel::rowCount(const QModelIndex &) const { return m_rows.size(); }
int ProfilesModel::columnCount(const QModelIndex &) const { return ColCount; }

QVariant ProfilesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return {};
    const int r = index.row();
    const int c = index.column();
    const Profile &p = m_rows[r];

    if (role == Qt::DisplayRole) {
        if (c == ColNum) {
            int gi = std::lower_bound(m_groupStarts.begin(), m_groupStarts.end(), r+1) - m_groupStarts.begin() - 1;
            int start = m_groupStarts.value(gi, 0);
            return r - start + 1;
        } else if (c == ColName) return p.name;
        else if (c == ColUser) return p.user;
        else if (c == ColHost) return p.host;
        else if (c == ColPort) return p.port;
        else if (c == ColKey) return p.keyfile.isEmpty() ? "agent" : p.keyfile;
        else if (c == ColRemoteDir) return p.remotedir;
    }

    if (role == Qt::TextAlignmentRole) {
        if (c == ColNum || c == ColPort) return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::UserRole) return r;
    if (role == Qt::UserRole + 1) return p.id;
    if (role == Qt::UserRole + 2) return p.group;

    return {};
}

QVariant ProfilesModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == ColNum) return "#";
        if (section == ColName) return I18n::text("Name");
        if (section == ColUser) return I18n::text("User");
        if (section == ColHost) return I18n::text("Host");
        if (section == ColPort) return I18n::text("Port");
        if (section == ColKey) return I18n::text("Key");
        if (section == ColRemoteDir) return I18n::text("Remote dir");
    }
    return {};
}

void ProfilesModel::setProfiles(QVector<Profile> list) {
    beginResetModel();
    std::stable_sort(list.begin(), list.end(), [](const Profile &a, const Profile &b){
        if (a.group.compare(b.group, Qt::CaseInsensitive) == 0)
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        return a.group.compare(b.group, Qt::CaseInsensitive) < 0;
    });
    m_rows = std::move(list);
    rebuildGroups();
    endResetModel();
}

const Profile& ProfilesModel::at(int row) const { return m_rows[row]; }

void ProfilesModel::rebuildGroups() {
    m_groupStarts.clear();
    m_groupNames.clear();
    QString prev;
    for (int i=0;i<m_rows.size();++i) {
        const QString g = m_rows[i].group;
        if (i==0 || g.compare(prev, Qt::CaseInsensitive)!=0) {
            m_groupStarts.push_back(i);
            m_groupNames.push_back(g);
            prev = g;
        }
    }
}

QVector<int> ProfilesModel::groupStartRows() const { return m_groupStarts; }
QString ProfilesModel::groupOfRow(int row) const {
    int gi = std::lower_bound(m_groupStarts.begin(), m_groupStarts.end(), row+1) - m_groupStarts.begin() - 1;
    if (gi < 0) gi = 0;
    return m_groupNames.value(gi);
}
