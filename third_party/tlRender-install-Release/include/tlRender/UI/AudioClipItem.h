// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/UI/IBasicItem.h>

#include <opentimelineio/clip.h>

namespace tl
{
    namespace ui
    {
        //! Audio clip item.
        class TL_API_TYPE AudioClipItem : public IBasicItem
        {
        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const OTIO_NS::SerializableObject::Retainer<OTIO_NS::Clip>&,
                double scale,
                const ItemOptions&,
                const DisplayOptions&,
                const std::shared_ptr<ItemData>&,
                const std::shared_ptr<IWidget>& parent);

            AudioClipItem();

        public:
            TL_API virtual ~AudioClipItem();

            //! Create a new item.
            TL_API static std::shared_ptr<AudioClipItem> create(
                const std::shared_ptr<ftk::Context>&,
                const OTIO_NS::SerializableObject::Retainer<OTIO_NS::Clip>&,
                double scale,
                const ItemOptions&,
                const DisplayOptions&,
                const std::shared_ptr<ItemData>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            TL_API void setScale(double) override;
            TL_API void setDisplayOptions(const DisplayOptions&) override;

            TL_API ftk::Size2I getSizeHint() const override;
            TL_API void setGeometry(const ftk::Box2I&) override;
            TL_API void tickEvent(
                bool,
                bool,
                const ftk::TickEvent&) override;
            TL_API void sizeHintEvent(const ftk::SizeHintEvent&) override;
            TL_API void clipEvent(const ftk::Box2I&, bool) override;
            TL_API void drawEvent(const ftk::Box2I&, const ftk::DrawEvent&) override;

        private:
            void _drawWaveforms(
                const ftk::Box2I&,
                const ftk::DrawEvent&);

            void _cancelRequests();

            FTK_PRIVATE();
        };
    }
}
