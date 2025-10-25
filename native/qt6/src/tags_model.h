#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QPair>
#include "db.h"

class TagsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole };
    explicit TagsModel(QObject* parent=nullptr): QAbstractListModel(parent) {
        connect(&DB::instance(), &DB::tagsChanged, this, &TagsModel::reload);
        reload();
    }

    int rowCount(const QModelIndex& parent) const override { Q_UNUSED(parent); return m_rows.size(); }
    QVariant data(const QModelIndex& idx, int role) const override {
        if (!idx.isValid() || idx.row()<0 || idx.row()>=m_rows.size()) return {};
        const auto &p = m_rows[idx.row()];
        if (role == IdRole) return p.first;
        if (role == NameRole || role == Qt::DisplayRole) return p.second;
        return {};
    }
    QHash<int,QByteArray> roleNames() const override { QHash<int,QByteArray> r; r[IdRole]="id"; r[NameRole]="name"; return r; }

    Q_INVOKABLE int createTag(const QString& name) { int id=DB::instance().createTag(name); reload(); return id; }
    Q_INVOKABLE bool renameTag(int id, const QString& name) { bool ok=DB::instance().renameTag(id,name); if (ok) reload(); return ok; }
    Q_INVOKABLE bool deleteTag(int id) { bool ok=DB::instance().deleteTag(id); if (ok) reload(); return ok; }

public slots:
    void reload() {
        beginResetModel();
        m_rows = DB::instance().listTags();
        endResetModel();
    }

private:
    QVector<QPair<int,QString>> m_rows;
};
