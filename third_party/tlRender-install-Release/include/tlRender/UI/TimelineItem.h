// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/UI/IItem.h>

#include <tlRender/Timeline/Player.h>

namespace ftk
{
    namespace gl
    {
        class Window;
    }
}

namespace tl
{
    namespace ui
    {
        //! Track types.
        enum class TL_API_TYPE TrackType
        {
            None,
            Video,
            Audio
        };

        //! Timeline item.
        //!
        //! \todo Add a selection model.
        //! \todo Add support for dragging clips to different tracks.
        //! \todo Add support for adjusting clip handles.
        //! \todo Add support for undo/redo.
        //! \todo Add an option for viewing/playing individual clips ("solo" mode).
        class TL_API_TYPE TimelineItem : public IItem
        {
        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<Player>&,
                const OTIO_NS::SerializableObject::Retainer<OTIO_NS::Stack>&,
                double scale,
                const ItemOptions&,
                const DisplayOptions&,
                const std::shared_ptr<ItemData>&,
                const std::shared_ptr<ftk::gl::Window>&,
                const std::shared_ptr<IWidget>& parent);

            TimelineItem();

        public:
            TL_API virtual ~TimelineItem();

            //! Create a new item.
            TL_API static std::shared_ptr<TimelineItem> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<Player>&,
                const OTIO_NS::SerializableObject::Retainer<OTIO_NS::Stack>&,
                double scale,
                const ItemOptions&,
                const DisplayOptions&,
                const std::shared_ptr<ItemData>&,
                const std::shared_ptr<ftk::gl::Window>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            //! Set whether playback stops when scrubbing.
            TL_API void setStopOnScrub(bool);

            //! Observe whether scrubbing is in progress.
            TL_API std::shared_ptr<ftk::IObservable<bool> > observeScrub() const;

            //! Observe time scrubbing.
            TL_API std::shared_ptr<ftk::IObservable<OTIO_NS::RationalTime> > observeTimeScrub() const;

            //! Set the frame markers.
            TL_API void setFrameMarkers(const std::vector<int>&);

            //! Get the track geometry.
            TL_API std::vector<ftk::Box2I> getTrackGeom() const;

            TL_API void setDisplayOptions(const DisplayOptions&) override;

            TL_API ftk::Size2I getSizeHint() const override;
            TL_API void setGeometry(const ftk::Box2I&) override;
            TL_API void styleEvent(const ftk::StyleEvent&) override;
            TL_API void sizeHintEvent(const ftk::SizeHintEvent&) override;
            TL_API void drawOverlayEvent(const ftk::Box2I&, const ftk::DrawEvent&) override;
            TL_API void mouseMoveEvent(ftk::MouseMoveEvent&) override;
            TL_API void mousePressEvent(ftk::MouseClickEvent&) override;
            TL_API void mouseReleaseEvent(ftk::MouseClickEvent&) override;

        protected:
            void _timeUnitsUpdate() override;

        private:
            bool _isTrackVisible(int) const;

            ftk::Size2I _getLabelMaxSize(
                const std::shared_ptr<ftk::FontSystem>&) const;
            void _getTimeTicks(
                const std::shared_ptr<ftk::FontSystem>&,
                double& seconds,
                int& tick);

            void _drawInOutPoints(
                const ftk::Box2I&,
                const ftk::DrawEvent&);
            void _drawFrameMarkers(
                const ftk::Box2I&,
                const ftk::DrawEvent&);
            void _drawCacheInfo(
                const ftk::Box2I&,
                const ftk::DrawEvent&);
            void _drawTimeLabels(
                const ftk::Box2I&,
                const ftk::DrawEvent&);
            void _drawTimeTicks(
                const ftk::Box2I&,
                const ftk::DrawEvent&);
            void _drawCurrentTime(
                const ftk::Box2I&,
                const ftk::DrawEvent&);

            void _tracksUpdate();
            void _textUpdate();

            FTK_PRIVATE();
        };
    }
}
