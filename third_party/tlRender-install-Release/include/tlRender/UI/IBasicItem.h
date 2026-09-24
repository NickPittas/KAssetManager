// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/UI/IItem.h>

#include <opentimelineio/gap.h>

namespace tl
{
    namespace ui
    {
        //! Base class for clips, gaps, and other items.
        class TL_API_TYPE IBasicItem : public IItem
        {
        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::string& label,
                const ftk::Color4F&,
                const std::string& objectName,
                const OTIO_NS::SerializableObject::Retainer<OTIO_NS::Item>&,
                double scale,
                const ItemOptions&,
                const DisplayOptions&,
                const std::shared_ptr<ItemData>&,
                const std::shared_ptr<ftk::IWidget>& parent = nullptr);

            IBasicItem();

        public:
            TL_API virtual ~IBasicItem() = 0;

            TL_API void setScale(double) override;
            TL_API void setDisplayOptions(const DisplayOptions&) override;

            TL_API ftk::Size2I getSizeHint() const override;
            TL_API void setGeometry(const ftk::Box2I&) override;
            TL_API void styleEvent(const ftk::StyleEvent&) override;
            TL_API void sizeHintEvent(const ftk::SizeHintEvent&) override;
            TL_API void clipEvent(const ftk::Box2I&, bool) override;
            TL_API void drawEvent(const ftk::Box2I&, const ftk::DrawEvent&) override;

        protected:
            int _getMargin() const;
            int _getLineHeight() const;
            ftk::Box2I _getInsideGeometry() const;

            void _timeUnitsUpdate() override;

        private:
            void _textUpdate();

            FTK_PRIVATE();
        };
    }
}
