// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the tlRender project.

#pragma once

#include <tlRender/Timeline/Player.h>

#include <ftk/UI/CheckBox.h>
#include <ftk/UI/DoubleEdit.h>
#include <ftk/UI/FormLayout.h>
#include <ftk/UI/RowLayout.h>

namespace tl
{
    namespace play
    {
        class App;

        //! This widget provides the timeline player cache settings.
        class CacheSettingsWidget : public ftk::IWidget
        {
            FTK_NON_COPYABLE(CacheSettingsWidget);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<IWidget>& parent);

            CacheSettingsWidget() = default;

        public:
            ~CacheSettingsWidget();

            static std::shared_ptr<CacheSettingsWidget> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            ftk::Size2I getSizeHint() const override;
            void setGeometry(const ftk::Box2I&) override;

        private:
            std::shared_ptr<ftk::DoubleEdit> _videoEdit;
            std::shared_ptr<ftk::DoubleEdit> _audioEdit;
            std::shared_ptr<ftk::DoubleEdit> _readBehindEdit;
            std::shared_ptr<ftk::FormLayout> _layout;
            std::shared_ptr<ftk::Observer<PlayerCacheOptions> > _cacheObserver;
        };

        //! This widget provides the file browser settings.
        class FileBrowserSettingsWidget : public ftk::IWidget
        {
            FTK_NON_COPYABLE(FileBrowserSettingsWidget);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<IWidget>& parent);

            FileBrowserSettingsWidget() = default;

        public:
            ~FileBrowserSettingsWidget();

            static std::shared_ptr<FileBrowserSettingsWidget> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            ftk::Size2I getSizeHint() const override;
            void setGeometry(const ftk::Box2I&) override;

        private:
            std::shared_ptr<ftk::CheckBox> _nativeCheckBox;
            std::shared_ptr<ftk::FormLayout> _layout;
            std::shared_ptr<ftk::Observer<PlayerCacheOptions> > _cacheObserver;
        };

        //! This widget provides the settings.
        class SettingsWidget : public ftk::IWidget
        {
            FTK_NON_COPYABLE(SettingsWidget);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<IWidget>& parent);

            SettingsWidget() = default;

        public:
            ~SettingsWidget();

            static std::shared_ptr<SettingsWidget> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            ftk::Size2I getSizeHint() const override;
            void setGeometry(const ftk::Box2I&) override;

        private:
            std::shared_ptr<ftk::VerticalLayout> _layout;
        };
    }
}
