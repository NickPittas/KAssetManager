// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/IO/IO.h>

#include <ftk/Core/Path.h>

namespace tl
{
    //! Image sequence audio options.
    enum class TL_API_TYPE ImageSeqAudio
    {
        None,     //!< No audio
        Ext,      //!< Search for an audio file by extension
        FileName, //!< Use the given audio file name

        Count,
        First = None
    };
    TL_ENUM(ImageSeqAudio);

    //! Timeline options.
    struct TL_API_TYPE Options
    {
        //! Image sequence audio.
        ImageSeqAudio imageSeqAudio = ImageSeqAudio::Ext;

        //! Image sequence audio extensions.
        std::vector<std::string> imageSeqAudioExts = { ".mp3", ".wav" };

        //! Image sequence audio file name.
        std::string imageSeqAudioFileName;

        //! Enable workarounds for timelines that may not conform exactly
        //! to specification.
        bool compat = true;

        //! Maximum number of video requests.
        size_t videoRequestMax = 16;

        //! Maximum number of audio requests.
        size_t audioRequestMax = 16;

        //! Request timeout.
        std::chrono::milliseconds requestTimeout = std::chrono::milliseconds(5);

        //! I/O options.
        IOOptions ioOptions;

        //! Path options.
        ftk::PathOptions pathOptions;

        TL_API bool operator == (const Options&) const;
        TL_API bool operator != (const Options&) const;
    };
}
