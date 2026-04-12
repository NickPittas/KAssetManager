// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Timeline/Player.h>
#include <tlRender/Timeline/TimeUnits.h>

#if defined(TLRENDER_BMD)
#include <tlRender/Device/BMDOutputDevice.h>
#endif // TLRENDER_BMD

#include <ftk/UI/App.h>
#include <ftk/UI/RecentFilesModel.h>
#include <ftk/UI/SysLogModel.h>
#include <ftk/Core/CmdLine.h>
#include <ftk/Core/Timer.h>

namespace tl
{
    namespace play
    {
        class FilesModel;
        class MainWindow;
        class SettingsModel;

        //! Application.
        class App : public ftk::App
        {
            FTK_NON_COPYABLE(App);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                std::vector<std::string>&);

            App() = default;

        public:
            ~App();

            static std::shared_ptr<App> create(
                const std::shared_ptr<ftk::Context>&,
                std::vector<std::string>&);

            const std::shared_ptr<SettingsModel>& getSettingsModel() const;
            const std::shared_ptr<ftk::SysLogModel>& getSysLogModel() const;
            const std::shared_ptr<TimeUnitsModel>& getTimeUnitsModel() const;
            const std::shared_ptr<ftk::RecentFilesModel>& getRecentFilesModel() const;
            const std::shared_ptr<FilesModel>& getFilesModel() const;

            void open(const ftk::Path&);
            void open();
            void reload();

            void run() override;

        private:
            struct CmdLine
            {
                std::shared_ptr<ftk::CmdLineListArg<std::string> > inputs;
                std::shared_ptr<ftk::CmdLineOption<int> > debugLoop;
            };
            CmdLine _cmdLine;

            std::shared_ptr<SettingsModel> _settingsModel;
            std::shared_ptr<ftk::SysLogModel> _sysLogModel;
            std::shared_ptr<TimeUnitsModel> _timeUnitsModel;
            std::shared_ptr<ftk::RecentFilesModel> _recentFilesModel;
            std::shared_ptr<FilesModel> _filesModel;

            std::shared_ptr<MainWindow> _window;

#if defined(TLRENDER_BMD)
            std::shared_ptr<bmd::OutputDevice> _bmdOutputDevice;
#endif // TLRENDER_BMD

            std::shared_ptr<ftk::Observer<std::shared_ptr<Player> > > _playerObserver;

            std::shared_ptr<ftk::Timer> _debugTimer;
            int _debugInput = 0;
        };
    }
}
