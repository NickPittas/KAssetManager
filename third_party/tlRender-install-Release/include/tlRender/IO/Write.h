// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/IO/Plugin.h>

namespace tl
{
    //! Base class for writers.
    class TL_API_TYPE IWrite : public IIO
    {
    protected:
        void _init(
            const ftk::Path&,
            const IOOptions&,
            const IOInfo&,
            const std::shared_ptr<ftk::LogSystem>&);

        IWrite();

    public:
        TL_API virtual ~IWrite();

        //! Write video data.
        TL_API virtual void writeVideo(
            const OTIO_NS::RationalTime&,
            const std::shared_ptr<ftk::Image>&,
            const IOOptions& = IOOptions()) = 0;

    protected:
        IOInfo _info;
    };

    //! Base class for write plugins.
    class TL_API_TYPE IWritePlugin : public IIOPlugin
    {
        FTK_NON_COPYABLE(IWritePlugin);

    protected:
        void _init(
            const std::string& name,
            const std::map<std::string, FileType>& extensions,
            const std::shared_ptr<ftk::LogSystem>&);

        IWritePlugin();

    public:
        TL_API virtual ~IWritePlugin() = 0;

        //! Get information for writing.
        TL_API virtual ftk::ImageInfo getInfo(
            const ftk::ImageInfo&,
            const IOOptions& = IOOptions()) const = 0;

        //! Create a writer for the given path.
        TL_API virtual std::shared_ptr<IWrite> write(
            const ftk::Path&,
            const IOInfo&,
            const IOOptions& = IOOptions()) = 0;

    protected:
        bool _isCompatible(const ftk::ImageInfo&, const IOOptions&) const;

    private:
        FTK_PRIVATE();
    };
}