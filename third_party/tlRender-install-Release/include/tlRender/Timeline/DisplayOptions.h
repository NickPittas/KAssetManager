// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Core/Export.h>

#include <ftk/Core/Image.h>
#include <ftk/Core/Matrix.h>
#include <ftk/Core/RenderOptions.h>

namespace tl
{
    //! Color values.
    struct TL_API_TYPE Color
    {
        bool     enabled    = false;
        ftk::V3F add        = ftk::V3F(0.F, 0.F, 0.F);
        ftk::V3F brightness = ftk::V3F(1.F, 1.F, 1.F);
        ftk::V3F contrast   = ftk::V3F(1.F, 1.F, 1.F);
        ftk::V3F saturation = ftk::V3F(1.F, 1.F, 1.F);
        float    hue        = 0.F;
        bool     invert     = false;

        TL_API bool operator == (const Color&) const;
        TL_API bool operator != (const Color&) const;
    };

    //! Get a color matrix.
    TL_API ftk::M44F color(const Color&);

    //! Levels values.
    struct TL_API_TYPE Levels
    {
        bool  enabled = false;
        float inLow   = 0.F;
        float inHigh  = 1.F;
        float gamma   = 1.F;
        float outLow  = 0.F;
        float outHigh = 1.F;

        TL_API bool operator == (const Levels&) const;
        TL_API bool operator != (const Levels&) const;
    };

    //! These values match the controls in exrdisplay for comparison and
    //! testing.
    struct TL_API_TYPE Exposure
    {
        bool  enabled  = false;
        float exposure = 0.F;
        float defog    = 0.F;
        float kneeLow  = 0.F;
        float kneeHigh = 5.F;
        float gamma    = 1.F;

        TL_API bool operator == (const Exposure&) const;
        TL_API bool operator != (const Exposure&) const;
    };

    //! Soft clip.
    struct TL_API_TYPE SoftClip
    {
        bool  enabled = false;
        float value   = 0.F;

        TL_API bool operator == (const SoftClip&) const;
        TL_API bool operator != (const SoftClip&) const;
    };

    //! Display options.
    struct TL_API_TYPE DisplayOptions
    {
        ftk::ChannelDisplay channels     = ftk::ChannelDisplay::Color;
        ftk::ImageMirror    mirror;
        Color               color;
        Levels              levels;
        Exposure            exposure;
        SoftClip            softClip;
        ftk::ImageFilters   imageFilters;

        TL_API bool operator == (const DisplayOptions&) const;
        TL_API bool operator != (const DisplayOptions&) const;
    };

    //! \name Serialize
    ///@{

    TL_API void to_json(nlohmann::json&, const Color&);
    TL_API void to_json(nlohmann::json&, const Levels&);
    TL_API void to_json(nlohmann::json&, const Exposure&);
    TL_API void to_json(nlohmann::json&, const SoftClip&);
    TL_API void to_json(nlohmann::json&, const DisplayOptions&);

    TL_API void from_json(const nlohmann::json&, Color&);
    TL_API void from_json(const nlohmann::json&, Levels&);
    TL_API void from_json(const nlohmann::json&, Exposure&);
    TL_API void from_json(const nlohmann::json&, SoftClip&);
    TL_API void from_json(const nlohmann::json&, DisplayOptions&);

    ///@}
}
