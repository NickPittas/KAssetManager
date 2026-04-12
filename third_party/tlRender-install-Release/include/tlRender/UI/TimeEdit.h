// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Core/Export.h>

#include <ftk/UI/IWidget.h>

#include <opentimelineio/version.h>

namespace tl
{
    class TimeUnitsModel;

    namespace ui
    {
        //! Time value editor.
        class TL_API_TYPE TimeEdit : public ftk::IWidget
        {
            FTK_NON_COPYABLE(TimeEdit);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<TimeUnitsModel>&,
                const std::shared_ptr<IWidget>& parent);

            TimeEdit();

        public:
            TL_API virtual ~TimeEdit();

            //! Create a new widget.
            TL_API static std::shared_ptr<TimeEdit> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<TimeUnitsModel>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            //! Get the time units model.
            TL_API const std::shared_ptr<TimeUnitsModel>& getTimeUnitsModel() const;

            //! Get the time value.
            TL_API const OTIO_NS::RationalTime& getValue() const;

            //! Set the time value.
            TL_API void setValue(const OTIO_NS::RationalTime&);

            //! Set the time value callback.
            TL_API void setCallback(const std::function<void(const OTIO_NS::RationalTime&)>&);

            //! Select all.
            TL_API void selectAll();

            //! Set the font.
            TL_API void setFont(ftk::FontType);

            TL_API ftk::Size2I getSizeHint() const override;
            TL_API void setGeometry(const ftk::Box2I&) override;
            TL_API void takeKeyFocus() override;
            TL_API void keyPressEvent(ftk::KeyEvent&) override;
            TL_API void keyReleaseEvent(ftk::KeyEvent&) override;

        private:
            void _commitValue(const std::string&);
            void _commitValue(const OTIO_NS::RationalTime&);
            void _textUpdate();

            FTK_PRIVATE();
        };
    }
}
