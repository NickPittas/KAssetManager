// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Qt/TimeObject.h>

#include <QWidget>

#include <memory>

namespace tl
{
    namespace qtwidget
    {
        //! Time label.
        class TimeLabel : public QWidget
        {
            Q_OBJECT
            Q_PROPERTY(
                OTIO_NS::RationalTime value
                READ value
                WRITE setValue)
            Q_PROPERTY(
                tl::TimeUnits timeUnits
                READ timeUnits
                WRITE setTimeUnits)

        public:
            TimeLabel(QWidget* parent = nullptr);

            virtual ~TimeLabel();

            //! Set the time object.
            void setTimeObject(qt::TimeObject*);

            //! Get the time value.
            const OTIO_NS::RationalTime& value() const;

            //! Get the time units.
            TimeUnits timeUnits() const;

        public Q_SLOTS:
            //! Set the time value.
            void setValue(const OTIO_NS::RationalTime&);

            //! Set the time units.
            void setTimeUnits(tl::TimeUnits);

        private:
            void _textUpdate();

            FTK_PRIVATE();
        };
    }
}
