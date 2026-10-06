#include "group_header_proxy.h"
#include <QBrush>
#include <QColor>
#include <QHash>
#include <algorithm>

GroupHeaderProxy::GroupHeaderProxy(QObject *parent) : QAbstractProxyModel(parent) {}

void GroupHeaderProxy::setSourceModel(QAbstractItemModel *src) {
    QAbstractProxyModel::setSourceModel(src);
    if (!src) return;
    connect(src, &QAbstractItemModel::modelReset,     this, &GroupHeaderProxy::handleSourceReset);
    connect(src, &QAbstractItemModel::rowsInserted,   this, &GroupHeaderProxy::handleSourceReset);
    connect(src, &QAbstractItemModel::rowsRemoved,    this, &GroupHeaderProxy::handleSourceReset);
    connect(src, &QAbstractItemModel::dataChanged,    this, &GroupHeaderProxy::handleSourceReset);
    connect(src, &QAbstractItemModel::layoutChanged,  this, &GroupHeaderProxy::handleSourceReset);
    handleSourceReset();
}

QModelIndex GroupHeaderProxy::mapToSource(const QModelIndex &proxyIndex) const {
    if (!proxyIndex.isValid() || proxyIndex.model() != this || !sourceModel()) return {};
    if (isHeaderRow(proxyIndex.row())) return {};
    int srow = sourceRowForProxy(proxyIndex.row());
    return sourceModel()->index(srow, proxyIndex.column());
}

QModelIndex GroupHeaderProxy::mapFromSource(const QModelIndex &sourceIndex) const {
    if (!sourceIndex.isValid() || sourceIndex.model() != sourceModel()
        || sourceIndex.row() >= m_proxyRows.size()) return {};
    return index(m_proxyRows[sourceIndex.row()], sourceIndex.column());
}

int GroupHeaderProxy::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_sourceRows.size();
}

int GroupHeaderProxy::columnCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);
    if (!sourceModel()) return 0;
    return sourceModel()->columnCount();
}

QModelIndex GroupHeaderProxy::index(int row, int column, const QModelIndex &parent) const {
    if (parent.isValid()) return {};
    if (row < 0 || column < 0 || row >= rowCount() || column >= columnCount()) return {};
    return createIndex(row, column, nullptr);
}

QModelIndex GroupHeaderProxy::parent(const QModelIndex &) const { return {}; }

QVariant GroupHeaderProxy::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid()) return {};
    if (isHeaderRow(idx.row())) {
        if (role == Qt::DisplayRole && idx.column()==ProfilesModel::ColName) {
            int h = m_headerRows.indexOf(idx.row());
            QString name = m_headerNames.value(h);
            if (name.trimmed().isEmpty()) name = "Home";
            return name;
        }
        if (role == Qt::BackgroundRole) return QBrush(QColor(50,50,50,180));
        if (role == Qt::ForegroundRole) return QBrush(Qt::white);
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignLeft | Qt::AlignVCenter);
        if (role == Qt::UserRole) return -1;
        if (role == Qt::UserRole+3) return true;
        return {};
    }
    if (!sourceModel()) return {};
    QModelIndex s = mapToSource(idx);
    QVariant v = sourceModel()->data(s, role);
    if (role == Qt::UserRole+3) return false;
    return v;
}

QVariant GroupHeaderProxy::headerData(int section, Qt::Orientation orientation, int role) const {
    return sourceModel() ? sourceModel()->headerData(section, orientation, role) : QVariant();
}

Qt::ItemFlags GroupHeaderProxy::flags(const QModelIndex &index) const {
    if (isHeaderRow(index.row())) return Qt::NoItemFlags;
    if (!sourceModel()) return Qt::NoItemFlags;
    return sourceModel()->flags(mapToSource(index));
}

void GroupHeaderProxy::setGroups(const QStringList &groups) {
    if (m_groups == groups) return;
    beginResetModel();
    m_groups = groups;
    rebuildCache();
    endResetModel();
}

void GroupHeaderProxy::setHideEmptyGroups(bool hide) {
    if (m_hideEmptyGroups == hide) return;
    m_hideEmptyGroups = hide;
    handleSourceReset();
}

bool GroupHeaderProxy::isHeaderRow(int proxyRow) const {
    return std::binary_search(m_headerRows.cbegin(), m_headerRows.cend(), proxyRow);
}

QString GroupHeaderProxy::headerName(int proxyRow) const {
    int i = m_headerRows.indexOf(proxyRow);
    return m_headerNames.value(i);
}

int GroupHeaderProxy::sourceRowForProxy(int proxyRow) const {
    return m_sourceRows.value(proxyRow, -1);
}

void GroupHeaderProxy::rebuildCache() {
    m_headerRows.clear();
    m_headerNames.clear();
    m_sourceRows.clear();
    m_proxyRows.clear();
    auto *src = sourceModel();
    if (!src) return;
    QStringList groups = m_groups;
    QHash<QString, QVector<int>> rows;
    m_proxyRows.resize(src->rowCount());
    for (int row = 0; row < src->rowCount(); ++row) {
        QString group = src->index(row, 0).data(Qt::UserRole + 2).toString();
        if (group.isEmpty()) group = "Home";
        if (!groups.contains(group, Qt::CaseInsensitive)) groups.append(group);
        rows[group.toCaseFolded()].append(row);
    }
    std::sort(groups.begin(), groups.end(), [](const QString &a, const QString &b) {
        const bool homeA = a.compare("Home", Qt::CaseInsensitive) == 0;
        const bool homeB = b.compare("Home", Qt::CaseInsensitive) == 0;
        if (homeA != homeB) return homeA;
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for (const QString &group : groups) {
        if ((m_hideEmptyGroups || group.compare("Home", Qt::CaseInsensitive) == 0)
            && !rows.contains(group.toCaseFolded())) continue;
        m_headerRows.append(m_sourceRows.size());
        m_headerNames.append(group);
        m_sourceRows.append(-1);
        const auto it = rows.constFind(group.toCaseFolded());
        if (it == rows.cend()) continue;
        for (int sourceRow : it.value()) {
            m_proxyRows[sourceRow] = m_sourceRows.size();
            m_sourceRows.append(sourceRow);
        }
    }
}

void GroupHeaderProxy::handleSourceReset() {
    beginResetModel();
    rebuildCache();
    endResetModel();
}
