// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/UI/IBasicItem.h>

#include <opentimelineio/gap.h>

namespace tl
{
    namespace ui
    {
        //! Gap item.
        class TL_API_TYPE GapItem : public IBasicItem
        {
        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const ftk::Color4F&,
                const OTIO_NS::SerializableObject::Retainer<OTIO_NS::Gap>&,
                double scale,
                const ItemOptions&,
                const DisplayOptions&,
                const std::shared_ptr<ItemData>&,
                const std::shared_ptr<IWidget>& parent);

            GapItem();

        public:
            TL_API virtual ~GapItem();

            //! Create a new item.
            TL_API static std::shared_ptr<GapItem> create(
                const std::shared_ptr<ftk::Context>&,
                const ftk::Color4F&,
                const OTIO_NS::SerializableObject::Retainer<OTIO_NS::Gap>&,
                double scale,
                const ItemOptions&,
                const DisplayOptions&,
                const std::shared_ptr<ItemData>&,
                const std::shared_ptr<IWidget>& parent = nullptr);
        };
    }
}
