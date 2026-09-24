// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Timeline/Player.h>

namespace tl
{
    namespace play
    {
        class SettingsModel;

        //! This model handles opening and closing files.
        class FilesModel : public std::enable_shared_from_this<FilesModel>
        {
            FTK_NON_COPYABLE(FilesModel);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<SettingsModel>&);

            FilesModel() = default;

        public:
            ~FilesModel();

            //! Create a new model.
            static std::shared_ptr<FilesModel> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<SettingsModel>&);

            void open(const ftk::Path&);
            void open();
            void close();
            void close(int);
            void closeAll();
            void reload();
            void setCurrent(int);
            void next();
            void prev();

            std::shared_ptr<ftk::IObservableList<std::shared_ptr<Player> > > observePlayers() const;
            std::shared_ptr<ftk::IObservable<std::shared_ptr<Player> > > observePlayer() const;
            std::shared_ptr<ftk::IObservable<int> > observePlayerIndex() const;

            void setB(int);
            void setCompare(Compare);

            std::shared_ptr<ftk::IObservable<std::shared_ptr<Player> > > observeBPlayer() const;
            std::shared_ptr<ftk::IObservable<int> > observeBPlayerIndex() const;
            std::shared_ptr<ftk::IObservable<Compare> > observeCompare() const;

        private:
            std::weak_ptr<ftk::Context> _context;
            std::shared_ptr<ftk::ObservableList<std::shared_ptr<Player> > > _players;
            std::shared_ptr<ftk::Observable<std::shared_ptr<Player> > > _player;
            std::shared_ptr<ftk::Observable<int> > _playerIndex;
            std::shared_ptr<ftk::Observable<std::shared_ptr<Player> > > _bPlayer;
            std::shared_ptr<ftk::Observable<int> > _bPlayerIndex;
            std::shared_ptr<ftk::Observable<Compare> > _compare;
            PlayerCacheOptions _cacheOptions;
            std::shared_ptr<ftk::Observer<PlayerCacheOptions> > _cacheObserver;
        };
    }
}
