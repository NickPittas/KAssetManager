// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Core/Export.h>

#include <ftk/Core/Box.h>

namespace tl
{
    //! Get a box with the given aspect ratio that fits within
    //! the given box.
    TL_API ftk::Box2I getBox(float aspect, const ftk::Box2I&);
}
