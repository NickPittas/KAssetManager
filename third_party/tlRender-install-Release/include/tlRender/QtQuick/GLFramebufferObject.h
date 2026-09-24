// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Timeline/Timeline.h>

#include <QQuickFramebufferObject>

namespace tl
{
    namespace qtquick
    {
        //! OpenGL frame buffer object.
        class GLFramebufferObject : public QQuickFramebufferObject
        {
            Q_OBJECT
            Q_PROPERTY(
                std::vector<tl::VideoFrame> video
                READ video
                WRITE setVideo)

        public:
            GLFramebufferObject(QQuickItem* parent = nullptr);

            virtual ~GLFramebufferObject();

            //! Get the video.
            const std::vector<VideoFrame>& video() const;

            Renderer* createRenderer() const override;

        public Q_SLOTS:
            //! Set the video.
            void setVideo(const std::vector<tl::VideoFrame>&);

        private:
            FTK_PRIVATE();
        };
    }
}
