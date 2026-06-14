// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Core/Util.h>

#include <ftk/Core/Color.h>
#include <ftk/Core/FontSystem.h>

namespace tl
{
    //! Grid labels.
    enum class TL_API_TYPE GridLabels
    {
        None,
        Pixels,
        Alphanumeric,

        Count,
        First = None
    };
    TL_ENUM(GridLabels);

    //! Grid.
    struct TL_API_TYPE Grid
    {
        bool          enabled      = false;
        int           size         = 100;
        int           lineWidth    = 2;
        ftk::Color4F  color        = ftk::Color4F(0.F, 0.F, 0.F);
        GridLabels    labels       = GridLabels::None;
        ftk::Color4F  textColor    = ftk::Color4F(1.F, 1.F, 1.F);
        ftk::Color4F  overlayColor = ftk::Color4F(0.F, 0.F, 0.F, .5F);
        ftk::FontInfo fontInfo     = ftk::FontInfo(ftk::getDefaultFont(ftk::FontType::Mono), 12);
        int           textMargin   = 2;

        TL_API bool operator == (const Grid&) const;
        TL_API bool operator != (const Grid&) const;
    };

    //! Outline.
    struct TL_API_TYPE Outline
    {
        bool         enabled = false;
        int          width   = 2;
        ftk::Color4F color   = ftk::Color4F(1.F, 0.F, 0.F);

        TL_API bool operator == (const Outline&) const;
        TL_API bool operator != (const Outline&) const;
    };

    //! Foreground options.
    struct TL_API_TYPE ForegroundOptions
    {
        Grid    grid;
        Outline outline;

        TL_API bool operator == (const ForegroundOptions&) const;
        TL_API bool operator != (const ForegroundOptions&) const;
    };

    //! \name Serialize
    ///@{

    TL_API void to_json(nlohmann::json&, const Grid&);
    TL_API void to_json(nlohmann::json&, const Outline&);
    TL_API void to_json(nlohmann::json&, const ForegroundOptions&);

    TL_API void from_json(const nlohmann::json&, Grid&);
    TL_API void from_json(const nlohmann::json&, Outline&);
    TL_API void from_json(const nlohmann::json&, ForegroundOptions&);

    ///@}
}
