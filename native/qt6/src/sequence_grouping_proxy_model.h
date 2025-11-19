#ifndef SEQUENCE_GROUPING_PROXY_MODEL_H
#define SEQUENCE_GROUPING_PROXY_MODEL_H

#include <QSortFilterProxyModel>
#include <QFutureWatcher>
#include <QSet>
#include <QHash>
#include <QDir>
#include <QtConcurrent>
#include <QFileSystemModel>

class SequenceGroupingProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    struct Info {
        QString dir;
        QString base;
        QString ext;
        int start = -1;
        int end = -1;
        int count = 0;
        QString reprPath; // first frame path
    };

    explicit SequenceGroupingProxyModel(QObject* parent=nullptr);

    void setGroupingEnabled(bool on);
    bool groupingEnabled() const;

    void setHideFolders(bool hide);
    bool hideFolders() const;

    void rebuildForRoot(const QString& dirPath);

    bool isRepresentativeProxyIndex(const QModelIndex& proxyIdx) const;

    Info infoForProxyIndex(const QModelIndex& proxyIdx) const;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;

    QVariant data(const QModelIndex &proxyIndex, int role) const override;

    // Always place folders before files regardless of sort order
    bool lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const override;

private:
    struct BuildResult {
        QString dirPath;
        QSet<QString> hidden;
        QHash<QString, Info> infoByRepr;
        QHash<QString, QString> keyByRepr;
    };

    bool m_enabled = true;
    bool m_hideFolders = false;
    QSet<QString> m_hidden; // absolute file paths to hide
    QHash<QString, Info> m_infoByRepr; // reprPath -> info
    QHash<QString, QString> m_keyByRepr; // reprPath -> grouping key
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    QFutureWatcher<BuildResult> m_buildWatcher;
    QString m_requestedDir;
    QString m_pendingDir;
    QString m_activeDir;

    void startBuild(const QString& dirPath);
    void applyBuildResult(BuildResult&& result);
    static BuildResult buildSequences(const QString& dirPath);
};

#endif // SEQUENCE_GROUPING_PROXY_MODEL_H
