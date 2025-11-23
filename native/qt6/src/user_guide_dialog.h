#ifndef USER_GUIDE_DIALOG_H
#define USER_GUIDE_DIALOG_H

#include <QDialog>

class QTextBrowser;

class UserGuideDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserGuideDialog(QWidget *parent = nullptr);
    ~UserGuideDialog();

private:
    QTextBrowser* textBrowser;
    
    void setupUi();
    void loadUserGuide();
};

#endif // USER_GUIDE_DIALOG_H

