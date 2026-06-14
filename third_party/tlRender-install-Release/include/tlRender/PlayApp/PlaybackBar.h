// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/UI/PlaybackLoopWidget.h>
#include <tlRender/UI/TimeEdit.h>
#include <tlRender/UI/TimeLabel.h>
#include <tlRender/UI/TimeUnitsWidget.h>

#include <tlRender/Timeline/Player.h>
#include <tlRender/Timeline/TimeUnits.h>

#include <ftk/UI/Action.h>
#include <ftk/UI/ComboBox.h>
#include <ftk/UI/DoubleEdit.h>
#include <ftk/UI/Label.h>
#include <ftk/UI/RowLayout.h>

namespace tl
{
    namespace play
    {
        class App;

        //! This widget provides playback controls and other time related widgets.
        class PlaybackBar : public ftk::IWidget
        {
            FTK_NON_COPYABLE(PlaybackBar);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::map<std::string, std::shared_ptr<ftk::Action> >&,
                const std::shared_ptr<IWidget>& parent);

            PlaybackBar() = default;

        public:
            ~PlaybackBar();

            static std::shared_ptr<PlaybackBar> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::map<std::string, std::shared_ptr<ftk::Action> >&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            ftk::Size2I getSizeHint() const override;
            void setGeometry(const ftk::Box2I&) override;

        private:
            std::shared_ptr<Player> _player;
            std::shared_ptr<ftk::HorizontalLayout> _layout;
            std::shared_ptr<ui::PlaybackLoopWidget> _loopWidget;
            std::shared_ptr<ui::TimeEdit> _currentTimeEdit;
            std::shared_ptr<ui::TimeLabel> _durationLabel;
            std::shared_ptr<ftk::DoubleEdit> _speedEdit;
            std::shared_ptr<ftk::Label> _actualSpeedLabel;
            std::shared_ptr<ui::TimeUnitsWidget> _timeUnitsWidget;

            std::shared_ptr<ftk::Observer<std::shared_ptr<Player> > > _playerObserver;
            std::shared_ptr<ftk::Observer<Loop> > _loopObserver;
            std::shared_ptr<ftk::Observer<OTIO_NS::RationalTime> > _currentTimeObserver;
            std::shared_ptr<ftk::Observer<double> > _speedObserver;
            std::shared_ptr<ftk::Observer<double> > _actualSpeedObserver;
        };
    }
}
