#pragma once

#include <QIcon>

// Toolbar and file-type icon helpers shared across UI modules.
QIcon icoFolderNew();
QIcon icoCopy();
QIcon icoCut();
QIcon icoPaste();
QIcon icoDelete();
QIcon icoRename();
QIcon icoAdd();
QIcon icoGrid();
QIcon icoList();
QIcon icoGroup();
QIcon icoEye();
QIcon icoBack();
QIcon icoUp();
QIcon icoRefresh();
QIcon icoHide();
QIcon icoSearch();
QIcon icoMediaPlay();
QIcon icoMediaPause();
QIcon icoMediaStop();
QIcon icoMediaNextFrame();
QIcon icoMediaPrevFrame();
QIcon icoMediaAudio();
QIcon icoMediaNoAudio();
QIcon icoMediaMute();
QIcon icoFilePdf();
QIcon icoFileCsv();
QIcon icoFileDoc();
QIcon icoFileXls();
QIcon icoFileTxt();
QIcon icoFileAi();
QIcon icoFileGeneric();

QIcon getFileTypeIcon(const QString& ext);
