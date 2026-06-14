// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Core/Util.h>

#include <ftk/Core/Color.h>
#include <ftk/Core/Size.h>
#include <ftk/Core/Util.h>

namespace tl
{
    //! Background type.
    enum class TL_API_TYPE Background
    {
        Solid,
        Checkers,
        Gradient,

        Count,
        First = Solid
    };
    TL_ENUM(Background);

    //! Background options.
    struct TL_API_TYPE BackgroundOptions
    {
        Background type = Background::Solid;

        ftk::Color4F solidColor = ftk::Color4F(0.F, 0.F, 0.F);

        std::pair<ftk::Color4F, ftk::Color4F> checkersColor =
        {
            ftk::Color4F(0.F, 0.F, 0.F),
            ftk::Color4F(1.F, 1.F, 1.F)
        };
        ftk::Size2I checkersSize = ftk::Size2I(100, 100);

        std::pair<ftk::Color4F, ftk::Color4F> gradientColor =
        {
            ftk::Color4F(0.F, 0.F, 0.F),
            ftk::Color4F(1.F, 1.F, 1.F)
        };

        TL_API bool operator == (const BackgroundOptions&) const;
        TL_API bool operator != (const BackgroundOptions&) const;
    };

    //! \name Serialize
    ///@{

    TL_API void to_json(nlohmann::json&, const BackgroundOptions&);

    TL_API void from_json(const nlohmann::json&, BackgroundOptions&);

    ///@}
}
