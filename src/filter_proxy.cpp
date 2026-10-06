#include "filter_proxy.h"

ProfilesFilterProxy::ProfilesFilterProxy(QObject *parent) : QSortFilterProxyModel(parent) {
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
}

void ProfilesFilterProxy::setNeedle(const QString &s) {
    const QString needle = s.trimmed();
    if (needle.compare(m_needle, Qt::CaseInsensitive) == 0) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_needle = needle;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

bool ProfilesFilterProxy::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const {
    if (m_needle.isEmpty()) return true;
    auto idx = [&](int col){ return sourceModel()->index(source_row, col, source_parent); };
    for (int c=0;c<sourceModel()->columnCount();++c) {
        const QString v = sourceModel()->data(idx(c), Qt::DisplayRole).toString();
        if (v.contains(m_needle, Qt::CaseInsensitive)) return true;
    }
    const QString grp = sourceModel()->data(idx(0), Qt::UserRole+2).toString();
    if (grp.contains(m_needle, Qt::CaseInsensitive)) return true;
    return false;
}
