// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Timeline/TimeUnits.h>

#include <QMetaType>
#include <QObject>

namespace tl
{
    namespace qt
    {
        Q_NAMESPACE

        //! Time object.
        class TimeObject : public QObject
        {
            Q_OBJECT
            Q_PROPERTY(
                tl::TimeUnits timeUnits
                READ timeUnits
                WRITE setTimeUnits
                NOTIFY timeUnitsChanged)

        public:
            TimeObject(
                const std::shared_ptr<TimeUnitsModel>&,
                QObject* parent = nullptr);

            //! Get the time units.
            TimeUnits timeUnits() const;

        public Q_SLOTS:
            //! Set the time units.
            void setTimeUnits(tl::TimeUnits);

        Q_SIGNALS:
            //! This signal is emitted when the time units are changed.
            void timeUnitsChanged(tl::TimeUnits);

        private:
            std::shared_ptr<TimeUnitsModel> _model;
        };

        //! \name Serialize
        ///@{

        QDataStream& operator << (QDataStream&, const TimeUnits&);

        QDataStream& operator >> (QDataStream&, TimeUnits&);

        ///@}
    }
}
