#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>
#include <QApplication>
#include <QClipboard>
#include <QNetworkCookie>
#include <QMessageBox>
#include <QInputDialog>
#include <QTextCodec>
#include <stdio.h>
#include <iostream>
#include <QtWebSockets/QWebSocket>
#include <QAuthenticator>
#include "netutil.h"
#include "livedanmaku.h"
#include "livedanmakuwindow.h"
#include "taskwidget.h"
#include "commonvalues.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

#define SOCKET_DEB if (1) qDebug()
#define SOCKET_INF if (1) qDebug()
#define SOCKET_MODE

class MainWindow : public QMainWindow, public CommonValues
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    struct Diange
    {
        QString nickname;
        qint64 uid;
        QString name;
        QDateTime time;
    };

    struct HostInfo
    {
        QString host;
        int port;
        int wss_port;
        int ws_port;
    };

    struct HeaderStruct
    {
        int totalSize;
        short headerSize;
        short ver;
        int operation;
        int seqId;
    };

    struct FanBean
    {
        qint64 mid;
        QString uname;
        int attribute; // 0：未关注,2：已关注,6：已互粉
        qint64 mtime;
    };

    enum Operation
    {
        HANDSHAKE = 0,
        HANDSHAKE_REPLY = 1,
        HEARTBEAT = 2, // 心跳包
        HEARTBEAT_REPLY = 3, // 心跳包回复（人气值）
        SEND_MSG = 4,
        SEND_MSG_REPLY = 5, // 普通包（命令）
        DISCONNECT_REPLY = 6,
        AUTH = 7, // 认证包
        AUTH_REPLY = 8, // 认证包（回复）
        RAW = 9,
        PROTO_READY = 10,
        PROTO_FINISH = 11,
        CHANGE_ROOM = 12,
        CHANGE_ROOM_REPLY = 13,
        REGISTER = 14,
        REGISTER_REPLY = 15,
        UNREGISTER = 16,
        UNREGISTER_REPLY = 17
    };

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

signals:
    void signalNewDanmaku(LiveDanmaku danmaku);
    void signalRemoveDanmaku(LiveDanmaku danmaku);

public slots:
    void pullLiveDanmaku();
    void removeTimeoutDanmaku();

private slots:
    void on_refreshDanmakuIntervalSpin_valueChanged(int arg1);

    void on_refreshDanmakuCheck_stateChanged(int arg1);

    void on_showLiveDanmakuButton_clicked();

    void on_DiangeAutoCopyCheck_stateChanged(int);

    void on_testDanmakuButton_clicked();

    void on_removeDanmakuIntervalSpin_valueChanged(int arg1);

    void on_roomIdEdit_editingFinished();

    void on_languageAutoTranslateCheck_stateChanged(int);

    void on_tabWidget_tabBarClicked(int index);

    void on_refreshDanmakuCheck_clicked();

    void on_SetBrowserCookieButton_clicked();

    void on_SetBrowserDataButton_clicked();

    void on_SetBrowserHelpButton_clicked();

    void on_SendMsgButton_clicked();

    void on_AIReplyCheck_stateChanged(int);

    void on_testDanmakuEdit_returnPressed();

    void on_SendMsgEdit_returnPressed();

    void on_taskListWidget_customContextMenuRequested(const QPoint &);

    void on_addTaskButton_clicked();

    void sendMsg(QString msg);

    void sendWelcomeMsg(QString msg);

    void sendGiftMsg(QString msg);

    void sendAttentionMsg(QString msg);

    void slotSocketError(QAbstractSocket::SocketError error);

    void slotBinaryMessageReceived(const QByteArray &message);

    void slotUncompressBytes(const QByteArray &body);

    void splitUncompressedBody(const QByteArray &unc);

    void on_autoSendWelcomeCheck_stateChanged(int arg1);

    void on_autoSendGiftCheck_stateChanged(int arg1);

    void on_autoWelcomeWordsEdit_textChanged();

    void on_autoThankWordsEdit_textChanged();

    void on_startLiveWordsEdit_editingFinished();

    void on_endLiveWordsEdit_editingFinished();

    void on_startLiveSendCheck_stateChanged(int arg1);

    void on_autoSendAttentionCheck_stateChanged(int arg1);

    void on_autoAttentionWordsEdit_textChanged();

    void on_sendWelcomeCDSpin_valueChanged(int arg1);

    void on_sendGiftCDSpin_valueChanged(int arg1);

    void on_sendAttentionCDSpin_valueChanged(int arg1);

    void on_diangeHistoryButton_clicked();

    void addBlockUser(qint64 uid, int hour);

    void delBlockUser(qint64 uid);

    void delRoomBlockUser(qint64 id);

    void on_enableBlockCheck_clicked();

    void on_newbieTipCheck_clicked();

    void on_blockKeysButton_clicked();

    void on_diangeFormatButton_clicked();

private:
    void appendNewLiveDanmakus(QList<LiveDanmaku> roomDanmakus);
    void appendNewLiveDanmaku(LiveDanmaku danmaku);
    void newLiveDanmakuAdded(LiveDanmaku danmaku);
    void oldLiveDanmakuRemoved(LiveDanmaku danmaku);
    void addNoReplyDanmakuText(QString text);

    void addTimerTask(bool enable, int second, QString text);
    void saveTaskList();
    void restoreTaskList();

    QVariant getCookies();
    void initWS();
    void startConnectRoom();
    void getRoomInit();
    void getRoomInfo();
    void getDanmuInfo();
    void getFansAndUpdate();
    void startMsgLoop();
    QByteArray makePack(QByteArray body, qint32 operation);
    void sendVeriPacket();
    void sendHeartPacket();
    void handleMessage(QJsonObject json);
    void refreshBlockList();

    QByteArray zlibUncompress(QByteArray ba) const;
    QString getLocalNickname(qint64 name) const;
    QString nicknameSimplify(QString nickname) const;

private:
    Ui::MainWindow *ui;
    QSettings settings;
    QString roomId;
    bool liveStatus = false; // 是否正在直播
    QString roomName;
    QPixmap coverPixmap;
    bool justStart = true; // 启动10秒内不进行发送，避免一些误会
    int currentFans = 0;
    int currentFansClub = 0;
    QList<FanBean> fansList; // 最近的关注，按时间排序

    QList<LiveDanmaku> roomDanmakus;
    LiveDanmakuWindow* danmakuWindow = nullptr;
#ifndef SOCKET_MODE
    QTimer* danmakuTimer;
    qint64 prevLastDanmakuTimestamp = 0;
    bool firstPullDanmaku = true; // 是否不加载以前的弹幕
#endif
    QTimer* removeTimer;
    qint64 removeDanmakuInterval = 20000;

    bool diangeAutoCopy = false;
    QList<Diange> diangeHistory;
    QString diangeFormatString;
    QString blockReString;

    QString browserCookie;
    QString browserData;
    QTimer* sendMsgTimer;

    QLabel* statusLabel;

    QString shortId;
    QString uid;
    QList<HostInfo> hostList;
    QString token;

    QWebSocket* socket;
    QTimer* heartTimer;
};
#endif // MAINWINDOW_H
