#pragma once
#include <QSortFilterProxyModel>

class ProfilesFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ProfilesFilterProxy(QObject *parent=nullptr);
    void setNeedle(const QString &s);
protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;
private:
    QString m_needle;
};
