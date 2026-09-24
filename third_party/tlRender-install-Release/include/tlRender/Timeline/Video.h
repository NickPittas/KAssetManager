// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Timeline/Transition.h>

#include <tlRender/Core/Time.h>

#include <ftk/Core/Image.h>
#include <ftk/Core/RenderOptions.h>

namespace tl
{
    //! Video layer.
    struct TL_API_TYPE VideoLayer
    {
        std::shared_ptr<ftk::Image> image;
        ftk::ImageOptions           imageOptions;

        std::shared_ptr<ftk::Image> imageB;
        ftk::ImageOptions           imageOptionsB;

        Transition                  transition      = Transition::None;
        float                       transitionValue = 0.F;

        TL_API bool operator == (const VideoLayer&) const;
        TL_API bool operator != (const VideoLayer&) const;
    };

    //! Video frame.
    struct TL_API_TYPE VideoFrame
    {
        ftk::Size2I             size;
        OTIO_NS::RationalTime   time   = invalidTime;
        std::vector<VideoLayer> layers;

        TL_API bool operator == (const VideoFrame&) const;
        TL_API bool operator != (const VideoFrame&) const;
    };

    //! Compare the time values of video frames.
    TL_API bool isTimeEqual(const VideoFrame&, const VideoFrame&);
}
