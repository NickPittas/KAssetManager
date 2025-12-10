#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include <QTimer>

#include "project_db.h"

/**
 * @brief Model for listing projects in the Project Manager.
 */
class ProjectsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        WatchPathRole,
        CreatedAtRole,
        UpdatedAtRole,
        AssetCountRole,
        UnreadCountRole
    };

    explicit ProjectsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int createProject(const QString& name, const QString& watchPath);
    Q_INVOKABLE bool renameProject(int id, const QString& name);
    Q_INVOKABLE bool deleteProject(int id);
    Q_INVOKABLE QVariantMap get(int row) const;

public slots:
    void reload();
    void refresh() { reload(); }

private slots:
    void scheduleReload();

private:
    QVector<Project> m_projects;
    QTimer m_reloadTimer;
};
