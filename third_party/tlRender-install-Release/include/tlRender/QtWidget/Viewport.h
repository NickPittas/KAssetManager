// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/QtWidget/ContainerWidget.h>

#include <tlRender/Qt/PlayerObject.h>

#include <tlRender/Timeline/BackgroundOptions.h>
#include <tlRender/Timeline/ColorOptions.h>
#include <tlRender/Timeline/CompareOptions.h>
#include <tlRender/Timeline/DisplayOptions.h>
#include <tlRender/Timeline/ForegroundOptions.h>

#include <ftk/GL/Texture.h>

#include <QSharedPointer>
#include <QVector>

namespace tl
{
    namespace qtwidget
    {
        //! Timeline viewport widget.
        class Viewport : public ContainerWidget
        {
            Q_OBJECT

        public:
            Viewport(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<ftk::Style>&,
                QWidget* parent = nullptr);

            virtual ~Viewport();

            //! Get the color buffer type.
            ftk::gl::TextureType colorBuffer() const;

            //! Get the view position.
            const ftk::V2I& viewPos() const;

            //! Get the view zoom.
            double zoom() const;

            //! Get whether the view is framed.
            bool hasFrameView() const;

            //! Get the frames per second.
            double getFPS() const;

            //! Get the number of dropped frames during playback.
            size_t getDroppedFrames() const;

        public Q_SLOTS:
            //! Set the OpenColorIO options.
            void setOCIOOptions(const tl::OCIOOptions&);

            //! Set the LUT options.
            void setLUTOptions(const tl::LUTOptions&);

            //! Set the image options.
            void setImageOptions(const std::vector<ftk::ImageOptions>&);

            //! Set the display options.
            void setDisplayOptions(const std::vector<tl::DisplayOptions>&);

            //! Set the comparison options.
            void setCompareOptions(const tl::CompareOptions&);

            //! Set the background options.
            void setBackgroundOptions(const tl::BackgroundOptions&);

            //! Set the foreground options.
            void setForegroundOptions(const tl::ForegroundOptions&);

            //! Set the color buffer type.
            void setColorBuffer(ftk::gl::TextureType);

            //! Set the timeline player.
            void setPlayer(const QSharedPointer<qt::PlayerObject>&);

            //! Set the view position and zoom.
            void setViewPosAndZoom(const ftk::V2I&, double);

            //! Set the view zoom.
            void setZoom(double, const ftk::V2I& focus = ftk::V2I());

            //! Frame the view.
            void setFrameView(bool);

            //! Reset the view zoom to 1:1.
            void resetZoom();

            //! Zoom the view in.
            void zoomIn();

            //! Zoom the view out.
            void zoomOut();

        Q_SIGNALS:
            //! This signal is emitted when the comparison options are changed.
            void compareOptionsChanged(const tl::CompareOptions&);

            //! This signal is emitted when the position and zoom change.
            void viewPosAndZoomChanged(const ftk::V2I&, double);

            //! This signal is emitted when the frame view is changed.
            void frameViewChanged(bool);

            //! This signal is emitetd when the FPS is changed.
            void fpsChanged(double);

            //! This signal is emitted when the dropped frames count is changed.
            void droppedFramesChanged(size_t);

            //! This signal is emitted when the color picker is changed.
            void colorPickerChanged(const ftk::Color4F&);

        private:
            FTK_PRIVATE();
        };
    }
}
