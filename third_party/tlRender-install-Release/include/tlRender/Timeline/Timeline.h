// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Timeline/Audio.h>
#include <tlRender/Timeline/TimelineOptions.h>
#include <tlRender/Timeline/Video.h>

#include <ftk/Core/Path.h>

#include <opentimelineio/timeline.h>

#include <future>

namespace ftk
{
    class Context;
}

namespace tl
{
    //! Create an OTIO timeline from a path. The path can point to an .otio
    //! file, .otioz file, movie file, or image sequence.
    TL_API OTIO_NS::SerializableObject::Retainer<OTIO_NS::Timeline> create(
        const std::shared_ptr<ftk::Context>&,
        const ftk::Path&,
        const Options& = Options());

    //! Create an OTIO timeline from a path and audio path. The file name
    //! can point to an .otio file, .otioz file, movie file, or image
    //! sequence.
    TL_API OTIO_NS::SerializableObject::Retainer<OTIO_NS::Timeline> create(
        const std::shared_ptr<ftk::Context>&,
        const ftk::Path& path,
        const ftk::Path& audioPath,
        const Options& = Options());

    //! Video size request.
    struct TL_API_TYPE VideoSizeRequest
    {
        uint64_t id = 0;
        std::future<size_t> future;
    };

    //! Video request.
    struct TL_API_TYPE VideoRequest
    {
        uint64_t id = 0;
        std::future<VideoFrame> future;
    };

    //! Audio size request.
    struct TL_API_TYPE AudioSizeRequest
    {
        uint64_t id = 0;
        std::future<size_t> future;
    };

    //! Audio request.
    struct TL_API_TYPE AudioRequest
    {
        uint64_t id = 0;
        std::future<AudioFrame> future;
    };

    //! Timeline.
    class TL_API_TYPE Timeline : public std::enable_shared_from_this<Timeline>
    {
        FTK_NON_COPYABLE(Timeline);

    protected:
        void _init(
            const std::shared_ptr<ftk::Context>&,
            const OTIO_NS::SerializableObject::Retainer<OTIO_NS::Timeline>&,
            const Options&);

        Timeline();

    public:
        TL_API ~Timeline();

        //! Create a new timeline.
        TL_API static std::shared_ptr<Timeline> create(
            const std::shared_ptr<ftk::Context>&,
            const OTIO_NS::SerializableObject::Retainer<OTIO_NS::Timeline>&,
            const Options& = Options());

        //! Create a new timeline from a path. The path can point to an
        //! .otio file, movie file, or image sequence.
        TL_API static std::shared_ptr<Timeline> create(
            const std::shared_ptr<ftk::Context>&,
            const ftk::Path&,
            const Options& = Options());

        //! Create a new timeline from a path and audio path. The path can
        //! point to an .otio file, movie file, or image sequence.
        TL_API static std::shared_ptr<Timeline> create(
            const std::shared_ptr<ftk::Context>&,
            const ftk::Path& path,
            const ftk::Path& audioPath,
            const Options& = Options());

        //! Create a new timeline from a file name. The file name can point
        //! to an .otio file, movie file, or image sequence.
        TL_API static std::shared_ptr<Timeline> create(
            const std::shared_ptr<ftk::Context>&,
            const std::string&,
            const Options& = Options());

        //! Create a new timeline from a file name and audio file name.
        //! The file name can point to an .otio file, movie file, or
        //! image sequence.
        TL_API static std::shared_ptr<Timeline> create(
            const std::shared_ptr<ftk::Context>&,
            const std::string& fileName,
            const std::string& audioFilename,
            const Options& = Options());

        //! Get the context.
        TL_API std::shared_ptr<ftk::Context> getContext() const;

        //! Get the timeline.
        TL_API const OTIO_NS::SerializableObject::Retainer<OTIO_NS::Timeline>& getTimeline() const;

        //! Get the file path.
        TL_API const ftk::Path& getPath() const;

        //! Get the audio file path.
        TL_API const ftk::Path& getAudioPath() const;

        //! Get the timeline options.
        TL_API const Options& getOptions() const;

        //! \name Information
        ///@{

        //! Get the time range.
        TL_API const OTIO_NS::TimeRange& getTimeRange() const;

        //! Get the duration.
        TL_API OTIO_NS::RationalTime getDuration() const;

        //! Get the I/O information. This information is retrieved from
        //! the first clip in the timeline.
        TL_API const IOInfo& getIOInfo() const;

        ///@}

        //! \name Video and Audio
        ///@{

        //! Get video.
        TL_API VideoRequest getVideo(
            const OTIO_NS::RationalTime&,
            const IOOptions& = IOOptions());

        //! Get audio.
        TL_API AudioRequest getAudio(
            double seconds,
            const IOOptions& = IOOptions());

        //! Cancel requests.
        TL_API void cancelRequests(const std::vector<uint64_t>&);

        ///@}

        //! Get the number of objects currenty instantiated.
        TL_API static size_t getObjectCount();

    private:
        FTK_PRIVATE();
    };
}
