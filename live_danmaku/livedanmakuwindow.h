#ifndef LIVEDANMAKUWINDOW_H
#define LIVEDANMAKUWINDOW_H

#include <QWidget>
#include <windows.h>
#include <windowsx.h>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QFontMetrics>
#include <QTimer>
#include <QListWidget>
#include <QVBoxLayout>
#include <QSettings>
#include <QMenu>
#include <QAction>
#include <QColorDialog>
#include <QLabel>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QJsonParseError>
#include <QJsonDocument>
#include <QLineEdit>
#include <QCryptographicHash>
#include <QScrollBar>
#include <QPropertyAnimation>
#include <QInputDialog>
#include "livedanmaku.h"
#include "netutil.h"
#include "freecopyedit.h"
#include "qxtglobalshortcut.h"

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

#define DANMAKU_JSON_ROLE Qt::UserRole
#define DANMAKU_STRING_ROLE Qt::UserRole+1
#define DANMAKU_IGNORE_ROLE Qt::UserRole+2
#define DANMAKU_TRANS_ROLE Qt::UserRole+3
#define DANMAKU_REPLY_ROLE Qt::UserRole+4
#define DANMAKU_HIGHLIGHT_ROLE Qt::UserRole+5

class LiveDanmakuWindow : public QWidget
{
    Q_OBJECT
public:
    LiveDanmakuWindow(QWidget *parent = nullptr);

    QString getLocalNickname(qint64 uid);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *event) override;

signals:
    void signalSendMsg(QString msg);

public slots:
    void slotNewLiveDanmaku(LiveDanmaku danmaku);
    void slotOldLiveDanmakuRemoved(LiveDanmaku danmaku);
    void setItemWidgetText(QListWidgetItem* item);
    void highlightItemText(QListWidgetItem* item, bool recover = false);
    void resetItemsTextColor();
    void resetItemsText();
    void showMenu();
    void setAutoTranslate(bool trans);
    void startTranslate(QListWidgetItem* item);
    void setAIReply(bool reply);
    void startReply(QListWidgetItem* item);
    void addIgnoredMsg(QString text);
    void setListWidgetItemSpacing(int x);

private:
    bool isItemExist(QListWidgetItem *item);
    void adjustItemTextDynamic(QListWidgetItem* item);

private:
    QSettings settings;
    QListWidget* listWidget;
    TransparentEdit* lineEdit;
    QxtGlobalShortcut* editShortcut;
#ifdef Q_OS_WIN32
    HWND prevWindow = nullptr;
#endif

    QColor nameColor;
    QColor msgColor;
    QColor bgColor;
    QColor hlColor;
    bool autoTrans = true;
    bool aiReply = false;
    QStringList ignoredMsgs;
    QList<qint64> careUsers;

    QHash<qint64, QString> localNicknames;

    int fontHeight;
    int lineSpacing;
    int boundaryWidth = 8;
    int boundaryShowed = 2;
    QPoint pressPos;
    int listItemSpacing = 6;
};

#endif // LIVEDANMAKUWINDOW_H
