#pragma once

#include <QFileIconProvider>

class FmIconProvider : public QFileIconProvider
{
public:
    FmIconProvider();

    QIcon icon(const QFileInfo& info) const override;
};
