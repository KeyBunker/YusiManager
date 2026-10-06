#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include "profile.h"

class ProfilesModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Columns { ColNum=0, ColName, ColUser, ColHost, ColPort, ColKey, ColRemoteDir, ColCount };
    explicit ProfilesModel(QObject *parent=nullptr);
    int rowCount(const QModelIndex &parent=QModelIndex()) const override;
    int columnCount(const QModelIndex &parent=QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void setProfiles(QVector<Profile> list);
    const Profile& at(int row) const;
    QVector<int> groupStartRows() const;
    QString groupOfRow(int row) const;

private:
    QVector<Profile> m_rows;
    QVector<int> m_groupStarts;
    QVector<QString> m_groupNames;
    void rebuildGroups();
};
