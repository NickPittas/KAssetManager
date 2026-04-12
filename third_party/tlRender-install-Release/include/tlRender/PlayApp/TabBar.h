// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/UI/TimeEdit.h>
#include <tlRender/UI/TimeLabel.h>

#include <tlRender/Timeline/Player.h>

#include <ftk/UI/TabBar.h>

namespace tl
{
    namespace play
    {
        class App;

        //! This widget provides tabs for the open documents.
        class TabBar : public ftk::IWidget
        {
            FTK_NON_COPYABLE(TabBar);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<IWidget>& parent);

            TabBar() = default;

        public:
            ~TabBar();

            static std::shared_ptr<TabBar> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            ftk::Size2I getSizeHint() const override;
            void setGeometry(const ftk::Box2I&) override;

        private:
            std::shared_ptr<ftk::TabBar> _tabBar;
            std::shared_ptr<ftk::ListObserver<std::shared_ptr<Player> > > _playersObserver;
            std::shared_ptr<ftk::Observer<int> > _playerIndexObserver;
        };
    }
}
