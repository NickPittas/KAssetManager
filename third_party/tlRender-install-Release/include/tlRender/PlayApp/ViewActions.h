// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/UI/Viewport.h>

#include <ftk/UI/Action.h>

namespace tl
{
    namespace play
    {
        class App;

        //! This class provides view actions.
        class ViewActions : public std::enable_shared_from_this<ViewActions>
        {
            FTK_NON_COPYABLE(ViewActions);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<ui::Viewport>&);

            ViewActions() = default;

        public:
            ~ViewActions();

            static std::shared_ptr<ViewActions> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<ui::Viewport>&);

            const std::map<std::string, std::shared_ptr<ftk::Action> >& getActions() const;

        private:
            std::map<std::string, std::shared_ptr<ftk::Action> > _actions;
            std::shared_ptr<ftk::Observer<std::shared_ptr<Player> > > _playerObserver;
            std::shared_ptr<ftk::Observer<bool> > _frameObserver;
        };
    }
}
