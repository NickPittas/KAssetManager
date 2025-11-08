#pragma once

#include <QRect>
#include <QString>

// Preview-related helpers shared across delegates and controllers.
QRect insetPreviewRect(const QRect& source);
bool isPreviewableSuffix(const QString& suffix);
