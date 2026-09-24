// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Timeline/IRender.h>
#include <tlRender/Timeline/Player.h>
#include <tlRender/Timeline/TimeUnits.h>

#include <tlRender/Core/AudioSystem.h>

#include <ftk/Core/RenderOptions.h>

#include <QMetaType>

Q_DECLARE_METATYPE(ftk::AlphaBlend);
Q_DECLARE_METATYPE(ftk::ChannelDisplay);
Q_DECLARE_METATYPE(ftk::ImageType);
Q_DECLARE_METATYPE(ftk::ImageFilter);
Q_DECLARE_METATYPE(ftk::InputVideoLevels);
Q_DECLARE_METATYPE(ftk::Size2I);

Q_DECLARE_METATYPE(tl::AudioType);

Q_DECLARE_METATYPE(tl::FileType);

Q_DECLARE_METATYPE(tl::Compare);
Q_DECLARE_METATYPE(tl::CompareTime);
Q_DECLARE_METATYPE(tl::ImageSeqAudio);
Q_DECLARE_METATYPE(tl::Loop);
Q_DECLARE_METATYPE(tl::Playback);
Q_DECLARE_METATYPE(tl::TimeAction);
Q_DECLARE_METATYPE(tl::TimeUnits);
Q_DECLARE_METATYPE(tl::Transition);
