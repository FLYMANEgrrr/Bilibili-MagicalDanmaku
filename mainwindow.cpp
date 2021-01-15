#include <zlib.h>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "videolyricscreator.h"
#include "roomstatusdialog.h"

QHash<qint64, QString> CommonValues::localNicknames; // 本地昵称
QHash<qint64, qint64> CommonValues::userComeTimes;   // 用户进来的时间（客户端时间戳为准）
QHash<qint64, qint64> CommonValues::userBlockIds;    // 本次用户屏蔽的ID
QSettings* CommonValues::danmakuCounts = nullptr;    // 每个用户的统计
QList<LiveDanmaku> CommonValues::allDanmakus;        // 本次启动的所有弹幕
QList<qint64> CommonValues::careUsers;               // 特别关心
QList<qint64> CommonValues::strongNotifyUsers;       // 强提醒
QHash<QString, QString> CommonValues::pinyinMap;     // 拼音
QHash<QString, QString> CommonValues::customVariant; // 自定义变量
QList<qint64> CommonValues::notWelcomeUsers;         // 强提醒
QHash<int, QString> CommonValues::giftNames;         // 自定义礼物名字
QString CommonValues::browserCookie;
QString CommonValues::browserData;
QString CommonValues::csrf_token;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow),
      settings(QApplication::applicationDirPath()+"/settings.ini", QSettings::Format::IniFormat),
      robotRecord(QApplication::applicationDirPath()+"/robots.ini", QSettings::Format::IniFormat)
{
    ui->setupUi(this);
    connect(qApp, &QApplication::paletteChanged, this, [=](const QPalette& pa){
        ui->tabWidget->setPalette(pa);
    });
    ui->menubar->setStyleSheet("QMenuBar:item{background:transparent;}QMenuBar{background:transparent;}");

    // 页面
    int tabIndex = settings.value("mainwindow/tabIndex", 0).toInt();
    if (tabIndex >= 0 && tabIndex < ui->tabWidget->count())
        ui->tabWidget->setCurrentIndex(tabIndex);

    // 房间号
    roomId = settings.value("danmaku/roomId", "").toString();
    if (!roomId.isEmpty())
        ui->roomIdEdit->setText(roomId);

    // 刷新间隔
#ifndef SOCKET_MODE
    danmakuTimer = new QTimer(this);
    int interval = settings.value("danmaku/interval", 500).toInt();
    ui->refreshDanmakuIntervalSpin->setValue(interval);
    danmakuTimer->setInterval(interval);
    connect(danmakuTimer, SIGNAL(timeout()), this, SLOT(pullLiveDanmaku()));
    ui->refreshDanmakuCheck->setChecked(true);
#endif

    // 移除间隔
    removeTimer = new QTimer(this);
    removeTimer->setInterval(1000);
    connect(removeTimer, SIGNAL(timeout()), this, SLOT(removeTimeoutDanmaku()));
    removeTimer->start();

    int removeIv = settings.value("danmaku/removeInterval", 60).toInt();
    ui->removeDanmakuIntervalSpin->setValue(removeIv); // 自动引发改变事件
    this->removeDanmakuInterval = removeIv * 1000;

    removeIv = settings.value("danmaku/removeTipInterval", 20).toInt();
    ui->removeDanmakuTipIntervalSpin->setValue(removeIv); // 自动引发改变事件
    this->removeDanmakuTipInterval = removeIv * 1000;

    // 点歌自动复制
    diangeAutoCopy = settings.value("danmaku/diangeAutoCopy", true).toBool();
    ui->DiangeAutoCopyCheck->setChecked(diangeAutoCopy);
    ui->diangeNeedMedalCheck->setChecked(settings.value("danmaku/diangeNeedMedal", true).toBool());
    QString defaultDiangeFormat = "^点歌[ :：,，]*(.+)";
    diangeFormatString = settings.value("danmaku/diangeFormat", defaultDiangeFormat).toString();
    ui->diangeFormatEdit->setText(diangeFormatString);
    connect(this, SIGNAL(signalNewDanmaku(LiveDanmaku)), this, SLOT(slotDiange(LiveDanmaku)));
    ui->diangeReplyCheck->setChecked(settings.value("danmaku/diangeReply", false).toBool());
    ui->diangeShuaCheck->setChecked(settings.value("danmaku/diangeShua", true).toBool());

    // 自动翻译
    bool trans = settings.value("danmaku/autoTrans", true).toBool();
    ui->languageAutoTranslateCheck->setChecked(trans);

    // 自动回复
    bool reply = settings.value("danmaku/aiReply", false).toBool();
    ui->AIReplyCheck->setChecked(reply);

    // 黑名单管理
    ui->enableBlockCheck->setChecked(settings.value("block/enableBlock", false).toBool());

    // 新人提示
    ui->newbieTipCheck->setChecked(settings.value("block/newbieTip", true).toBool());

    // 自动禁言
    ui->autoBlockNewbieCheck->setChecked(settings.value("block/autoBlockNewbie", false).toBool());
    ui->autoBlockNewbieKeysEdit->setPlainText(settings.value("block/autoBlockNewbieKeys").toString());

    ui->autoBlockNewbieNotifyCheck->setChecked(settings.value("block/autoBlockNewbieNotify", false).toBool());
    ui->autoBlockNewbieNotifyWordsEdit->setPlainText(settings.value("block/autoBlockNewbieNotifyWords").toString());
    ui->autoBlockNewbieNotifyCheck->setEnabled(ui->autoBlockNewbieCheck->isChecked());

    ui->promptBlockNewbieCheck->setChecked(settings.value("block/promptBlockNewbie", false).toBool());
    ui->promptBlockNewbieKeysEdit->setPlainText(settings.value("block/promptBlockNewbieKeys").toString());

    ui->notOnlyNewbieCheck->setChecked(settings.value("block/notOnlyNewbie", false).toBool());

    // 实时弹幕
    if (settings.value("danmaku/liveWindow", false).toBool())
         on_actionShow_Live_Danmaku_triggered();

    // 点歌姬
    if (settings.value("danmaku/playerWindow", false).toBool())
        on_actionShow_Order_Player_Window_triggered();

    // 录播
    if (settings.value("danmaku/record", false).toBool())
        ui->recordCheck->setChecked(true);
    int recordSplit = settings.value("danmaku/recordSplit", 30).toInt();
    ui->recordSplitSpin->setValue(recordSplit);
    recordTimer = new QTimer(this);
    recordTimer->setInterval(recordSplit * 60000); // 默认30分钟断开一次
    connect(recordTimer, &QTimer::timeout, this, [=]{
        if (!recordLoop) // 没有正在录制
            return ;

        recordLoop->quit(); // 这个就是停止录制了
        // 停止之后，录播会检测是否还需要重新录播
        // 如果是，则继续录
    });
    ui->recordCheck->setToolTip("保存地址：" + QApplication::applicationDirPath() + "/record/房间名_时间.mp4");

    // 发送弹幕
    browserCookie = settings.value("danmaku/browserCookie", "").toString();
    browserData = settings.value("danmaku/browserData", "").toString();
    int posl = browserData.indexOf("csrf_token=") + 11;
    int posr = browserData.indexOf("&", posl);
    if (posr == -1) posr = browserData.length();
    csrf_token = browserData.mid(posl, posr - posl);
    getUserInfo();

    // 保存弹幕
    bool saveDanmuToFile = settings.value("danmaku/saveDanmakuToFile", false).toBool();
    if (saveDanmuToFile)
    {
        ui->saveDanmakuToFileCheck->setChecked(true);
        startSaveDanmakuToFile();
    }

    // 每日数据
    bool calcDaliy = settings.value("live/calculateDaliyData", true).toBool();
    ui->calculateDailyDataCheck->setChecked(calcDaliy);
    if (calcDaliy)
        startCalculateDailyData();

    // PK串门提示
    pkChuanmenEnable = settings.value("pk/chuanmen", false).toBool();
    ui->pkChuanmenCheck->setChecked(pkChuanmenEnable);

    // PK消息同步
    pkMsgSync = settings.value("pk/msgSync", false).toBool();
    ui->pkMsgSyncCheck->setChecked(pkMsgSync);

    // 判断机器人
    judgeRobot = settings.value("danmaku/judgeRobot", false).toBool();
    ui->judgeRobotCheck->setChecked(judgeRobot);

    // 本地昵称
    QStringList namePares = settings.value("danmaku/localNicknames").toString().split(";", QString::SkipEmptyParts);
    foreach (QString pare, namePares)
    {
        QStringList sl = pare.split("=>");
        if (sl.size() < 2)
            continue;

        localNicknames.insert(sl.at(0).toLongLong(), sl.at(1));
    }

    // 礼物别名
    namePares = settings.value("danmaku/giftNames").toString().split(";", QString::SkipEmptyParts);
    foreach (QString pare, namePares)
    {
        QStringList sl = pare.split("=>");
        if (sl.size() < 2)
            continue;

        giftNames.insert(sl.at(0).toInt(), sl.at(1));
    }

    // 特别关心
    QStringList usersS = settings.value("danmaku/careUsers", "20285041").toString().split(";", QString::SkipEmptyParts);
    foreach (QString s, usersS)
    {
        careUsers.append(s.toLongLong());
    }

    // 强提醒
    QStringList usersSN = settings.value("danmaku/strongNotifyUsers", "").toString().split(";", QString::SkipEmptyParts);
    foreach (QString s, usersSN)
    {
        strongNotifyUsers.append(s.toLongLong());
    }

    // 不自动欢迎
    QStringList usersNW = settings.value("danmaku/notWelcomeUsers", "").toString().split(";", QString::SkipEmptyParts);
    foreach (QString s, usersNW)
    {
        notWelcomeUsers.append(s.toLongLong());
    }

    // 仅开播发送
    ui->sendAutoOnlyLiveCheck->setChecked(settings.value("danmaku/sendAutoOnlyLive", true).toBool());
    ui->autoDoSignCheck->setChecked(settings.value("danmaku/autoDoSign", false).toBool());

    // 弹幕次数
    danmakuCounts = new QSettings(QApplication::applicationDirPath()+"/danmu_count.ini", QSettings::Format::IniFormat);

    // 状态栏
    statusLabel = new QLabel(this);
    fansLabel = new QLabel(this);
    rankLabel = new QLabel(this);
    this->statusBar()->addWidget(statusLabel, 1);
    this->statusBar()->addWidget(fansLabel, 1);
    this->statusBar()->addWidget(rankLabel, 1);
    statusLabel->setAlignment(Qt::AlignLeft);
    fansLabel->setAlignment(Qt::AlignCenter);
    rankLabel->setAlignment(Qt::AlignRight);

    // 托盘
    tray = new QSystemTrayIcon(this);//初始化托盘对象tray
    tray->setIcon(QIcon(QPixmap(":/icons/bowknot")));//设定托盘图标，引号内是自定义的png图片路径
    tray->setToolTip("神奇弹幕");
    tray->show();//让托盘图标显示在系统托盘上
    QString title="APP Message";
    QString text="神奇弹幕";
//    tray->showMessage(title,text,QSystemTrayIcon::Information,3000); //最后一个参数为提示时长，默认10000，即10s

    restoreAction = new QAction("显示", this);
    connect(restoreAction, SIGNAL(triggered()), this, SLOT(show()));
    quitAction = new QAction("退出", this);
    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));

    trayMenu = new QMenu(this);
    trayMenu->addAction(restoreAction);
    trayMenu->addSeparator();
    trayMenu->addAction(quitAction);
    tray->setContextMenu(trayMenu);

    connect(tray,SIGNAL(activated(QSystemTrayIcon::ActivationReason)),this,SLOT(showWidget(QSystemTrayIcon::ActivationReason)));

    // 大乱斗
    pkTimer = new QTimer(this);
    connect(pkTimer, &QTimer::timeout, this, [=]{
        // 更新PK信息
        int second = 0;
        int minute = 0;
        if (pkEndTime)
        {
            second = static_cast<int>(pkEndTime - QDateTime::currentSecsSinceEpoch());
            if (second < 0) // 结束后会继续等待一段时间，这时候会变成负数
                second = 0;
            minute = second / 60;
            second = second % 60;
        }
        QString text = QString("%1:%2 %3/%4")
                .arg(minute)
                .arg((second < 10 ? "0" : "") + QString::number(second))
                .arg(myVotes)
                .arg(matchVotes);
        if (danmakuWindow)
            danmakuWindow->setStatusText(text);
    });
    pkTimer->setInterval(300);

    // 大乱斗自动赠送吃瓜
    bool melon = settings.value("pk/autoMelon", false).toBool();
    ui->pkAutoMelonCheck->setChecked(melon);
    pkMaxGold = settings.value("pk/maxGold", 300).toInt();
    pkJudgeEarly = settings.value("pk/judgeEarly", 2000).toInt();
    toutaCount = settings.value("pk/toutaCount", 0).toInt();
    chiguaCount = settings.value("pk/chiguaCount", 0).toInt();
    goldTransPk = settings.value("pk/goldTransPk", goldTransPk).toInt();

    // 自定义变量
    restoreCustomVariant(settings.value("danmaku/customVariant", "").toString());

    // 定时任务
    srand((unsigned)time(0));
    restoreTaskList();

    // 自动回复
    restoreReplyList();

    // 自动发送
    ui->autoSendWelcomeCheck->setChecked(settings.value("danmaku/sendWelcome", false).toBool());
    ui->autoSendGiftCheck->setChecked(settings.value("danmaku/sendGift", false).toBool());
    ui->autoSendAttentionCheck->setChecked(settings.value("danmaku/sendAttention", false).toBool());
    ui->sendWelcomeCDSpin->setValue(settings.value("danmaku/sendWelcomeCD", 10).toInt());
    ui->sendGiftCDSpin->setValue(settings.value("danmaku/sendGiftCD", 5).toInt());
    ui->sendAttentionCDSpin->setValue(settings.value("danmaku/sendAttentionCD", 5).toInt());
    ui->autoWelcomeWordsEdit->setPlainText(settings.value("danmaku/autoWelcomeWords", ui->autoWelcomeWordsEdit->toPlainText()).toString());
    ui->autoThankWordsEdit->setPlainText(settings.value("danmaku/autoThankWords", ui->autoThankWordsEdit->toPlainText()).toString());
    ui->autoAttentionWordsEdit->setPlainText(settings.value("danmaku/autoAttentionWords", ui->autoAttentionWordsEdit->toPlainText()).toString());
    ui->sendWelcomeTextCheck->setChecked(settings.value("danmaku/sendWelcomeText", true).toBool());
    ui->sendWelcomeVoiceCheck->setChecked(settings.value("danmaku/sendWelcomeVoice", false).toBool());
    ui->sendGiftTextCheck->setChecked(settings.value("danmaku/sendGiftText", true).toBool());
    ui->sendGiftVoiceCheck->setChecked(settings.value("danmaku/sendGiftVoice", false).toBool());
    ui->sendAttentionTextCheck->setChecked(settings.value("danmaku/sendAttentionText", true).toBool());
    ui->sendAttentionVoiceCheck->setChecked(settings.value("danmaku/sendAttentionVoice", false).toBool());
    ui->sendWelcomeTextCheck->setEnabled(ui->autoSendWelcomeCheck->isChecked());
    ui->sendWelcomeVoiceCheck->setEnabled(ui->autoSendWelcomeCheck->isChecked());
    ui->sendGiftTextCheck->setEnabled(ui->autoSendGiftCheck->isChecked());
    ui->sendGiftVoiceCheck->setEnabled(ui->autoSendGiftCheck->isChecked());
    ui->sendAttentionTextCheck->setEnabled(ui->autoSendAttentionCheck->isChecked());
    ui->sendAttentionVoiceCheck->setEnabled(ui->autoSendAttentionCheck->isChecked());

    // 文字转语音
    ui->autoSpeekDanmakuCheck->setChecked(settings.value("danmaku/autoSpeek", false).toBool());
    if (ui->sendWelcomeVoiceCheck->isChecked() || ui->sendGiftVoiceCheck->isChecked()
            || ui->sendAttentionVoiceCheck->isChecked() || ui->autoSpeekDanmakuCheck)
        tts = new QTextToSpeech(this);

    // 开播
    ui->startLiveWordsEdit->setText(settings.value("live/startWords").toString());
    ui->endLiveWordsEdit->setText(settings.value("live/endWords").toString());
    ui->startLiveSendCheck->setChecked(settings.value("live/startSend").toBool());

    // 弹幕人气
    danmuPopularTimer = new QTimer(this);
    danmuPopularTimer->setInterval(30000);
    connect(danmuPopularTimer, &QTimer::timeout, this, [=]{
        danmuPopularValue += minuteDanmuPopular;
        danmuPopularQueue.append(minuteDanmuPopular);
        minuteDanmuPopular = 0;
        if (danmuPopularQueue.size() > 10)
            danmuPopularValue -= danmuPopularQueue.takeFirst();
        ui->popularityLabel->setToolTip("弹幕人气：" + snum(danmuPopularValue));
    });

    // 定时连接
    ui->timerConnectServerCheck->setChecked(settings.value("live/timerConnectServer", false).toBool());
    ui->startLiveHourSpin->setValue(settings.value("live/startLiveHour", 0).toInt());
    ui->endLiveHourSpin->setValue(settings.value("live/endLiveHour", 0).toInt());
    connectServerTimer = new QTimer(this);
    connectServerTimer->setInterval(CONNECT_SERVER_INTERVAL);
    connect(connectServerTimer, &QTimer::timeout, this, [=]{
        connectServerTimer->setInterval(180000); // 比如服务器主动断开，则会短期内重新定时，还原自动连接定时
        if (liveStatus && (socket->state() == QAbstractSocket::ConnectedState || socket->state() == QAbstractSocket::ConnectingState))
        {
            connectServerTimer->stop();
            return ;
        }
        startConnectRoom();
    });

    // WS连接
    initWS();
    startConnectRoom();

//    for (int i = 0; i < 3; i++)
    /*{
        QFile file("receive.txt");
        file.open(QIODevice::ReadWrite);
        auto ba = file.readAll();
        ba = ba.right(ba.size()-16);

        unsigned long si;
        BYTE* target = new BYTE[ba.size()*5+100]{};
        uncompress(target, &si, (unsigned char*)ba.data(), ba.size());
        QByteArray unc = QByteArray::fromRawData((char*)target, si);
        qDebug() << "解压后的：" << unc;
        qDebug() << "直接解压：" << zlibUncompress(ba);
    }*/

    // 10秒内不进行自动化操作
    QTimer::singleShot(3000, [=]{
        justStart = false;
    });

    // 读取拼音1
    QtConcurrent::run([=]{
        QFile pinyinFile(":/document/pinyin");
        pinyinFile.open(QIODevice::ReadOnly);
        QTextStream pinyinIn(&pinyinFile);
        pinyinIn.setCodec("UTF-8");
        QString line = pinyinIn.readLine();
        while (!line.isNull())
        {
            if (!line.isEmpty())
            {
                QString han = line.at(0);
                QString pinyin = line.right(line.length()-1);
                pinyinMap.insert(han, pinyin);
            }
            line = pinyinIn.readLine();
        }
    });

    // 隐藏偷塔
    if (!QFileInfo(QApplication::applicationDirPath() + "/touta").exists())
    {
        ui->pkAutoMelonCheck->setText("此项禁止使用");
        ui->pkAutoMelonCheck->hide();
        ui->pkMaxGoldButton->hide();
        ui->pkMelonValButton->hide();
        ui->pkJudgeEarlyButton->hide();
    }

    // 粉丝勋章
    ui->autoSwitchMedalCheck->setChecked(settings.value("danmaku/autoSwitchMedal", false).toBool());

    // 读取自定义快捷房间
    QStringList list = settings.value("custom/rooms", "").toString().split(";", QString::SkipEmptyParts);
    ui->menu_3->addSeparator();
    for (int i = 0; i < list.size(); i++)
    {
        QStringList texts = list.at(i).split(",", QString::SkipEmptyParts);
        if (texts.size() < 1)
            continue ;
        QString id = texts.first();
        QString name = texts.size() >= 2 ? texts.at(1) : id;
        QAction* action = new QAction(name, this);
        ui->menu_3->addAction(action);
        connect(action, &QAction::triggered, this, [=]{
            ui->roomIdEdit->setText(id);
            on_roomIdEdit_editingFinished();
        });
    }

    // 滚屏
    ui->enableScreenDanmakuCheck->setChecked(settings.value("screendanmaku/enableDanmaku", false).toBool());
    ui->enableScreenMsgCheck->setChecked(settings.value("screendanmaku/enableMsg", false).toBool());
    ui->screenDanmakuLeftSpin->setValue(settings.value("screendanmaku/left", 0).toInt());
    ui->screenDanmakuRightSpin->setValue(settings.value("screendanmaku/right", 0).toInt());
    ui->screenDanmakuTopSpin->setValue(settings.value("screendanmaku/top", 10).toInt());
    ui->screenDanmakuBottomSpin->setValue(settings.value("screendanmaku/bottom", 60).toInt());
    ui->screenDanmakuSpeedSpin->setValue(settings.value("screendanmaku/speed", 10).toInt());
    ui->enableScreenMsgCheck->setEnabled(ui->enableScreenDanmakuCheck->isChecked());
    QString danmakuFontString = settings.value("screendanmaku/font").toString();
    if (!danmakuFontString.isEmpty())
        screenDanmakuFont.fromString(danmakuFontString);
    screenDanmakuColor = qvariant_cast<QColor>(settings.value("screendanmaku/color", QColor(0, 0, 0)));
    connect(this, &MainWindow::signalNewDanmaku, this, [=](LiveDanmaku danmaku){
//        QtConcurrent::run([&]{
            showScreenDanmaku(danmaku);
//        });
    });

    connect(this, &MainWindow::signalNewDanmaku, this, [=](LiveDanmaku danmaku){
        if (ui->autoSpeekDanmakuCheck->isChecked() && danmaku.getMsgType() == MSG_DANMAKU)
            speekText(danmaku.getText());
    });

    // 自动签到
    doSignTimer = new QTimer(this);
    doSignTimer->setInterval(24 * 60 * 60 * 1000);
    connect(doSignTimer, &QTimer::timeout, this, [=]{
        doSign();
    });
    if (settings.value("danmaku/autoDoSign", false).toBool())
    {
        ui->autoDoSignCheck->setChecked(true);
        doSignTimer->start();
    }

    // 自动参与天选
    ui->autoLOTCheck->setChecked(settings.value("danmaku/autoLOT", false).toBool());

//    qDebug() << nicknameSimplify("修改昵称需要6个币"); // 昵称调试
}

MainWindow::~MainWindow()
{
    if (danmuLogFile)
    {
        finishSaveDanmuToFile();
    }
    if (ui->calculateDailyDataCheck->isChecked())
    {
        saveCalculateDailyData();
    }

    if (danmakuWindow)
    {
        danmakuWindow->close();
        danmakuWindow->deleteLater();
        danmakuWindow = nullptr;
    }

    /*if (playerWindow)
    {
        settings.setValue("danmaku/playerWindow", !playerWindow->isHidden());
        playerWindow->close();
        playerWindow->deleteLater();
    }*/

    delete ui;
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    restoreGeometry(settings.value("mainwindow/geometry").toByteArray());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    settings.setValue("mainwindow/geometry", this->saveGeometry());

    event->ignore();
    this->hide();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    // 自动调整封面大小
    if (!roomCover.isNull())
    {
        QPixmap pixmap = roomCover;
        int w = ui->roomCoverLabel->width();
        if (w > ui->tabWidget->contentsRect().width())
            w = ui->tabWidget->contentsRect().width();
//        int h = ui->tabWidget->contentsRect().height() - ui->roomCoverLabel->geometry().top();
        pixmap = pixmap.scaledToWidth(w, Qt::SmoothTransformation);
        ui->roomCoverLabel->setPixmap(pixmap);
        ui->roomCoverLabel->setMinimumSize(1, 1);
    }

    // 自动调整任务列表大小
    for (int row = 0; row < ui->taskListWidget->count(); row++)
    {
        auto item = ui->taskListWidget->item(row);
        auto widget = ui->taskListWidget->itemWidget(item);
        if (!widget)
            continue;
        QSize size(ui->taskListWidget->contentsRect().width() - ui->taskListWidget->verticalScrollBar()->width(), widget->height());
        auto taskWidget = static_cast<TaskWidget*>(widget);
//        taskWidget->resize(size.width(), size.height());
//        taskWidget->setFixedWidth(size.width());
        taskWidget->resize(size);
        taskWidget->autoResizeEdit();
//        taskWidget->adjustSize(); // autoResizeEdit 的时候会自动resize
//        item->setSizeHint(taskWidget->size()); // taskWidget内部信号会触发item->setSizeHint
    }

    for (int row = 0; row < ui->replyListWidget->count(); row++)
    {
        auto item = ui->replyListWidget->item(row);
        auto widget = ui->replyListWidget->itemWidget(item);
        if (!widget)
            continue;
        QSize size(ui->replyListWidget->contentsRect().width() - ui->replyListWidget->verticalScrollBar()->width(), widget->height());
        auto replyWidget = static_cast<ReplyWidget*>(widget);
//        replyWidget->resize(size.width(), size.height());
//        replyWidget->setFixedWidth(size.width());
        replyWidget->resize(size);
        replyWidget->autoResizeEdit();
//        replyWidget->adjustSize();
//        item->setSizeHint(replyWidget->size()); // replyWidget内部信号会触发item->setSizeHint
    }
}

void MainWindow::changeEvent(QEvent *event)
{

    QMainWindow::changeEvent(event);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    QPainter painter(this);
}

void MainWindow::pullLiveDanmaku()
{
    if (roomId.isEmpty())
        return ;
    QString url = "https://api.live.bilibili.com/ajax/msg";
    QStringList param{"roomid", roomId};
    connect(new NetUtil(url, param), &NetUtil::finished, this, [=](QString result){
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "pullLiveDanmaku.ERROR:" << error.errorString();
            qDebug() << result;
            return ;
        }
        QJsonObject json = document.object();
        QJsonArray danmakus = json.value("data").toObject().value("room").toArray();
        QList<LiveDanmaku> lds;
        for (int i = 0; i < danmakus.size(); i++)
            lds.append(LiveDanmaku::fromDanmakuJson(danmakus.at(i).toObject()));
        appendNewLiveDanmakus(lds);
    });
}

void MainWindow::removeTimeoutDanmaku()
{
    // 移除过期队列
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    if (roomDanmakus.size()) // 每次最多移除一个
    {
        QDateTime dateTime = roomDanmakus.first().getTimeline();
        if (dateTime.toMSecsSinceEpoch() + removeDanmakuInterval < timestamp)
        {
            auto danmaku = roomDanmakus.takeFirst();
            oldLiveDanmakuRemoved(danmaku);
        }
    }

    // 移除多余的提示（一般时间更短）
    for (int i = 0; i < roomDanmakus.size(); i++)
    {
        auto danmaku = roomDanmakus.at(i);
        auto type = danmaku.getMsgType();
        if (type == MSG_ATTENTION || type == MSG_WELCOME || type == MSG_FANS
                || (type == MSG_GIFT && (!danmaku.isGoldCoin() || danmaku.getTotalCoin() < 1000))
                || (type == MSG_DANMAKU && danmaku.isNoReply())
                || type == MSG_MSG
                || danmaku.isToView() || danmaku.isPkLink())
        {
            QDateTime dateTime = danmaku.getTimeline();
            if (dateTime.toMSecsSinceEpoch() + removeDanmakuTipInterval < timestamp)
            {
                roomDanmakus.removeAt(i--);
                oldLiveDanmakuRemoved(danmaku);
            }
            // break; // 不break，就是一次性删除多个
        }
    }
}

void MainWindow::appendNewLiveDanmakus(QList<LiveDanmaku> danmakus)
{
#ifndef SOCKET_MODE
    // 去掉已经存在的弹幕
    while (danmakus.size() && danmakus.first().getTimeline().toMSecsSinceEpoch() <= prevLastDanmakuTimestamp)
        danmakus.removeFirst();
    if (!danmakus.size())
        return ;
    prevLastDanmakuTimestamp = danmakus.last().getTimeline().toMSecsSinceEpoch();

    // 不是第一次加载
    if (!firstPullDanmaku || danmakus.size() == 1) // 用作测试就不需要该条件；1条很可能是测试弹幕
    {
        // 发送信号给其他插件
        for (int i = 0; i < danmakus.size(); i++)
        {
            newLiveDanmakuAdded(danmakus.at(i));
        }
    }
    else
        firstPullDanmaku = false;
#endif

    // 添加到队列
    roomDanmakus.append(danmakus);
    allDanmakus.append(danmakus);
}

void MainWindow::appendNewLiveDanmaku(LiveDanmaku danmaku)
{
    roomDanmakus.append(danmaku);
    allDanmakus.append(danmaku);
    newLiveDanmakuAdded(danmaku);
}

void MainWindow::newLiveDanmakuAdded(LiveDanmaku danmaku)
{
    SOCKET_DEB << "+++++新弹幕：" << danmaku.toString();
    emit signalNewDanmaku(danmaku);

    // 保存到文件
    if (danmuLogStream)
    {
        (*danmuLogStream) << danmaku.toString() << "\n";
        (*danmuLogStream).flush(); // 立刻刷新到文件里
    }
}

void MainWindow::oldLiveDanmakuRemoved(LiveDanmaku danmaku)
{
    SOCKET_DEB << "-----旧弹幕：" << danmaku.toString();
    emit signalRemoveDanmaku(danmaku);
}

void MainWindow::addNoReplyDanmakuText(QString text)
{
    noReplyMsgs.append(text);
}

void MainWindow::showLocalNotify(QString text)
{
    appendNewLiveDanmaku(LiveDanmaku(text));
}

void MainWindow::showLocalNotify(QString text, qint64 uid)
{
    LiveDanmaku danmaku(text);
    danmaku.setUid(uid);
    appendNewLiveDanmaku(danmaku);
}

/**
 * 发送单条弹幕的原子操作
 */
void MainWindow::sendMsg(QString msg)
{
    sendRoomMsg(roomId, msg);
}

void MainWindow::sendRoomMsg(QString roomId, QString msg)
{
    if (browserCookie.isEmpty() || browserData.isEmpty())
    {
        statusLabel->setText("未设置Cookie信息");
        return ;
    }
    if (msg.isEmpty() || roomId.isEmpty())
        return ;

    QUrl url("https://api.live.bilibili.com/msg/send");

    // 建立对象
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");

    // 设置数据（JSON的ByteArray）
    QString s = browserData;
    int posl = s.indexOf("msg=")+4;
    int posr = s.indexOf("&", posl);
    if (posr == -1)
        posr = s.length();
    s.replace(posl, posr-posl, msg);

    posl = s.indexOf("roomid=")+7;
    posr = s.indexOf("&", posl);
    if (posr == -1)
        posr = s.length();
    s.replace(posl, posr-posl, roomId);

    QByteArray ba(s.toStdString().data());
//    QByteArray ba("color=16777215&fontsize=25&mode=1&msg=thist&rnd=1604144057&roomid=11584296&bubble=0&csrf_token=13ddba7f6f0ad582fecef801d40b3abf&csrf=13ddba7f6f0ad582fecef801d40b3abf");
//    qDebug() << ba;

    // 连接槽
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

//        qDebug() << data;
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "发送弹幕：" << error.errorString();
            return ;
        }
        QJsonObject object = document.object();
        QString errorMsg = object.value("message").toString();
        statusLabel->setText("");
        if (!errorMsg.isEmpty())
        {
            statusLabel->setText(errorMsg);
            qDebug() << s8("warning: 发送失败：") << errorMsg << msg;
            showLocalNotify(errorMsg + " -> " + msg);
        }
    });

    manager->post(*request, ba);
}

/**
 * 发送多条消息
 * 使用“\n”进行多行换行
 */
void MainWindow::sendAutoMsg(QString msgs)
{
    msgs = processTimeVariants(msgs);
//    qDebug() << "@@@@@@@@@@->准备发送弹幕：" << msgs;
    QStringList sl = msgs.split("\\n", QString::SkipEmptyParts);
    const int cd = 1500;
    int delay = 0;
    if (sl.size())
    {
        for (int i = 0; i < sl.size(); i++)
        {
            QTimer::singleShot(delay, [=]{
                QString msg = sl.at(i);
                addNoReplyDanmakuText(msg);
                sendMsg(msg);
            });
            delay += cd;
        }
    }
}

void MainWindow::sendCdMsg(QString msg, int cd, int channel, bool enableText, bool enableVoice)
{
    if (ui->sendAutoOnlyLiveCheck->isChecked() && !liveStatus) // 不在直播中
        return ;
    if (msg.trimmed().isEmpty())
        return ;

    analyzeMsgAndCd(msg, cd, channel);

    // 避免太频繁发消息
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    if (timestamp - msgCds[channel] < cd)
        return ;
    msgCds[channel] = timestamp;

    if (enableText)
    {
        sendAutoMsg(msgToShort(msg));
    }
    if (enableVoice)
        speekVariantText(msg);
}

void MainWindow::sendWelcomeGuard(QString msg)
{
    if (ui->sendAutoOnlyLiveCheck->isChecked() && !liveStatus) // 不在直播中
        return ;
    if (msg.trimmed().isEmpty())
        return ;

    // 避免太频繁发消息
    static qint64 prevTimestamp = 0;
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    int cd = ui->sendWelcomeCDSpin->value() * 1000 / 10; // 十分之一的冷却，并且独立发送
    if (timestamp - prevTimestamp < cd)
        return ;
    prevTimestamp = timestamp;

    if (ui->sendWelcomeTextCheck->isChecked())
    {
        msg = msgToShort(msg);
        sendAutoMsg(msg);
    }
    if (ui->sendWelcomeVoiceCheck->isChecked())
        speekVariantText(msg);
}

void MainWindow::sendWelcomeMsg(QString msg)
{
    sendCdMsg(msg, ui->sendWelcomeCDSpin->value() * 1000, WELCOME_CD_CN,
              ui->sendWelcomeTextCheck->isChecked(), ui->sendWelcomeVoiceCheck->isChecked());
}

void MainWindow::sendOppositeMsg(QString msg)
{
    if (ui->sendAutoOnlyLiveCheck->isChecked() && !liveStatus) // 不在直播中
        return ;
    if (msg.trimmed().isEmpty())
        return ;

    // 避免太频繁发消息
    static qint64 prevTimestamp = 0;
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    int cd = ui->sendWelcomeCDSpin->value() * 1000 / 4; // 四分之一的欢迎冷却时间
    cd = qMax(2000, cd);
    if (timestamp - prevTimestamp < cd)
        return ;
    prevTimestamp = timestamp;

    if (ui->sendWelcomeTextCheck->isChecked())
    {
        msg = msgToShort(msg);
        sendAutoMsg(msg);
    }
    if (ui->sendWelcomeVoiceCheck->isChecked())
        speekVariantText(msg);
}

void MainWindow::sendGiftMsg(QString msg)
{
    if (ui->sendAutoOnlyLiveCheck->isChecked() && !liveStatus) // 不在直播中
        return ;
    if (msg.trimmed().isEmpty())
        return ;

    // 避免太频繁发消息
    static qint64 prevTimestamp = 0;
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    int cd = ui->sendGiftCDSpin->value() * 1000;
    if (timestamp - prevTimestamp < cd)
        return ;
    prevTimestamp = timestamp;

    if (ui->sendGiftTextCheck->isChecked())
    {
        msg = msgToShort(msg);
        sendAutoMsg(msg);
    }
    if (ui->sendGiftVoiceCheck->isChecked())
        speekVariantText(msg);
}

void MainWindow::sendAttentionMsg(QString msg)
{
    if (ui->sendAutoOnlyLiveCheck->isChecked() && !liveStatus) // 不在直播中
        return ;
    if (msg.trimmed().isEmpty())
        return ;

    // 避免太频繁发消息
    static qint64 prevTimestamp = 0;
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    int cd = ui->sendAttentionCDSpin->value() * 1000;
    if (timestamp - prevTimestamp < cd)
        return ;
    prevTimestamp = timestamp;

    if (ui->sendAttentionTextCheck->isChecked())
    {
        msg = msgToShort(msg);
        sendAutoMsg(msg);
    }
    if (ui->sendAttentionVoiceCheck->isChecked())
        speekVariantText(msg);
}

void MainWindow::sendNotifyMsg(QString msg)
{
    if (ui->sendAutoOnlyLiveCheck->isChecked() && !liveStatus)
        return ;
    if (msg.trimmed().isEmpty())
        return ;

    // 避免太频繁发消息
    static qint64 prevTimestamp = 0;
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    int cd = 1000; // 最快1秒
    if (timestamp - prevTimestamp < cd)
        return ;
    prevTimestamp = timestamp;

    msg = msgToShort(msg);
    sendAutoMsg(msg);
}

void MainWindow::on_DiangeAutoCopyCheck_stateChanged(int)
{
    settings.setValue("danmaku/diangeAutoCopy", diangeAutoCopy = ui->DiangeAutoCopyCheck->isChecked());
}

void MainWindow::on_testDanmakuButton_clicked()
{
    QString text = ui->testDanmakuEdit->text();
    if (text.isEmpty())
        text = "测试弹幕";
    int r = qrand() % 7 + 1;
    appendNewLiveDanmaku(LiveDanmaku("测试用户" + QString::number(r), text,
                            10000+r, 12,
                             QDateTime::currentDateTime(), "", ""));

    if (text == "赠送吃瓜")
    {
        sendGift(20004, 1);
    }
    else if (text == "测试送礼")
    {
        QString username = "测试用户";
        QString giftName = "测试礼物";
        int giftId = 123;
        int num = qrand() % 3 + 1;
        qint64 uid = 123;
        qint64 timestamp = QDateTime::currentSecsSinceEpoch();
        QString coinType = qrand()%2 ? "gold" : "silver";
        int totalCoin = qrand() % 20 * 100;
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);
        appendNewLiveDanmaku(danmaku);

        QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
        qDebug() << "条件替换结果：" << words;
    }
    else if (text == "测试消息")
    {
        showLocalNotify("测试通知消息");
    }
    else
    {
        ui->testDanmakuEdit->setText("");
        ui->testDanmakuEdit->setFocus();
    }
}

void MainWindow::on_removeDanmakuIntervalSpin_valueChanged(int arg1)
{
    this->removeDanmakuInterval = arg1 * 1000;
    settings.setValue("danmaku/removeInterval", arg1);
}

void MainWindow::on_roomIdEdit_editingFinished()
{
    if (roomId == ui->roomIdEdit->text() || shortId == ui->roomIdEdit->text())
        return ;

    // 关闭旧的
    if (socket)
    {
        if (socket->state() != QAbstractSocket::UnconnectedState)
            socket->abort();
        liveStatus = false;
    }
    roomId = ui->roomIdEdit->text();
    settings.setValue("danmaku/roomId", roomId);

    releaseLiveData();

    emit signalRoomChanged(roomId);

    // 开启新的
#ifndef SOCKET_MODE
    firstPullDanmaku = true;
    prevLastDanmakuTimestamp = 0;
#else
    if (socket)
    {
        startConnectRoom();
    }
#endif
}

void MainWindow::on_languageAutoTranslateCheck_stateChanged(int)
{
    auto trans = ui->languageAutoTranslateCheck->isChecked();
    settings.setValue("danmaku/autoTrans", trans);
    if (danmakuWindow)
        danmakuWindow->setAutoTranslate(trans);
}

void MainWindow::on_tabWidget_tabBarClicked(int index)
{
    settings.setValue("mainwindow/tabIndex", index);
}

void MainWindow::on_SendMsgButton_clicked()
{
    QString msg = ui->SendMsgEdit->text();
    msg = processDanmakuVariants(msg, LiveDanmaku());
    sendAutoMsg(msg);
}

void MainWindow::on_AIReplyCheck_stateChanged(int)
{
    bool reply = ui->AIReplyCheck->isChecked();
    settings.setValue("danmaku/aiReply", reply);
    if (danmakuWindow)
        danmakuWindow->setAIReply(reply);
}

void MainWindow::on_testDanmakuEdit_returnPressed()
{
    on_testDanmakuButton_clicked();
}

void MainWindow::on_SendMsgEdit_returnPressed()
{
    QString msg = ui->SendMsgEdit->text();
    msg = processDanmakuVariants(msg, LiveDanmaku());
    sendAutoMsg(msg);
    ui->SendMsgEdit->clear();
}

void MainWindow::addTimerTask(bool enable, int second, QString text)
{
    TaskWidget* tw = new TaskWidget(this);
    QListWidgetItem* item = new QListWidgetItem(ui->taskListWidget);

    ui->taskListWidget->addItem(item);
    ui->taskListWidget->setItemWidget(item, tw);
    ui->taskListWidget->setCurrentRow(ui->taskListWidget->count()-1);
    ui->taskListWidget->scrollToBottom();

    // 连接信号
    connect(tw->check, &QCheckBox::stateChanged, this, [=](int){
        bool enable = tw->check->isChecked();
        int row = ui->taskListWidget->row(item);
        settings.setValue("task/r"+QString::number(row)+"Enable", enable);
    });

    connect(tw, &TaskWidget::spinChanged, this, [=](int val){
        int row = ui->taskListWidget->row(item);
        settings.setValue("task/r"+QString::number(row)+"Interval", val);
    });

    connect(tw->edit, &QPlainTextEdit::textChanged, this, [=]{
        item->setSizeHint(tw->sizeHint());

        QString content = tw->edit->toPlainText();
        int row = ui->taskListWidget->row(item);
        settings.setValue("task/r"+QString::number(row)+"Msg", content);
    });

    connect(tw, &TaskWidget::signalSendMsgs, this, [=](QString sl, bool manual){
        if (!manual && ui->sendAutoOnlyLiveCheck->isChecked() && !liveStatus) // 没有开播，不进行定时任务
            return ;
        QStringList msgs = getEditConditionStringList(sl, LiveDanmaku());
        if (msgs.size())
        {
            int r = qrand() % msgs.size();
            QString s = msgs.at(r);
            if (!s.trimmed().isEmpty())
            {
                sendAutoMsg(s);
            }
        }
    });

    connect(tw, &TaskWidget::signalResized, tw, [=]{
        item->setSizeHint(tw->size());
    });

    remoteControl = settings.value("danmaku/remoteControl", remoteControl).toBool();

    // 设置属性
    tw->check->setChecked(enable);
    tw->spin->setValue(second);
    tw->slotSpinChanged(second);
    tw->edit->setPlainText(text);
    tw->autoResizeEdit();
    tw->adjustSize();
//    item->setSizeHint(tw->sizeHint());
}

void MainWindow::saveTaskList()
{
    settings.setValue("task/count", ui->taskListWidget->count());
    for (int row = 0; row < ui->taskListWidget->count(); row++)
    {
        auto widget = ui->taskListWidget->itemWidget(ui->taskListWidget->item(row));
        auto tw = static_cast<TaskWidget*>(widget);
        settings.setValue("task/r"+QString::number(row)+"Enable", tw->check->isChecked());
        settings.setValue("task/r"+QString::number(row)+"Interval", tw->spin->value());
        settings.setValue("task/r"+QString::number(row)+"Msg", tw->edit->toPlainText());
    }
}

void MainWindow::restoreTaskList()
{
    int count = settings.value("task/count", 0).toInt();
    for (int row = 0; row < count; row++)
    {
        bool enable = settings.value("task/r"+QString::number(row)+"Enable", true).toBool();
        int interval = settings.value("task/r"+QString::number(row)+"Interval", 1800).toInt();
        QString msg = settings.value("task/r"+QString::number(row)+"Msg", "").toString();
        addTimerTask(enable, interval, msg);
    }
}

void MainWindow::addAutoReply(bool enable, QString key, QString reply)
{
    ReplyWidget* rw = new ReplyWidget(this);
    QListWidgetItem* item = new QListWidgetItem(ui->replyListWidget);

    ui->replyListWidget->addItem(item);
    ui->replyListWidget->setItemWidget(item, rw);
    ui->replyListWidget->setCurrentRow(ui->replyListWidget->count()-1);
    ui->replyListWidget->scrollToBottom();

    // 连接信号
    connect(rw->check, &QCheckBox::stateChanged, this, [=](int){
        bool enable = rw->check->isChecked();
        int row = ui->replyListWidget->row(item);
        settings.setValue("reply/r"+QString::number(row)+"Enable", enable);
    });

    connect(rw->keyEdit, &QLineEdit::textEdited, this, [=]{
        QString content = rw->keyEdit->text();
        int row = ui->replyListWidget->row(item);
        settings.setValue("reply/r"+QString::number(row)+"Key", content);
    });

    connect(rw->replyEdit, &QPlainTextEdit::textChanged, this, [=]{
        item->setSizeHint(rw->sizeHint());

        QString content = rw->replyEdit->toPlainText();
        int row = ui->replyListWidget->row(item);
        settings.setValue("reply/r"+QString::number(row)+"Reply", content);
    });

    connect(this, SIGNAL(signalNewDanmaku(LiveDanmaku)), rw, SLOT(slotNewDanmaku(LiveDanmaku)));

    connect(rw, &ReplyWidget::signalReplyMsgs, this, [=](QString sl, LiveDanmaku danmaku, bool manual){
        if (!manual && ui->sendAutoOnlyLiveCheck->isChecked() && !liveStatus) // 没有开播，不进行自动回复
            return ;
        QStringList msgs = getEditConditionStringList(sl, danmaku);
        if (msgs.size())
        {
            int r = qrand() % msgs.size();
            QString s = msgs.at(r);
            if (!s.trimmed().isEmpty())
            {
                static qint64 prevTimestamp = 0;
                qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
                int cd = 1500;
                if (timestamp - prevTimestamp < cd)
                    return ;
                prevTimestamp = timestamp;

                if (QString::number(danmaku.getUid()) == this->cookieUid) // 自己发的，自己回复，必须要延迟一会儿
                    s = "\\n" + s; // 延迟一次发送的时间
                sendAutoMsg(s);
            }
        }
    });

    connect(rw, &ReplyWidget::signalResized, rw, [=]{
        item->setSizeHint(rw->size());
    });

    remoteControl = settings.value("danmaku/remoteControl", remoteControl).toBool();

    // 设置属性
    rw->check->setChecked(enable);
    rw->keyEdit->setText(key);
    rw->replyEdit->setPlainText(reply);
    rw->autoResizeEdit();
    rw->adjustSize();
    item->setSizeHint(rw->sizeHint());
}

void MainWindow::saveReplyList()
{
    settings.setValue("reply/count", ui->replyListWidget->count());
    for (int row = 0; row < ui->replyListWidget->count(); row++)
    {
        auto widget = ui->replyListWidget->itemWidget(ui->replyListWidget->item(row));
        auto tw = static_cast<ReplyWidget*>(widget);
        settings.setValue("reply/r"+QString::number(row)+"Enable", tw->check->isChecked());
        settings.setValue("reply/r"+QString::number(row)+"Key", tw->keyEdit->text());
        settings.setValue("reply/r"+QString::number(row)+"Reply", tw->replyEdit->toPlainText());
    }
}

void MainWindow::restoreReplyList()
{
    int count = settings.value("reply/count", 0).toInt();
    for (int row = 0; row < count; row++)
    {
        bool enable = settings.value("reply/r"+QString::number(row)+"Enable", true).toBool();
        QString key = settings.value("reply/r"+QString::number(row)+"Key").toString();
        QString reply = settings.value("reply/r"+QString::number(row)+"Reply").toString();
        addAutoReply(enable, key, reply);
    }
}

QVariant MainWindow::getCookies()
{
    QList<QNetworkCookie> cookies;

    // 设置cookie
    QString cookieText = browserCookie;
    QStringList sl = cookieText.split(";");
    foreach (auto s, sl)
    {
        s = s.trimmed();
        int pos = s.indexOf("=");
        QString key = s.left(pos);
        QString val = s.right(s.length() - pos - 1);
        cookies.push_back(QNetworkCookie(key.toUtf8(), val.toUtf8()));
    }

    // 请求头里面加入cookies
    QVariant var;
    var.setValue(cookies);
    return var;
}

/**
 * 获取用户信息
 */
void MainWindow::getUserInfo()
{
    if (browserCookie.isEmpty())
        return ;
    QString url = "http://api.bilibili.com/x/member/web/account";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        SOCKET_INF << QString(data);
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取用户信息出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取用户信息
        QJsonObject dataObj = json.value("data").toObject();
        cookieUid = snum(static_cast<qint64>(dataObj.value("mid").toDouble()));
        cookieUname = dataObj.value("uname").toString();
        qDebug() << "当前cookie用户：" << cookieUid << cookieUname;
    });
    manager->get(*request);
}

/**
 * 获取用户在当前直播间的信息
 * 会触发进入直播间，就不能偷看了……
 */
void MainWindow::getRoomUserInfo()
{
    if (browserCookie.isEmpty())
        return ;
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getInfoByUser?room_id=" + roomId;
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        SOCKET_INF << QString(data);
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取房间用户信息出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取用户在这个房间信息
        QJsonObject info = json.value("data").toObject().value("info").toObject();
        cookieUid = snum(static_cast<qint64>(info.value("uid").toDouble()));
        cookieUname = info.value("uname").toString();
        QString uface = info.value("uface").toString(); // 头像地址
        qDebug() << "当前cookie用户：" << cookieUid << cookieUname;
    });
    manager->get(*request);
}

void MainWindow::on_taskListWidget_customContextMenuRequested(const QPoint &)
{
    QListWidgetItem* item = ui->taskListWidget->currentItem();

    QMenu* menu = new QMenu(this);
    QAction* actionDelete = new QAction("删除", this);

    if (!item)
    {
        actionDelete->setEnabled(false);
    }

    menu->addAction(actionDelete);

    connect(actionDelete, &QAction::triggered, this, [=]{
        auto widget = ui->taskListWidget->itemWidget(item);
        auto tw = static_cast<TaskWidget*>(widget);

        ui->taskListWidget->removeItemWidget(item);
        ui->taskListWidget->takeItem(ui->taskListWidget->currentRow());

        saveTaskList();

        tw->deleteLater();
    });

    menu->exec(QCursor::pos());

    actionDelete->deleteLater();
}

void MainWindow::on_replyListWidget_customContextMenuRequested(const QPoint &)
{
    QListWidgetItem* item = ui->replyListWidget->currentItem();

    QMenu* menu = new QMenu(this);
    QAction* actionDelete = new QAction("删除", this);

    if (!item)
    {
        actionDelete->setEnabled(false);
    }

    menu->addAction(actionDelete);

    connect(actionDelete, &QAction::triggered, this, [=]{
        auto widget = ui->replyListWidget->itemWidget(item);
        auto rw = static_cast<ReplyWidget*>(widget);

        ui->replyListWidget->removeItemWidget(item);
        ui->replyListWidget->takeItem(ui->replyListWidget->currentRow());

        saveReplyList();

        rw->deleteLater();
    });

    menu->exec(QCursor::pos());

    actionDelete->deleteLater();
}

void MainWindow::on_addTaskButton_clicked()
{
    addTimerTask(true, 1800, "");
    saveTaskList();
    auto widget = ui->taskListWidget->itemWidget(ui->taskListWidget->item(ui->taskListWidget->count()-1));
    auto tw = static_cast<TaskWidget*>(widget);
    tw->edit->setFocus();
}

void MainWindow::on_addReplyButton_clicked()
{
    addAutoReply(true, "", "");
    saveReplyList();
    auto widget = ui->taskListWidget->itemWidget(ui->taskListWidget->item(ui->taskListWidget->count()-1));
    auto rw = static_cast<ReplyWidget*>(widget);
    rw->keyEdit->setFocus();
}

void MainWindow::slotDiange(LiveDanmaku danmaku)
{
    if (danmaku.getMsgType() != MSG_DANMAKU || danmaku.isPkLink())
        return ;
    QRegularExpression re(diangeFormatString);
    QRegularExpressionMatch match;
    if (danmaku.getText().indexOf(re, 0, &match) == -1)
        return ;
    if (match.capturedTexts().size() < 2)
    {
        statusLabel->setText("无法获取点歌内容，请检测点歌格式");
        return ;
    }
    if (ui->diangeNeedMedalCheck->isChecked())
    {
        if (danmaku.getAnchorRoomid() != roomId) // 不是对应的粉丝牌
        {
            qDebug() << "点歌未带粉丝勋章：" << danmaku.getNickname() << danmaku.getAnchorRoomid() << "!=" << roomId;
            return ;
        }
    }

    // 记录到历史（先不复制）
    QString text = match.capturedTexts().at(1);
    text = text.trimmed();
    qDebug() << s8("检测到点歌：") << text;
    diangeHistory.append(Diange{danmaku.getNickname(), danmaku.getUid(), text, danmaku.getTimeline()});
    ui->diangeHistoryListWidget->insertItem(0, text + " - " + danmaku.getNickname());

    if (!diangeAutoCopy) // 是否进行复制操作
        return ;

    if (playerWindow && !playerWindow->isHidden()) // 自动播放
    {
        if (ui->diangeShuaCheck->isChecked() && playerWindow->hasSongInOrder(danmaku.getNickname())) // 已经点了
        {
            showLocalNotify("已阻止频繁点歌");
        }
        else
        {
            playerWindow->slotSearchAndAutoAppend(text, danmaku.getNickname());
        }
    }
    else
    {
        QClipboard* clip = QApplication::clipboard();
        clip->setText(text);

        addNoReplyDanmakuText(danmaku.getText()); // 点歌不限制长度
        QTimer::singleShot(10, [=]{
            appendNewLiveDanmaku(LiveDanmaku(danmaku.getNickname(), danmaku.getUid(), text, danmaku.getTimeline()));
        });
    }
//    ui->DiangeAutoCopyCheck->setText("点歌（" + text + "）");
}

void MainWindow::slotSocketError(QAbstractSocket::SocketError error)
{
    qDebug() << "error" << socket->errorString();
    statusLabel->setText(socket->errorString());
}

void MainWindow::initWS()
{
    socket = new QWebSocket();

    connect(socket, &QWebSocket::connected, this, [=]{
        SOCKET_DEB << "socket connected";
        ui->connectStateLabel->setText("状态：已连接");

        // 5秒内发送认证包
        sendVeriPacket(socket, roomId, token);

        // 定时发送心跳包
        heartTimer->start();
        danmuPopularTimer->start();
    });

    connect(socket, &QWebSocket::disconnected, this, [=]{
        // 正在直播的时候突然断开了
        if (liveStatus)
        {
            qDebug() << "正在直播的时候突然断开，10秒后重连...";
            liveStatus = false;
            // 尝试10秒钟后重连
            connectServerTimer->setInterval(10000);
        }

        SOCKET_DEB << "disconnected";
        ui->connectStateLabel->setText("状态：未连接");
        ui->popularityLabel->setText("人气值：0");

        heartTimer->stop();
        danmuPopularTimer->stop();

        // 如果不是主动连接的话，这个会断开
        if (!connectServerTimer->isActive())
            connectServerTimer->start();
    });

    connect(socket, &QWebSocket::binaryMessageReceived, this, [=](const QByteArray &message){
//        qDebug() << "binaryMessageReceived" << message;
//        for (int i = 0; i < 100; i++) // 测试内存泄漏
        try {
            slotBinaryMessageReceived(message);
        } catch (...) {
            qDebug() << "!!!!!!!error:slotBinaryMessageReceived";
        }

    });

    connect(socket, &QWebSocket::textFrameReceived, this, [=](const QString &frame, bool isLastFrame){
        qDebug() << "textFrameReceived";
    });

    connect(socket, &QWebSocket::textMessageReceived, this, [=](const QString &message){
        qDebug() << "textMessageReceived";
    });

    connect(socket, &QWebSocket::stateChanged, this, [=](QAbstractSocket::SocketState state){
        SOCKET_DEB << "stateChanged" << state;
        QString str = "未知";
        if (state == QAbstractSocket::UnconnectedState)
            str = "未连接";
        else if (state == QAbstractSocket::ConnectingState)
            str = "连接中";
        else if (state == QAbstractSocket::ConnectedState)
            str = "已连接";
        else if (state == QAbstractSocket::BoundState)
            str = "已绑定";
        else if (state == QAbstractSocket::ClosingState)
            str = "断开中";
        else if (state == QAbstractSocket::ListeningState)
            str = "监听中";
        ui->connectStateLabel->setText(str);
    });

    heartTimer = new QTimer(this);
    heartTimer->setInterval(30000);
    connect(heartTimer, &QTimer::timeout, this, [=]{
        if (socket->state() == QAbstractSocket::ConnectedState)
            sendHeartPacket();
        else if (socket->state() == QAbstractSocket::UnconnectedState)
            startConnectRoom();

        if (pkSocket && pkSocket->state() == QAbstractSocket::ConnectedState)
        {
            QByteArray ba;
            ba.append("[object Object]");
            ba = makePack(ba, HEARTBEAT);
            pkSocket->sendBinaryMessage(ba);
        }

        for (int i = 0; i < robots_sockets.size(); i++)
        {
            QWebSocket* socket = robots_sockets.at(i);
            if (socket->state() == QAbstractSocket::ConnectedState)
            {
                QByteArray ba;
                ba.append("[object Object]");
                ba = makePack(ba, HEARTBEAT);
                socket->sendBinaryMessage(ba);
            }
        }
    });
}

void MainWindow::startConnectRoom()
{
    if (roomId.isEmpty())
        return ;

    // 初始化主播数据
    currentFans = 0;
    currentFansClub = 0;

    // 准备房间数据
    if (danmakuCounts)
        danmakuCounts->deleteLater();
    QDir dir;
    dir.mkdir(QApplication::applicationDirPath()+"/danmaku_counts");
    danmakuCounts = new QSettings(QApplication::applicationDirPath()+"/danmaku_counts/" + roomId + ".ini", QSettings::Format::IniFormat);
    if (ui->calculateDailyDataCheck->isChecked())
        startCalculateDailyData();

    // 保存房间弹幕
    if (ui->saveDanmakuToFileCheck)
        startSaveDanmakuToFile();

    // 开始获取房间信息
    getRoomInfo(true);
    if (ui->enableBlockCheck->isChecked())
        refreshBlockList();
}

/**
 * 获取房间信息
 * （已废弃）
 */
void MainWindow::getRoomInit()
{
    QString roomInitUrl = "https://api.live.bilibili.com/room/v1/Room/room_init?id=" + roomId;
    connect(new NetUtil(roomInitUrl), &NetUtil::finished, this, [=](QString result){
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取房间初始化出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << "返回结果不为0：" << json.value("message").toString();
            return ;
        }

        int realRoom = json.value("data").toObject().value("room_id").toInt();
        qDebug() << "真实房间号：" << realRoom;
    });
}

void MainWindow::getRoomInfo(bool reconnect)
{
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getInfoByRoom?room_id=" + roomId;
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        SOCKET_INF << QString(data);
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取房间信息出错：" << error.errorString();
            ui->connectStateLabel->setText("无法获取房间信息");
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取房间信息
        QJsonObject dataObj = json.value("data").toObject();
        QJsonObject roomInfo = dataObj.value("room_info").toObject();
        QJsonObject anchorInfo = dataObj.value("anchor_info").toObject();
        roomId = QString::number(roomInfo.value("room_id").toInt()); // 应当一样
        ui->roomIdEdit->setText(roomId);
        shortId = QString::number(roomInfo.value("short_id").toInt());
        upUid = QString::number(static_cast<qint64>(roomInfo.value("uid").toDouble()));
        liveStatus = roomInfo.value("live_status").toInt();
        if (danmakuWindow)
            danmakuWindow->setUpUid(upUid.toLongLong());
        qDebug() << "房间信息: roomid=" << roomId
                 << "  shortid=" << shortId
                 << "  uid=" << upUid;

        roomTitle = roomInfo.value("title").toString();
        setWindowTitle(roomTitle + " - " + QApplication::applicationName());
        tray->setToolTip(roomTitle + " - " + QApplication::applicationName());
        ui->roomNameLabel->setText(roomTitle);
        upName = anchorInfo.value("base_info").toObject().value("uname").toString();
        if (liveStatus != 1)
            ui->popularityLabel->setText("未开播");
        else
            ui->popularityLabel->setText("已开播");

        // 判断房间，未开播则暂停连接，等待开播
        if (!isLivingOrMayliving())
            return ;

        // 获取主播信息
        currentFans = anchorInfo.value("relation_info").toObject().value("attention").toInt();
        currentFansClub = anchorInfo.value("medal_info").toObject().value("fansclub").toInt();
//        qDebug() << s8("粉丝数：") << currentFans << s8("    粉丝团：") << currentFansClub;
        fansLabel->setText("粉丝:" + snum(currentFans));
        // getFansAndUpdate();

        // 异步获取房间封面
        getRoomCover(roomInfo.value("cover").toString());

        // 获取主播头像
        getUpFace(upUid);

        // 开始工作
        if (liveStatus)
            slotStartWork();

        if (!reconnect)
            return ;

        // 获取弹幕信息
        getDanmuInfo();

        // 录播
        if (ui->recordCheck->isChecked() && liveStatus)
            startLiveRecord();
    });
    manager->get(*request);
    if (reconnect)
        ui->connectStateLabel->setText("获取房间信息...");
}

bool MainWindow::isLivingOrMayliving()
{
    if (ui->timerConnectServerCheck->isChecked())
    {
        if (!liveStatus) // 未开播，等待下一次的检测
        {
            // 如果是开播前一段时间，则继续保持着连接
            int start = ui->startLiveHourSpin->value();
            int end = ui->endLiveHourSpin->value();
            int hour = QDateTime::currentDateTime().time().hour();
            int minu = QDateTime::currentDateTime().time().minute();
            bool abort = false;
            qint64 currentVal = hour * 3600000 + minu * 60000;
            qint64 nextVal = (currentVal + CONNECT_SERVER_INTERVAL) % (24 * 3600000); // 0点
            if (start < end) // 白天档
            {
                qDebug() << "白天档" << currentVal << start * 3600000 << end * 3600000;
                if (nextVal >= start * 3600000
                        && currentVal <= end * 3600000)
                {
                    if (ui->doveCheck->isChecked()) // 今天鸽了
                    {
                        abort = true;
                        qDebug() << "今天鸽了";
                    }
                    // 即将开播
                }
                else // 直播时间之外
                {
                    qDebug() << "未开播，继续等待";
                    abort = true;
                    if (currentVal > end * 3600000 && ui->doveCheck->isChecked()) // 今天结束鸽鸽
                        ui->doveCheck->setChecked(false);
                }
            }
            else if (start > end) // 熬夜档
            {
                qDebug() << "晚上档" << currentVal << start * 3600000 << end * 3600000;
                if (currentVal + CONNECT_SERVER_INTERVAL >= start * 3600000
                        || currentVal <= end * 3600000)
                {
                    if (ui->doveCheck->isChecked()) // 今晚鸽了
                        abort = true;
                    // 即将开播
                }
                else // 直播时间之外
                {
                    qDebug() << "未开播，继续等待";
                    abort = true;
                    if (currentVal > end * 3600000 && currentVal < start * 3600000
                            && ui->doveCheck->isChecked())
                        ui->doveCheck->setChecked(false);
                }
            }

            if (abort)
            {
                qDebug() << "短期内不会开播，暂且断开连接";
                if (!connectServerTimer->isActive())
                    connectServerTimer->start();
                ui->connectStateLabel->setText("等待连接");

                // 如果正在连接或打算连接，却未开播，则断开
                if (socket->state() != QAbstractSocket::UnconnectedState)
                    socket->close();
                return false;
            }
        }
        else // 已开播，则停下
        {
            qDebug() << "开播，停止定时连接";
            if (connectServerTimer->isActive())
                connectServerTimer->stop();
            if (ui->doveCheck->isChecked())
                ui->doveCheck->setChecked(false);
            return true;
        }
    }
    return true;
}

void MainWindow::getRoomCover(QString url)
{
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply1){
        QByteArray jpegData = reply1->readAll();
        QPixmap pixmap;
        pixmap.loadFromData(jpegData);
        roomCover = pixmap; // 原图
        if (roomCover.isNull())
            return ;

        int w = ui->roomCoverLabel->width();
        if (w > ui->tabWidget->contentsRect().width())
            w = ui->tabWidget->contentsRect().width();
        pixmap = pixmap.scaledToWidth(w, Qt::SmoothTransformation);
        ui->roomCoverLabel->setPixmap(pixmap);
        ui->roomCoverLabel->setMinimumSize(1, 1);

        // 设置程序主题
        QColor bg, fg, sbg, sfg;
        auto colors = ImageUtil::extractImageThemeColors(roomCover.toImage(), 7);
        ImageUtil::getBgFgSgColor(colors, &bg, &fg, &sbg, &sfg);
        prevPa = BFSColor::fromPalette(palette());
        currentPa = BFSColor(QList<QColor>{bg, fg, sbg, sfg});
        QPropertyAnimation* ani = new QPropertyAnimation(this, "paletteProg");
        ani->setStartValue(0);
        ani->setEndValue(1.0);
        ani->setDuration(500);
        connect(ani, &QPropertyAnimation::valueChanged, this, [=](const QVariant& val){
            double d = val.toDouble();
            BFSColor bfs = prevPa + (currentPa - prevPa) * d;
            QColor bg, fg, sbg, sfg;
            bfs.toColors(&bg, &fg, &sbg, &sfg);

            QPalette pa;
            pa.setColor(QPalette::Window, bg);
            pa.setColor(QPalette::Background, bg);
            pa.setColor(QPalette::Button, bg);
            pa.setColor(QPalette::Base, bg);

            pa.setColor(QPalette::Foreground, fg);
            pa.setColor(QPalette::Text, fg);
            pa.setColor(QPalette::ButtonText, fg);
            pa.setColor(QPalette::WindowText, fg);

            pa.setColor(QPalette::Highlight, sbg);
            pa.setColor(QPalette::HighlightedText, sfg);
            setPalette(pa);
            setStyleSheet("QMainWindow{background:"+QVariant(bg).toString()+"} QLabel QCheckBox{background: transparent; color:"+QVariant(fg).toString()+"}");
            ui->menubar->setStyleSheet("QMenuBar:item{background:transparent;}QMenuBar{background:transparent; color:"+QVariant(fg).toString()+"}");
        });
        connect(ani, SIGNAL(finished()), ani, SLOT(deleteLater()));
        ani->start();

        // 设置主要界面主题
        ui->tabWidget->setBg(roomCover);
    });

    manager->get(*request);
}

void MainWindow::getUpFace(QString uid)
{
    QString url = "http://api.bilibili.com/x/space/acc/info?mid=" + uid;
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray dataBa = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(dataBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取主播头像出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonObject data = json.value("data").toObject();
        QString face = data.value("face").toString();

        // 开始下载头像
        getUpPortrait(face);
    });
    manager->get(*request);
}

void MainWindow::getUpPortrait(QString face)
{
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(face);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray jpegData = reply->readAll();
        manager->deleteLater();
        QPixmap pixmap;
        pixmap.loadFromData(jpegData);

        // 设置成圆角
        int side = qMin(pixmap.width(), pixmap.height());
        upFace = QPixmap(side, side);
        upFace.fill(Qt::transparent);
        QPainter painter(&upFace);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        QPainterPath path;
        path.addEllipse(0, 0, side, side);
        painter.setClipPath(path);
        painter.drawPixmap(0, 0, side, side, pixmap);

        // 设置到程序
        setWindowIcon(upFace);
        tray->setIcon(upFace);
    });
    manager->get(*request);
}

void MainWindow::getDanmuInfo()
{
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getDanmuInfo?id="+roomId+"&type=0";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray dataBa = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(dataBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取弹幕信息出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonObject data = json.value("data").toObject();
        token = data.value("token").toString();
        QJsonArray hostArray = data.value("host_list").toArray();
        hostList.clear();
        foreach (auto val, hostArray)
        {
            QJsonObject o = val.toObject();
            hostList.append(HostInfo{
                                o.value("host").toString(),
                                o.value("port").toInt(),
                                o.value("wss_port").toInt(),
                                o.value("ws_port").toInt(),
                            });
        }
        SOCKET_DEB << s8("getDanmuInfo: host数量=") << hostList.size() << "  token=" << token;

        startMsgLoop();
    });
    manager->get(*request);
    ui->connectStateLabel->setText("获取弹幕信息...");
}

void MainWindow::getFansAndUpdate()
{
    QString url = "http://api.bilibili.com/x/relation/followers?vmid=" + upUid;
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");

    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取粉丝出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        QJsonArray list = json.value("data").toObject().value("list").toArray();
        QList<FanBean> newFans;
        foreach (QJsonValue val, list)
        {
            QJsonObject fan = val.toObject();
            qint64 mid = static_cast<qint64>(fan.value("mid").toDouble());
            int attribute = fan.value("attribute").toInt(); // 三位数：1-1-0 被关注-已关注-未知
            if (attribute == 0) // 0是什么意思啊，不懂诶……应该是关注吧？
                attribute = 2;
            qint64 mtime = static_cast<qint64>(fan.value("mtime").toDouble()); // 这是秒吧？
            QString uname = fan.value("uname").toString();

            newFans.append(FanBean{mid, uname, attribute & 2, mtime});
//qDebug() << "    粉丝信息：" << mid << uname << attribute << QDateTime::fromSecsSinceEpoch(mtime);
        }

//qDebug() << "    现有粉丝数(第一页)：" << newFans.size();

        // 第一次加载
        if (!fansList.size())
        {
            fansList = newFans;
            return ;
        }

        // 进行比较 新增关注 的 取消关注
        // 先找到原先最后关注并且还在的，即：旧列.index == 新列.find
        int index = -1; // 旧列索引
        int find = -1;  // 新列索引
        while (++index < fansList.size())
        {
            FanBean fan = fansList.at(index);
            for (int j = 0; j < newFans.size(); j++)
            {
                FanBean nFan = newFans.at(j);
                if (fan.mid == nFan.mid)
                {
                    find = j;
                    break;
                }
            }
            if (find >= 0)
                break;
        }
//qDebug() << ">>>>>>>>>>" << "index:" << index << "           find:" << find;

        // 取消关注（只支持最新关注的，不是专门做的，只是顺带）
        if (!fansList.size())
        {}
        else if (index >= fansList.size()) // 没有被关注过，或之前关注的全部取关了？
        {
            qDebug() << s8("没有被关注过，或之前关注的全部取关了？");
        }
        else // index==0没有人取关，index>0则有人取关
        {
            for (int i = 0; i < index; i++)
            {
                FanBean fan = fansList.at(i);
                qDebug() << s8("取消关注：") << fan.uname << QDateTime::fromSecsSinceEpoch(fan.mtime);
                appendNewLiveDanmaku(LiveDanmaku(fan.uname, fan.mid, false, QDateTime::fromSecsSinceEpoch(fan.mtime)));
            }
            while (index--)
                fansList.removeFirst();
        }

        // 新增关注
        for (int i = find-1; i >= 0; i--)
        {
            FanBean fan = newFans.at(i);
            qDebug() << s8("新增关注：") << fan.uname << QDateTime::fromSecsSinceEpoch(fan.mtime);
            LiveDanmaku danmaku(fan.uname, fan.mid, true, QDateTime::fromSecsSinceEpoch(fan.mtime));
            appendNewLiveDanmaku(danmaku);

            if (i == 0) // 只发送第一个（其他几位，对不起了……）
            {
                if (!justStart && ui->autoSendAttentionCheck->isChecked())
                {
                    sendAttentionThankIfNotRobot(danmaku);
                }
                else
                    judgeRobotAndMark(danmaku);
            }
            else
                judgeRobotAndMark(danmaku);

            fansList.insert(0, fan);
        }

    });
    manager->get(*request);
}

void MainWindow::startMsgLoop()
{
    int hostRetry = 0; // 循环测试连接（意思一下，暂时未使用，否则应当设置为成员变量）
    HostInfo hostServer = hostList.at(hostRetry);
    QString host = QString("wss://%1:%2/sub").arg(hostServer.host).arg(hostServer.wss_port);
    SOCKET_DEB << "hostServer:" << host;

    // 设置安全套接字连接模式（不知道有啥用）
    QSslConfiguration config = socket->sslConfiguration();
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    config.setProtocol(QSsl::TlsV1SslV3);
    socket->setSslConfiguration(config);

    socket->open(host);
}

/**
 * 给body加上头部信息
偏移量	长度	类型	含义
0	4	uint32	封包总大小（头部大小+正文大小）
4	2	uint16	头部大小（一般为0x0010，16字节）
6	2	uint16	协议版本:0普通包正文不使用压缩，1心跳及认证包正文不使用压缩，2普通包正文使用zlib压缩
8	4	uint32	操作码（封包类型）
12	4	uint32	sequence，可以取常数1
 */
QByteArray MainWindow::makePack(QByteArray body, qint32 operation)
{
    // 因为是大端，所以要一个个复制
    qint32 totalSize = 16 + body.size();
    short headerSize = 16;
    short protover = 1;
    qint32 seqId = 1;

    auto byte4 = [=](qint32 i) -> QByteArray{
        QByteArray ba(4, 0);
        ba[3] = (uchar)(0x000000ff & i);
        ba[2] = (uchar)((0x0000ff00 & i) >> 8);
        ba[1] = (uchar)((0x00ff0000 & i) >> 16);
        ba[0] = (uchar)((0xff000000 & i) >> 24);
        return ba;
    };

    auto byte2 = [=](short i) -> QByteArray{
        QByteArray ba(2, 0);
        ba[1] = (uchar)(0x00ff & i);
        ba[0] = (uchar)((0xff00 & i) >> 8);
        return ba;
    };

    QByteArray header;
    header += byte4(totalSize);
    header += byte2(headerSize);
    header += byte2(protover);
    header += byte4(operation);
    header += byte4(seqId);

    return header + body;


    /* // 小端算法，直接上结构体
    int totalSize = 16 + body.size();
    short headerSize = 16;
    short protover = 1;
    int seqId = 1;

    HeaderStruct header{totalSize, headerSize, protover, operation, seqId};

    QByteArray ba((char*)&header, sizeof(header));
    return ba + body;*/
}

void MainWindow::sendVeriPacket(QWebSocket* socket, QString roomId, QString token)
{
    QByteArray ba;
    ba.append("{\"uid\": 0, \"roomid\": "+roomId+", \"protover\": 2, \"platform\": \"web\", \"clientver\": \"1.14.3\", \"type\": 2, \"key\": \""+token+"\"}");
    ba = makePack(ba, AUTH);
    SOCKET_DEB << "发送认证包：" << ba;
    socket->sendBinaryMessage(ba);
}

/**
 * 每隔半分钟发送一次心跳包
 */
void MainWindow::sendHeartPacket()
{
    QByteArray ba;
    ba.append("[object Object]");
    ba = makePack(ba, HEARTBEAT);
//    SOCKET_DEB << "发送心跳包：" << ba;
    socket->sendBinaryMessage(ba);
}

QString MainWindow::getLocalNickname(qint64 uid) const
{
    if (localNicknames.contains(uid))
        return localNicknames.value(uid);
    return "";
}

void MainWindow::analyzeMsgAndCd(QString& msg, int &cd, int &channel) const
{
    QRegularExpression re("\\(cd(\\d{1,2}):(\\d+)\\)");
    QRegularExpressionMatch match;
    if (msg.indexOf(re, 0, &match) == -1)
        return ;
    QStringList caps = match.capturedTexts();
    QString full = caps.at(0);
    QString chann = caps.at(1);
    QString val = caps.at(2);
    channel = chann.toInt();
    cd = val.toInt();
    msg = msg.right(msg.length() - full.length());
}

/**
 * 支持变量名
 */
QString MainWindow::processTimeVariants(QString msg) const
{
    // 早上/下午/晚上 - 好呀
    if (msg.contains("%hour%"))
    {
        int hour = QTime::currentTime().hour();
        QString rst;
        if (hour <= 3)
            rst = "晚上";
        else if (hour <= 5)
            rst = "凌晨";
        else if (hour < 11)
            rst = "早上";
        else if (hour <= 13)
            rst = "中午";
        else if (hour <= 17)
            rst = "下午";
        else if (hour <= 24)
            rst = "晚上";
        else
            rst = "今天";
        msg = msg.replace("%hour%", rst);
    }

    // 早上好、早、晚上好-
    if (msg.contains("%greet%"))
    {
        int hour = QTime::currentTime().hour();
        QStringList rsts;
        rsts << "您好" << "你好";
        if (hour <= 3)
            rsts << "晚上好";
        else if (hour <= 5)
            rsts << "凌晨好";
        else if (hour < 11)
            rsts << "早上好" << "早" << "早安";
        else if (hour <= 13)
            rsts << "中午好";
        else if (hour <= 16)
            rsts << "下午好";
        else if (hour <= 24)
            rsts << "晚上好";
        int r = qrand() % rsts.size();
        msg = msg.replace("%greet%", rsts.at(r));
    }

    // 全部打招呼
    if (msg.contains("%all_greet%"))
    {
        int hour = QTime::currentTime().hour();
        QStringList sl{"啊", "呀"};
        int r = qrand() % sl.size();
        QString tone = sl.at(r);
        QStringList rsts;
        rsts << "您好"+tone << "你好"+tone;
        if (hour <= 3)
            rsts << "晚上好"+tone;
        else if (hour <= 5)
            rsts << "凌晨好"+tone;
        else if (hour < 11)
            rsts << "早上好"+tone << "早"+tone;
        else if (hour <= 13)
            rsts << "中午好"+tone;
        else if (hour <= 16)
            rsts << "下午好"+tone;
        else if (hour <= 24)
            rsts << "晚上好"+tone;

        if (hour >= 6 && hour <= 8)
            rsts << "早饭吃了吗";
        else if (hour >= 11 && hour <= 12)
            rsts << "午饭吃了吗";
        else if (hour >= 17 && hour <= 18)
            rsts << "晚饭吃了吗";

        if ((hour >= 23 && hour <= 24)
                || (hour >= 0 && hour <= 3))
            rsts << "还没睡"+tone << "怎么还没睡~";
        else if (hour >= 3 && hour <= 5)
            rsts << "通宵了吗？";

        r = qrand() % rsts.size();
        msg = msg.replace("%all_greet%", rsts.at(r));
    }

    // 语气词
    if (msg.contains("%tone%"))
    {
        QStringList sl{"啊", "呀"};
        int r = qrand() % sl.size();
        msg = msg.replace("%tone%", sl.at(r));
    }
    if (msg.contains("%lela%"))
    {
        QStringList sl{"了", "啦"};
        int r = qrand() % sl.size();
        msg = msg.replace("%lela%", sl.at(r));
    }

    // 标点
    if (msg.contains("%punc%"))
    {
        QStringList sl{"~", "！"};
        int r = qrand() % sl.size();
        msg = msg.replace("%punc%", sl.at(r));
    }

    // 语气词或标点
    if (msg.contains("%tone/punc%"))
    {
        QStringList sl{"啊", "呀", "~", "！"};
        int r = qrand() % sl.size();
        msg = msg.replace("%tone/punc%", sl.at(r));
    }

    return msg;
}

QStringList MainWindow::getEditConditionStringList(QString plainText, LiveDanmaku user) const
{
    plainText = processDanmakuVariants(plainText, user);
    QStringList lines = plainText.split("\n", QString::SkipEmptyParts);
    QStringList result;
    // 替换变量，寻找条件
    for (int i = 0; i < lines.size(); i++)
    {
        QString line = lines.at(i);
        line = processVariantConditions(line);
//        qDebug() << "骚操作后：" << line << "    原始：" << lines.at(i);
        if (!line.isEmpty())
            result.append(line.trimmed());
    }

    // 查看是否优先级
    auto hasPriority = [=](QStringList sl){
        for (int i = 0; i < sl.size(); i++)
            if (sl.at(i).startsWith("*"))
                return true;
        return false;
    };

    // 去除优先级
    while (hasPriority(result))
    {
        for (int i = 0; i < result.size(); i++)
        {
            if (!result.at(i).startsWith("*"))
                result.removeAt(i--);
            else
                result[i] = result.at(i).right(result.at(i).length()-1);
        }
    }
//    qDebug() << "condition result:" << result;
    return result;
}

/**
 * 处理用户信息中蕴含的表达式
 * 用户信息、弹幕、礼物等等
 */
QString MainWindow::processDanmakuVariants(QString msg, LiveDanmaku danmaku) const
{
    // 自定义变量
    for (auto it = customVariant.begin(); it != customVariant.end(); ++it)
    {
        msg.replace(it.key(), it.value());
    }

    // 用户昵称
    if (msg.contains("%uname%"))
        msg.replace("%uname%", danmaku.getNickname());
    if (msg.contains("%username%"))
        msg.replace("%username%", danmaku.getNickname());
    if (msg.contains("%nickname%"))
        msg.replace("%nickname%", danmaku.getNickname());

    // 用户昵称
    if (msg.contains("%uid%"))
        msg.replace("%uid%", snum(danmaku.getUid()));

    // 本地昵称+简化
    if (msg.contains("%ai_name%"))
    {
        QString local = getLocalNickname(danmaku.getUid());
        if (local.isEmpty())
            local = nicknameSimplify(danmaku.getNickname());
        if (local.isEmpty())
            local = danmaku.getNickname();
        msg.replace("%ai_name%", local);
    }

    // 专属昵称
    if (msg.contains("%local_name%"))
    {
        QString local = getLocalNickname(danmaku.getUid());
        if (local.isEmpty())
            local = danmaku.getNickname();
        msg.replace("%local_name%", local);
    }

    // 昵称简化
    if (msg.contains("%simple_name%"))
    {
        msg.replace("%simple_name%", nicknameSimplify(danmaku.getNickname()));
    }

    // 用户等级
    if (msg.contains("%level%"))
        msg.replace("%level%", snum(danmaku.getLevel()));

    if (msg.contains("%text%"))
        msg.replace("%text%", danmaku.getText());

    // 进来次数
    if (msg.contains("%come_count%"))
    {
        if (danmaku.getMsgType() == MSG_WELCOME)
            msg.replace("%come_count%", snum(danmaku.getNumber()));
        else
            msg.replace("%come_count%", snum(danmakuCounts->value("come/"+snum(danmaku.getUid())).toInt()));
    }
/*qDebug() << ">>>>>>>>>进入时间：" << QDateTime::currentSecsSinceEpoch() <<
            QDateTime::currentSecsSinceEpoch() - (danmaku.getMsgType() == MSG_WELCOME
             ? danmaku.getPrevTimestamp()
             : danmakuCounts->value("comeTime/"+snum(danmaku.getUid())).toLongLong());*/
    // 上次进来
    if (msg.contains("%come_time%"))
    {
        msg.replace("%come_time%", snum(danmaku.getMsgType() == MSG_WELCOME
                                        ? danmaku.getPrevTimestamp()
                                        : danmakuCounts->value("comeTime/"+snum(danmaku.getUid())).toLongLong()));
    }

    // 本次送礼金瓜子
    if (msg.contains("%gift_gold%"))
        msg.replace("%gift_gold%", snum(danmaku.isGoldCoin() ? danmaku.getTotalCoin() : 0));

    // 本次送礼银瓜子
    if (msg.contains("%gift_silver%"))
        msg.replace("%gift_silver%", snum(danmaku.isGoldCoin() ? 0 : danmaku.getTotalCoin()));

    // 本次送礼金瓜子+银瓜子（应该只有一个，但直接相加了）
    if (msg.contains("%gift_coin%"))
        msg.replace("%gift_coin%", snum(danmaku.getTotalCoin()));

    // 是否是金瓜子礼物
    if (msg.contains("%coin_gold%"))
        msg.replace("%coin_gold%", danmaku.isGoldCoin() ? "1" : "0");

    // 本次送礼名字
    if (msg.contains("%gift_name%"))
        msg.replace("%gift_name%", giftNames.contains(danmaku.getGiftId()) ? giftNames.value(danmaku.getGiftId()) : danmaku.getGiftName());

    if (msg.contains("%origin_gift_name%")) // 原始礼物名字
        msg.replace("%origin_gift_name%", danmaku.getGiftName());

    // 本次送礼数量
    if (msg.contains("%gift_num%"))
        msg.replace("%gift_num%", snum(danmaku.getNumber()));

    // 总共赠送金瓜子
    if (msg.contains("%total_gold%"))
        msg.replace("%total_gold%", snum(danmakuCounts->value("gold/"+snum(danmaku.getUid())).toInt()));

    // 总共赠送银瓜子
    if (msg.contains("%total_silver%"))
        msg.replace("%total_silver%", snum(danmakuCounts->value("silver/"+snum(danmaku.getUid())).toInt()));

    // 粉丝牌房间
    if (msg.contains("%anchor_roomid%"))
        msg.replace("%anchor_roomid%", danmaku.getAnchorRoomid());
    if (msg.contains("%medal_roomid%"))
        msg.replace("%medal_roomid%", danmaku.getAnchorRoomid());

    // 粉丝牌名字
    if (msg.contains("%medal_name%"))
        msg.replace("%medal_name%", danmaku.getMedalName());

    // 粉丝牌等级
    if (msg.contains("%medal_level%"))
        msg.replace("%medal_level%", snum(danmaku.getMedalLevel()));

    // 粉丝牌主播
    if (msg.contains("%medal_up%"))
        msg.replace("%medal_up%", danmaku.getMedalUp());

    // 房管
    if (msg.contains("%admin%"))
        msg.replace("%admin%", danmaku.isGuard() ? "1" : "0");

    // 舰长
    if (msg.contains("%guard%"))
        msg.replace("%guard%", danmaku.isGuard() ? "1" : "0");

    // 房管或舰长
    if (msg.contains("%admin_or_guard%"))
        msg.replace("%admin_or_guard%", (danmaku.isGuard() || danmaku.isAdmin()) ? "1" : "0");

    // 昵称长度
    if (msg.contains("%nickname_len%"))
        msg.replace("%nickname_len%", snum(danmaku.getNickname().length()));

    // 礼物名字长度
    if (msg.contains("%giftname_len%"))
        msg.replace("%giftname_len%", snum((giftNames.contains(danmaku.getGiftId()) ? giftNames.value(danmaku.getGiftId()) : danmaku.getGiftName()).length()));

    // 昵称+礼物名字长度
    if (msg.contains("%name_sum_len%"))
        msg.replace("%name_sum_len%", snum(danmaku.getNickname().length() + (giftNames.contains(danmaku.getGiftId()) ? giftNames.value(danmaku.getGiftId()) : danmaku.getGiftName()).length()));

    if (msg.contains("%ainame_sum_len%"))
    {
        QString local = getLocalNickname(danmaku.getUid());
        if (local.isEmpty())
            local = nicknameSimplify(danmaku.getNickname());
        if (local.isEmpty())
            local = danmaku.getNickname();
        msg.replace("%ainame_sum_len%", snum(local.length() + (giftNames.contains(danmaku.getGiftId()) ? giftNames.value(danmaku.getGiftId()) : danmaku.getGiftName()).length()));
    }

    // 是否新关注
    if (msg.contains("%new_attention%"))
    {
        bool isInFans = false;
        qint64 uid = danmaku.getUid();
        foreach (FanBean fan, fansList)
            if (fan.mid == uid)
            {
                isInFans = true;
                break;
            }
        msg.replace("%new_attention%", isInFans ? "1" : "0");
    }

    // 是否是对面串门
    if (msg.contains("%pk_opposite%"))
        msg.replace("%pk_opposite%", danmaku.isOpposite() ? "1" : "0");

    // 是否是己方串门回来
    if (msg.contains("%pk_view_return%"))
        msg.replace("%pk_view_return%", danmaku.isViewReturn() ? "1" : "0");

    // 本次进来人次
    if (msg.contains("%today_come%"))
        msg.replace("%today_come%", snum(dailyCome));

    // 新人发言数量
    if (msg.contains("%today_newbie_msg%"))
        msg.replace("%today_newbie_msg%", snum(dailyNewbieMsg));

    // 今天弹幕总数
    if (msg.contains("%today_danmaku%"))
        msg.replace("%today_danmaku%", snum(dailyDanmaku));

    // 今天新增关注
    if (msg.contains("%today_fans%"))
        msg.replace("%today_fans%", snum(dailyNewFans));

    // 当前粉丝数量111
    if (msg.contains("%fans_count%"))
        msg.replace("%fans_count%", snum(dailyTotalFans));

    // 今天金瓜子总数
    if (msg.contains("%today_gold%"))
        msg.replace("%today_gold%", snum(dailyGiftGold));

    // 今天银瓜子总数
    if (msg.contains("%today_silver%"))
        msg.replace("%today_silver%", snum(dailyGiftSilver));

    // 今天是否有新舰长
    if (msg.contains("%today_guard%"))
        msg.replace("%today_guard%", snum(dailyGuard));

    // 当前时间
    if (msg.contains("%time_hour%"))
        msg.replace("%time_hour%", snum(QTime::currentTime().hour()));
    if (msg.contains("%time_minute%"))
        msg.replace("%time_minute%", snum(QTime::currentTime().minute()));
    if (msg.contains("%time_second%"))
        msg.replace("%time_second%", snum(QTime::currentTime().second()));
    if (msg.contains("%time_day%"))
        msg.replace("%time_day%", snum(QDate::currentDate().day()));
    if (msg.contains("%time_month%"))
        msg.replace("%time_month%", snum(QDate::currentDate().month()));
    if (msg.contains("%time_year%"))
        msg.replace("%time_year%", snum(QDate::currentDate().year()));
    if (msg.contains("%time_day_week%"))
        msg.replace("%time_day_week%", snum(QDate::currentDate().dayOfWeek()));
    if (msg.contains("%time_day_year%"))
        msg.replace("%time_day_year%", snum(QDate::currentDate().dayOfYear()));
    if (msg.contains("%timestamp%"))
        msg.replace("%timestamp%", snum(QDateTime::currentSecsSinceEpoch()));
    if (msg.contains("%timestamp13%"))
        msg.replace("%timestamp13%", snum(QDateTime::currentMSecsSinceEpoch()));

    // 大乱斗
    if (msg.contains("%pking%"))
        msg.replace("%pking%", snum(pking ? 1 : 0));

    // 房间属性
    if (msg.contains("%living%"))
        msg.replace("%living%", snum(liveStatus ? 1 : 0));
    if (msg.contains("%room_id%"))
        msg.replace("%room_id%", roomId);
    if (msg.contains("%room_name%"))
        msg.replace("%room_name%", roomTitle);
    if (msg.contains("%up_name%"))
        msg.replace("%up_name%", upName);
    if (msg.contains("%up_uid%"))
        msg.replace("%up_uid%", upUid);
    if (msg.contains("%my_uid%"))
        msg.replace("%my_uid%", cookieUid);
    if (msg.contains("%my_uname%"))
        msg.replace("%my_uname%", cookieUname);

    // 本地设置
    // 特别关心
    if (msg.contains("%care%"))
        msg.replace("%care%", careUsers.contains(danmaku.getUid()) ? "1" : "0");
    // 强提醒
    if (msg.contains("%strong_notify%"))
        msg.replace("%strong_notify%", strongNotifyUsers.contains(danmaku.getUid()) ? "1" : "0");
    // 是否被禁言
    if (msg.contains("%blocked%"))
        msg.replace("%blocked%", userBlockIds.contains(danmaku.getUid()) ? "1" : "0");
    // 不自动欢迎
    if (msg.contains("%not_welcome%"))
        msg.replace("%noe_welcome%", notWelcomeUsers.contains(danmaku.getUid()) ? "1" : "0");

    return msg;
}

/**
 * 处理条件变量
 * [exp1, exp2]...
 * 要根据时间戳、字符串
 * @return 如果返回空字符串，则不符合；否则返回去掉表达式后的正文
 */
QString MainWindow::processVariantConditions(QString msg) const
{
    QRegularExpression re("^\\s*\\[(.+?)\\]\\s*");
    QRegularExpression compRe("^\\s*([^<>=!]+?)\\s*([<>=!]{1,2})\\s*([^<>=!]+?)\\s*$");
    QRegularExpression intRe("^[\\d\\+\\-\\*\\/%]+$");
    QRegularExpressionMatch match;
    if (msg.indexOf(re, 0, &match) == -1) // 没有检测到表达式
        return msg;

    QString totalExp = match.capturedTexts().first(); // 整个表达式，带括号
    QString exprs = match.capturedTexts().at(1);
    QStringList orExps = exprs.split(QRegularExpression("(;|\\|\\|)"), QString::SkipEmptyParts);
    bool isTrue = false;
    foreach (QString orExp, orExps)
    {
        isTrue = true;
        QStringList andExps = orExp.split(QRegularExpression("(,|&&)"), QString::SkipEmptyParts);
//        qDebug() << "表达式or内：" << andExps;
        foreach (QString exp, andExps)
        {
//            qDebug() << "表达式and内：" << exp;
            if (exp.indexOf(compRe, 0, &match) == -1) // 非比较
            {
                exp = exp.trimmed();
                bool notTrue = exp.startsWith("!");
                if (notTrue) // 取反……
                {
                    exp = exp.right(exp.length() - 1);
                }
                if (exp.isEmpty() || exp == "0" || exp.toLower() == "false") // false
                {
                    if (!notTrue)
                    {
                        isTrue = false;
                        break;
                    }
                }
                else // true
                {
                    if (notTrue)
                    {
                        isTrue = false;
                        break;
                    }
                }
                // qDebug() << "无法解析表达式：" << exp;
                // qDebug() << "    原始语句：" << msg;
                continue;
            }

            // 比较类型
            QStringList caps = match.capturedTexts();
            QString s1 = caps.at(1);
            QString op = caps.at(2);
            QString s2 = caps.at(3);
//            qDebug() << "比较：" << s1 << op << s2;
            if (s1.indexOf(intRe) > -1 && s2.indexOf(intRe) > -1) // 都是整数
            {
                qint64 i1 = calcIntExpression(s1);
                qint64 i2 = calcIntExpression(s2);
//                qDebug() << "比较整数" << i1 << op << i2;
                if (!isConditionTrue<qint64>(i1, i2, op))
                {
                    isTrue = false;
                    break;
                }
            }
            else/* if (s1.startsWith("\"") || s1.endsWith("\"") || s1.startsWith("'") || s1.endsWith("'")
                    || s2.startsWith("\"") || s2.startsWith("\"") || s2.startsWith("'") || s2.startsWith("'")) // 都是字符串*/
            {
                auto removeQuote = [=](QString s) -> QString{
                    if (s.startsWith("\"") && s.endsWith("\""))
                        return s.mid(1, s.length()-2);
                    if (s.startsWith("'") && s.endsWith("'"))
                        return s.mid(1, s.length()-2);
                    return s;
                };
                s1 = removeQuote(s1);
                s2 = removeQuote(s2);
//                qDebug() << "比较字符串" << s1 << op << s2;
                if (!isConditionTrue<QString>(s1, s2, op))
                {
                    isTrue = false;
                    break;
                }
            }
            /*else
            {
                qDebug() << "error: 无法比较的表达式:" << match.capturedTexts().first();
                qDebug() << "    原始语句：" << msg;
            }*/
        }
        if (isTrue)
            break;
    }

    if (!isTrue)
        return "";
    return msg.right(msg.length() - totalExp.length());
}

/**
 * 计算纯int、运算符组成的表达式
 */
qint64 MainWindow::calcIntExpression(QString exp) const
{
    exp = exp.replace(QRegularExpression("\\s*"), ""); // 去掉所有空白
    QRegularExpression opRe("[\\+\\-\\*/%]");
    // 获取所有整型数值
    QStringList valss = exp.split(opRe); // 如果是-开头，那么会当做 0-x
    if (valss.size() == 0)
        return 0;
    QList<qint64> vals;
    foreach (QString val, valss)
        vals << val.toLongLong();
    // 获取所有运算符
    QStringList ops;
    QRegularExpressionMatchIterator i = opRe.globalMatch(exp);
    while (i.hasNext())
    {
        QRegularExpressionMatch match = i.next();
        ops << match.captured(0);
    }
    if (valss.size() != ops.size() + 1)
    {
        qDebug() << "错误的表达式：" << valss << ops << exp;
        return 0;
    }

    // 入栈：* / %
    for (int i = 0; i < ops.size(); i++)
    {
        // op[i] 操作 vals[i] x vals[i+1]
        if (ops[i] == "*")
            vals[i] *= vals[i+1];
        else if (ops[i] == "/")
        {
            if (vals[i+1] == 0)
            {
                qDebug() << "警告：被除数是0 ：" << exp;
                vals[i+1] = 1;
            }
            vals[i] /= vals[i+1];
        }
        else if (ops[i] == "%")
            vals[i] %= vals[i+1];
        else
            continue;
        vals.removeAt(i+1);
        ops.removeAt(i);
        i--;
    }

    // 顺序计算：+ -
    qint64 val = vals.first();
    for (int i = 0; i < ops.size(); i++)
    {
        if (ops[i] == "-")
            val -= vals[i+1];
        else if (ops[i] == "+")
            val += vals[i+1];
    }

    return val;
}

/**
 * 一个智能的用户昵称转简单称呼
 */
QString MainWindow::nicknameSimplify(QString nickname) const
{
    QString simp = nickname;

    // 没有取名字的，就不需要欢迎了
    QRegularExpression defaultRe("^([bB]ili_\\d+|\\d+_[bB]ili)$");
    if (simp.indexOf(defaultRe) > -1)
    {
        return "";
    }

    // 特殊字符
    simp = simp.replace(QRegularExpression("_|丨|丶|灬|ミ|丷"), "");

    // 去掉前缀后缀
    QStringList special{"~", "丶", "°", "゛", "-", "_", "ヽ"};
    QStringList starts{"我叫", "我是", "我就是", "可是", "一只", "是个", "是", "原来", "但是", "但", "在下", "做"};
    QStringList ends{"er", "啊", "呢", "呀", "哦", "呐", "巨凶", "吧", "呦", "诶", "哦", "噢"};
    starts += special;
    ends += special;
    for (int i = 0; i < starts.size(); i++)
    {
        QString start = starts.at(i);
        if (simp.startsWith(start))
        {
            simp.remove(0, start.length());
            i = 0; // 从头开始
        }
    }
    for (int i = 0; i < ends.size(); i++)
    {
        QString end = ends.at(i);
        if (simp.endsWith(end))
        {
            simp.remove(simp.length() - end.length(), end.length());
            i = 0; // 从头开始
        }
    }

    // xxx的xxx
    QRegularExpression deRe("^(.+)[的の](.{2,})$");
    QRegularExpressionMatch match;
    if (simp.indexOf(deRe, 0, &match) > -1 && match.capturedTexts().at(1).length() <= match.capturedTexts().at(2).length()*2)
    {
        QRegularExpression blank("(名字|^确)");
        if (match.capturedTexts().at(2).indexOf(blank) == -1) // 不包含黑名单
            simp = match.capturedTexts().at(2);
    }

    // 一大串 中文enen
    // 日语正则 [\u0800-\u4e00] ，实测“一”也会算在里面……？
    QRegularExpression ceRe("([\u4e00-\u9fa5]{2,})([-\\w\\d_\u0800-\u4dff]+)$");
    if (simp.indexOf(ceRe, 0, &match) > -1 && match.capturedTexts().at(1).length()*3 >= match.capturedTexts().at(2).length())
    {
        QString tmp = match.capturedTexts().at(1);
        if (!QString("的之の是叫有为奶在去着最").contains(tmp.right(1)))
        {
            simp = tmp;
        }
    }
    ceRe = QRegularExpression("^([-\\w\\d_\u0800-\u4dff]+)([\u4e00-\u9fa5]{2,})");
    if (simp.indexOf(ceRe, 0, &match) > -1 && match.capturedTexts().at(1).length() <= match.capturedTexts().at(2).length()*3)
    {
        QString tmp = match.capturedTexts().at(2);
        if (!QString("的之の是叫有为奶在去着最").contains(tmp.right(1)))
        {
            simp = tmp;
        }
    }

    QStringList extraExp{"^这个(.+)不太.+$", "^(.{3,})今天.+$", "最.+的(.{2,})$",
                         "^.+(?:要|我就是|叫我)(.+)$", "^.*还.+就(.{2})$",
                         "^(.{2})(不是|有点|才是|敲|很).+$"};
    for (int i = 0; i < extraExp.size(); i++)
    {
        QRegularExpression re(extraExp.at(i));
        if (simp.indexOf(re, 0, &match) > -1)
        {
            simp = match.capturedTexts().at(1);
            break;
        }
    }

    // xxx哥哥
    QRegularExpression gegeRe("^(.+)(大|小)?(鸽鸽|哥哥|爸爸|爷爷|奶奶|妈妈|朋友|盆友|魔王)$");
    if (simp.indexOf(gegeRe, 0, &match) > -1)
    {
        QString tmp = match.capturedTexts().at(1);
        simp = tmp;
    }

    // AAAA
    QRegularExpression dieRe("(.)\\1{2,}");
    if (simp.indexOf(dieRe, 0, &match) > -1)
    {
        QString ch = match.capturedTexts().at(1);
        QString all = match.capturedTexts().at(0);
        simp = simp.replace(all, QString("%1%1").arg(ch));
    }

    // ABAB
    dieRe = QRegularExpression("(.{2,})\\1{1,}");
    if (simp.indexOf(dieRe, 0, &match) > -1)
    {
        QString ch = match.capturedTexts().at(1);
        QString all = match.capturedTexts().at(0);
        simp = simp.replace(all, QString("%1").arg(ch));
    }

    if (simp.isEmpty())
        return nickname;
    return simp;
}

QString MainWindow::msgToShort(QString msg) const
{
    if (msg.length() <= 20)
        return msg;
    if (msg.contains(" "))
    {
        msg = msg.replace(" ", "");
        if (msg.length() <= 20)
            return msg;
    }
    if (msg.contains("“"))
    {
        msg = msg.replace("“", "");
        msg = msg.replace("”", "");
        if (msg.length() <= 20)
            return msg;
    }
    return msg;
}

double MainWindow::getPaletteBgProg() const
{
    return paletteProg;
}

void MainWindow::setPaletteBgProg(double x)
{
    this->paletteProg = x;
}

void MainWindow::startSaveDanmakuToFile()
{
    if (danmuLogFile)
        finishSaveDanmuToFile();

    QDir dir;
    dir.mkdir(QApplication::applicationDirPath()+"/danmaku_histories");
    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    danmuLogFile = new QFile(QApplication::applicationDirPath()+"/danmaku_histories/" + roomId + "_" + date + ".log");
    danmuLogFile->open(QIODevice::WriteOnly | QIODevice::Append);
    danmuLogStream = new QTextStream(danmuLogFile);
}

void MainWindow::finishSaveDanmuToFile()
{
    if (!danmuLogFile)
        return ;

    delete danmuLogStream;

    danmuLogFile->close();
    danmuLogFile->deleteLater();

    danmuLogFile = nullptr;
    danmuLogStream = nullptr;
}

void MainWindow::startCalculateDailyData()
{
    if (dailySettings)
    {
        saveCalculateDailyData();
        dailySettings->deleteLater();
    }

    QDir dir;
    dir.mkdir(QApplication::applicationDirPath()+"/live_daily");
    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    dailySettings = new QSettings(QApplication::applicationDirPath()+"/live_daily/" + roomId + "_" + date + ".ini", QSettings::Format::IniFormat);

    dailyCome = dailySettings->value("come", 0).toInt();
    dailyPeopleNum = dailySettings->value("people_num", 0).toInt();
    dailyDanmaku = dailySettings->value("danmaku", 0).toInt();
    dailyNewbieMsg = dailySettings->value("newbie_msg", 0).toInt();
    dailyNewFans = dailySettings->value("new_fans", 0).toInt();
    dailyTotalFans = dailySettings->value("total_fans", 0).toInt();
    dailyGiftSilver = dailySettings->value("gift_silver", 0).toInt();
    dailyGiftGold = dailySettings->value("gift_gold", 0).toInt();
    dailyGuard = dailySettings->value("guard", 0).toInt();
}

void MainWindow::saveCalculateDailyData()
{
    if (dailySettings)
    {
        // dailySettings->setValue("come", dailyCome);
        dailySettings->setValue("people_num", userComeTimes.size());
        // dailySettings->setValue("danmaku", dailyDanmaku);
        // dailySettings->setValue("newbie_msg", dailyNewbieMsg);
        // dailySettings->setValue("new_fans", dailyNewFans);
        dailySettings->setValue("total_fans", currentFans);
        // dailySettings->setValue("gift_silver", dailyGiftSilver);
        // dailySettings->setValue("gift_gold", dailyGiftGold);
        // dailySettings->setValue("guard", dailyGuard);
    }
}

void MainWindow::saveTouta()
{
    settings.setValue("pk/toutaCount", toutaCount);
    settings.setValue("pk/chiguaCount", chiguaCount);
    ui->pkAutoMelonCheck->setToolTip(QString("偷塔次数：%1\n吃瓜数量：%2").arg(toutaCount).arg(chiguaCount));
}

void MainWindow::startLiveRecord()
{
    finishLiveRecord();
    if (roomId.isEmpty())
        return ;

    getRoomLiveVideoUrl([=](QString url){
        recordUrl = "";

        qDebug() << "开始录播：" << url;
        QNetworkAccessManager manager;
        QNetworkRequest* request = new QNetworkRequest(QUrl(url));
        QEventLoop* loop = new QEventLoop();
        QNetworkReply *reply = manager.get(*request);

        QObject::connect(reply, SIGNAL(finished()), loop, SLOT(quit())); //请求结束并下载完成后，退出子事件循环
        connect(reply, &QNetworkReply::downloadProgress, this, [=](qint64 bytesReceived, qint64 bytesTotal){
            if (bytesReceived > 0)
            {
                qDebug() << "原始地址可直接下载";
                recordUrl = url;
                loop->quit();
                reply->deleteLater();
            }
        });
        loop->exec(); //开启子事件循环
        loop->deleteLater();

        // B站下载会有302重定向的，需要获取headers里面的Location值
        if (recordUrl.isEmpty())
        {
            auto headers = reply->rawHeaderPairs();
            for (int i = 0; i < headers.size(); i++)
                if (headers.at(i).first.toLower() == "location")
                {
                    recordUrl = headers.at(i).second;
                    qDebug() << "重定向下载地址：" << recordUrl;
                    break;
                }
            reply->deleteLater();
        }
        delete request;
        if (recordUrl.isEmpty())
        {
            qDebug() << "无法获取下载地址";
            return ;
        }

        // 开始下载文件
        startRecordUrl(recordUrl);
    });
}

void MainWindow::startRecordUrl(QString url)
{
    QDir dir(QApplication::applicationDirPath());
    dir.mkpath("record");
    dir.cd("record");
    QString path = QFileInfo(dir.absoluteFilePath(
                                 roomId + "_" + QDateTime::currentDateTime().toString("yyyy-MM-dd hh.mm.ss") + ".mp4"))
            .absoluteFilePath();

    ui->recordCheck->setText("录制中...");
    recordTimer->start();
    startRecordTime = QDateTime::currentMSecsSinceEpoch();
    recordLoop = new QEventLoop;
    QNetworkAccessManager manager;
    QNetworkRequest* request = new QNetworkRequest(QUrl(url));
    QNetworkReply *reply = manager.get(*request);
    QObject::connect(reply, SIGNAL(finished()), recordLoop, SLOT(quit())); //请求结束并下载完成后，退出子事件循环
    recordLoop->exec(); //开启子事件循环
    recordLoop->deleteLater();
    recordLoop = nullptr;
    qDebug() << "录播结束：" << path;

    QFile file(path);
    if (!file.open(QFile::WriteOnly))
    {
        qDebug() << "写入文件失败" << path;
        reply->deleteLater();
        return ;
    }
    QByteArray data = reply->readAll();
    if (!data.isEmpty())
    {
        qint64 write_bytes = file.write(data);
        file.flush();
        if (write_bytes != data.size())
            qDebug() << "写入文件大小错误" << write_bytes << "/" << data.size();
    }

    reply->deleteLater();
    delete request;
    startRecordTime = 0;
    ui->recordCheck->setText("录播");
    recordTimer->stop();

    // 可能是超时结束了，重新下载
    if (ui->recordCheck->isChecked() && liveStatus)
    {
        startLiveRecord();
    }
}

void MainWindow::finishLiveRecord()
{
    if (!recordLoop)
        return ;
    qDebug() << "结束录播";
    recordLoop->quit();
    recordLoop = nullptr;
}

void MainWindow::processDanmakuCmd(QString msg)
{
    if (!remoteControl)
        return ;

    if (msg == "关闭功能")
    {
        sendNotifyMsg(">已暂停自动弹幕");
        processDanmakuCmd("关闭欢迎");
        processDanmakuCmd("关闭关注答谢");
        processDanmakuCmd("关闭送礼答谢");
        processDanmakuCmd("关闭禁言");
    }
    else if (msg == "开启功能")
    {
        sendNotifyMsg(">已开启自动弹幕");
        processDanmakuCmd("开启欢迎");
        processDanmakuCmd("开启关注答谢");
        processDanmakuCmd("开启送礼答谢");
        processDanmakuCmd("开启禁言");
    }
    else if (msg == "关闭机器人")
    {
        socket->abort();
        connectServerTimer->stop();
    }
    else if (msg == "关闭欢迎")
    {
        ui->autoSendWelcomeCheck->setChecked(false);
        sendNotifyMsg(">已暂停自动欢迎");
    }
    else if (msg == "开启欢迎")
    {
        ui->autoSendWelcomeCheck->setChecked(true);
        sendNotifyMsg(">已开启自动欢迎");
    }
    else if (msg == "关闭关注答谢")
    {
        ui->autoSendAttentionCheck->setChecked(false);
        sendNotifyMsg(">已暂停自动答谢关注");
    }
    else if (msg == "开启关注答谢")
    {
        ui->autoSendAttentionCheck->setChecked(true);
        sendNotifyMsg(">已开启自动答谢关注");
    }
    else if (msg == "关闭送礼答谢")
    {
        ui->autoSendGiftCheck->setChecked(false);
        sendNotifyMsg(">已暂停自动答谢送礼");
    }
    else if (msg == "开启送礼答谢")
    {
        ui->autoSendGiftCheck->setChecked(true);
        sendNotifyMsg(">已开启自动答谢送礼");
    }
    else if (msg == "关闭禁言")
    {
        ui->autoBlockNewbieCheck->setChecked(false);
        on_autoBlockNewbieCheck_clicked();
        sendNotifyMsg(">已暂停新人关键词自动禁言");
    }
    else if (msg == "开启禁言")
    {
        ui->autoBlockNewbieCheck->setChecked(true);
        on_autoBlockNewbieCheck_clicked();
        sendNotifyMsg(">已开启新人关键词自动禁言");
    }
    else if (msg == "关闭偷塔")
    {
        ui->pkAutoMelonCheck->setChecked(false);
        on_pkAutoMelonCheck_clicked();
        sendNotifyMsg(">已暂停自动偷塔");
    }
    else if (msg == "开启偷塔")
    {
        ui->pkAutoMelonCheck->setChecked(true);
        on_pkAutoMelonCheck_clicked();
        sendNotifyMsg(">已开启自动偷塔");
    }
    else if (msg == "关闭点歌")
    {
        ui->DiangeAutoCopyCheck->setChecked(false);
        sendNotifyMsg(">已暂停自动点歌");
    }
    else if (msg == "开启点歌")
    {
        ui->DiangeAutoCopyCheck->setChecked(true);
        sendNotifyMsg(">已开启自动点歌");
    }
    else if (msg == "关闭点歌回复")
    {
        ui->diangeReplyCheck->setChecked(false);
        on_diangeReplyCheck_clicked();
        sendNotifyMsg(">已暂停自动点歌回复");
    }
    else if (msg == "开启点歌回复")
    {
        ui->diangeReplyCheck->setChecked(true);
        on_diangeReplyCheck_clicked();
        sendNotifyMsg(">已开启自动点歌回复");
    }
    else if (msg == "关闭自动连接")
    {
        ui->timerConnectServerCheck->setChecked(false);
        on_timerConnectServerCheck_clicked();
        sendNotifyMsg(">已暂停自动连接");
    }
    else if (msg == "开启自动连接")
    {
        ui->timerConnectServerCheck->setChecked(true);
        on_timerConnectServerCheck_clicked();
        sendNotifyMsg(">已开启自动连接");
    }
    else if (msg == "关闭定时任务")
    {
        for (int i = 0; i < ui->taskListWidget->count(); i++)
        {
            QListWidgetItem* item = ui->taskListWidget->item(i);
            auto widget = ui->taskListWidget->itemWidget(item);
            auto tw = static_cast<TaskWidget*>(widget);
            tw->check->setChecked(false);
        }
        saveTaskList();
        sendNotifyMsg(">已关闭定时任务");
    }
    else if (msg == "开启定时任务")
    {
        for (int i = 0; i < ui->taskListWidget->count(); i++)
        {
            QListWidgetItem* item = ui->taskListWidget->item(i);
            auto widget = ui->taskListWidget->itemWidget(item);
            auto tw = static_cast<TaskWidget*>(widget);
            tw->check->setChecked(true);
        }
        saveTaskList();
        sendNotifyMsg(">已开启定时任务");
    }
    else if (msg == "开启录播")
    {
        startLiveRecord();
        sendNotifyMsg(">已开启录播");
    }
    else if (msg == "关闭录播")
    {
        finishLiveRecord();
        sendNotifyMsg(">已关闭录播");
    }
}

void MainWindow::restoreCustomVariant(QString text)
{
    customVariant.clear();
    QStringList sl = text.split("\n", QString::SkipEmptyParts);
    foreach (QString s, sl)
    {
        QRegularExpression re("^\\s*(\\S+)\\s*=\\s?(.*)$");
        QRegularExpressionMatch match;
        if (s.indexOf(re, 0, &match) != -1)
        {
            QString key = match.captured(1);
            QString val = match.captured(2);
            customVariant.insert(key, val);
        }
        else
            qDebug() << "自定义变量读取失败：" << s;
    }
}

QString MainWindow::saveCustomVariant()
{
    QStringList sl;
    for (auto it = customVariant.begin(); it != customVariant.end(); ++it)
    {
        sl << it.key() + " = " + it.value();
    }
    return sl.join("\n");
}

void MainWindow::slotBinaryMessageReceived(const QByteArray &message)
{
    int operation = ((uchar)message[8] << 24)
            + ((uchar)message[9] << 16)
            + ((uchar)message[10] << 8)
            + ((uchar)message[11]);
    QByteArray body = message.right(message.length() - 16);
    SOCKET_DEB << "操作码=" << operation << "  正文=" << (body.left(35)) << "...";

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(body, &error);
    QJsonObject json;
    if (error.error == QJsonParseError::NoError)
        json = document.object();

    if (operation == AUTH_REPLY) // 认证包回复
    {
        if (json.value("code").toInt() != 0)
        {
            qDebug() << s8("认证出错");
        }
    }
    else if (operation == HEARTBEAT_REPLY) // 心跳包回复（人气值）
    {
        qint32 popularity = ((uchar)body[0] << 24)
                + ((uchar)body[1] << 16)
                + ((uchar)body[2] << 8)
                + (uchar)body[3];
        SOCKET_DEB << "人气值=" << popularity;
        if (liveStatus)
            ui->popularityLabel->setText("人气值：" + QString::number(popularity));
    }
    else if (operation == SEND_MSG_REPLY) // 普通包
    {
        QString cmd;
        if (!json.isEmpty())
        {
            cmd = json.value("cmd").toString();
            qDebug() << "普通CMD：" << cmd;
            SOCKET_INF << json;
        }

        if (cmd == "NOTICE_MSG") // 全站广播（不用管）
        {

        }
        else if (cmd == "ROOM_RANK")
        {
            /*{
                "cmd": "ROOM_RANK",
                "data": {
                    "color": "#FB7299",
                    "h5_url": "https://live.bilibili.com/p/html/live-app-rankcurrent/index.html?is_live_half_webview=1&hybrid_half_ui=1,5,85p,70p,FFE293,0,30,100,10;2,2,320,100p,FFE293,0,30,100,0;4,2,320,100p,FFE293,0,30,100,0;6,5,65p,60p,FFE293,0,30,100,10;5,5,55p,60p,FFE293,0,30,100,10;3,5,85p,70p,FFE293,0,30,100,10;7,5,65p,60p,FFE293,0,30,100,10;&anchor_uid=688893202&rank_type=master_realtime_area_hour&area_hour=1&area_v2_id=145&area_v2_parent_id=1",
                    "rank_desc": "娱乐小时榜 7",
                    "roomid": 22532956,
                    "timestamp": 1605749940,
                    "web_url": "https://live.bilibili.com/blackboard/room-current-rank.html?rank_type=master_realtime_area_hour&area_hour=1&area_v2_id=145&area_v2_parent_id=1"
                }
            }*/
            QJsonObject data = json.value("data").toObject();
            QString color = data.value("color").toString();
            QString desc = data.value("rank_desc").toString();
            ui->roomRankLabel->setStyleSheet("color: " + color + ";");
            ui->roomRankLabel->setText(desc);
            ui->roomRankLabel->setToolTip(QDateTime::currentDateTime().toString("更新时间：hh:mm:ss"));
            if (desc != ui->roomRankLabel->text()) // 排名有更新
                showLocalNotify("当前排名：" + desc);
        }
        else if (handlePK(json))
        {

        }
        else // 压缩弹幕消息
        {
            short protover = (message[6]<<8) + message[7];
            SOCKET_INF << "协议版本：" << protover;
            if (protover == 2) // 默认协议版本，zlib解压
            {
                slotUncompressBytes(body);
            }
            else if (protover == 0)
            {
                QJsonDocument document = QJsonDocument::fromJson(body, &error);
                if (error.error != QJsonParseError::NoError)
                {
                    qDebug() << s8("body转json出错：") << error.errorString();
                    return ;
                }
                QJsonObject json = document.object();
                QString cmd = json.value("cmd").toString();
                qDebug() << ">消息命令UNZC：" << cmd;

                if (cmd == "ROOM_RANK")
                {
                }
                else if (cmd == "ROOM_REAL_TIME_MESSAGE_UPDATE") // 实时信息改变
                {
                    // {"cmd":"ROOM_REAL_TIME_MESSAGE_UPDATE","data":{"roomid":22532956,"fans":1022,"red_notice":-1,"fans_club":50}}
                    QJsonObject data = json.value("data").toObject();
                    int fans = data.value("fans").toInt();
                    int fans_club = data.value("fans_club").toInt();
                    int delta_fans = 0, delta_club = 0;
                    if (currentFans || currentFansClub)
                    {
                        delta_fans = fans - currentFans;
                        delta_club = fans_club - currentFansClub;
                    }
                    currentFans = fans;
                    currentFansClub = fans_club;
                    qDebug() << s8("粉丝数量：") << fans << s8("  粉丝团：") << fans_club;
                    appendNewLiveDanmaku(LiveDanmaku(fans, fans_club,
                                                     delta_fans, delta_club));

                    dailyNewFans += delta_fans;
                    if (dailySettings)
                        dailySettings->setValue("new_fans", dailyNewFans);

//                    if (delta_fans) // 如果有变动，实时更新
//                        getFansAndUpdate();
                    fansLabel->setText("粉丝:" + snum(fans));
                    fansLabel->setToolTip("粉丝数量：" + snum(fans) + "，粉丝团：" + snum(fans_club));
                }
                else if (cmd == "WIDGET_BANNER") // 无关的横幅广播
                {}
                else if (cmd == "HOT_RANK_CHANGED") // 热门榜
                {
                    /*{
                        "cmd": "HOT_RANK_CHANGED",
                        "data": {
                            "rank": 14,
                            "trend": 2, // 趋势：1上升，2下降
                            "countdown": 1705,
                            "timestamp": 1610168495,
                            "web_url": "https://live.bilibili.com/p/html/live-app-hotrank/index.html?clientType=2\\u0026area_id=1",
                            "live_url": "……（太长了）",
                            "blink_url": "……（太长了）",
                            "live_link_url": "……（太长了）",
                            "pc_link_url": "……（太长了）",
                            "icon": "https://i0.hdslb.com/bfs/live/3f833451003cca16a284119b8174227808d8f936.png",
                            "area_name": "娱乐"
                        }
                    }*/
                    QJsonObject data = json.value("data").toObject();
                    int rank = data.value("rank").toInt();
                    int trend = data.value("trend").toInt(); // 趋势：1上升，2下降
                    QString area_name = data.value("area_name").toString();
                    QString msg = QString("热门榜 " + area_name + "榜 排名：" + snum(rank) + " " + (trend == 1 ? "↑" : "↓"));
                    rankLabel->setText(area_name + "榜 " + snum(rank) + " " + (trend == 1 ? "↑" : "↓"));
                    rankLabel->setToolTip(msg);
                }
                else if (cmd == "HOT_RANK_SETTLEMENT")
                {
                    /*{
                        "cmd": "HOT_RANK_SETTLEMENT",
                        "data": {
                            "rank": 9,
                            "uname": "丸嘻嘻",
                            "face": "http://i2.hdslb.com/bfs/face/17f1f3994cb4b2bba97f1557ffc7eb34a05e119b.jpg",
                            "timestamp": 1610173800,
                            "icon": "https://i0.hdslb.com/bfs/live/3f833451003cca16a284119b8174227808d8f936.png",
                            "area_name": "娱乐",
                            "url": "https://live.bilibili.com/p/html/live-app-hotrank/result.html?is_live_half_webview=1\\u0026hybrid_half_ui=1,5,250,200,f4eefa,0,30,0,0,0;2,5,250,200,f4eefa,0,30,0,0,0;3,5,250,200,f4eefa,0,30,0,0,0;4,5,250,200,f4eefa,0,30,0,0,0;5,5,250,200,f4eefa,0,30,0,0,0;6,5,250,200,f4eefa,0,30,0,0,0;7,5,250,200,f4eefa,0,30,0,0,0;8,5,250,200,f4eefa,0,30,0,0,0\\u0026areaId=1\\u0026cache_key=4417cab3fa8b15ad1b250ee29fd91c52",
                            "cache_key": "4417cab3fa8b15ad1b250ee29fd91c52",
                            "dm_msg": "恭喜主播 \\u003c% 丸嘻嘻 %\\u003e 荣登限时热门榜娱乐榜top9! 即将获得热门流量推荐哦！"
                        }
                    }*/
                    QJsonObject data = json.value("data").toObject();
                    int rank = data.value("rank").toInt();
                    QString uname = data.value("uname").toString();
                    QString area_name = data.value("area_name").toString();
                    QString msg = QString("恭喜荣登热门榜" + area_name + "榜 top" + snum(rank) + "!");
                    showLocalNotify(msg);
                }
                else
                {
                    qDebug() << "未处理的命令=" << cmd << "   正文=" << QString(body);
                }
            }
            else
            {
                qDebug() << s8("未知协议：") << protover << s8("，若有必要请处理");
                qDebug() << s8("未知正文：") << body;
            }
        }
    }
    else
    {

    }
//    delete[] body.data();
//    delete[] message.data();
    SOCKET_DEB << "消息处理结束";
}

/**
 * 博客来源：https://blog.csdn.net/doujianyoutiao/article/details/106236207
 */
QByteArray zlibToQtUncompr(const char *pZLIBData, uLongf dataLen/*, uLongf srcDataLen = 0x100000*/)
{
    char *pQtData = new char[dataLen + 4];
    char *pByte = (char *)(&dataLen);/*(char *)(&srcDataLen);*/
    pQtData[3] = *pByte;
    pQtData[2] = *(pByte + 1);
    pQtData[1] = *(pByte + 2);
    pQtData[0] = *(pByte + 3);
    memcpy(pQtData + 4, pZLIBData, dataLen);
    QByteArray qByteArray(pQtData, dataLen + 4);
    delete[] pQtData;
    return qUncompress(qByteArray);
}

void MainWindow::slotUncompressBytes(const QByteArray &body)
{
    splitUncompressedBody(zlibToQtUncompr(body.data(), body.size()+1));
}

void MainWindow::splitUncompressedBody(const QByteArray &unc)
{
    int offset = 0;
    short headerSize = 16;
    while (offset < unc.size() - headerSize)
    {
        int packSize = ((uchar)unc[offset+0] << 24)
                + ((uchar)unc[offset+1] << 16)
                + ((uchar)unc[offset+2] << 8)
                + (uchar)unc[offset+3];
        QByteArray jsonBa = unc.mid(offset + headerSize, packSize - headerSize);
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(jsonBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << s8("解析解压后的JSON出错：") << error.errorString();
            qDebug() << s8("包数值：") << offset << packSize << "  解压后大小：" << unc.size();
            qDebug() << s8(">>当前JSON") << jsonBa;
            qDebug() << s8(">>解压正文") << unc;
            return ;
        }
        QJsonObject json = document.object();
        QString cmd = json.value("cmd").toString();
        SOCKET_INF << "解压后获取到CMD：" << cmd;
        if (cmd != "ROOM_BANNER" && cmd != "ACTIVITY_BANNER_UPDATE_V2" && cmd != "PANEL"
                && cmd != "ONLINERANK")
            SOCKET_INF << "单个JSON消息：" << offset << packSize << QString(jsonBa);
        try {
            handleMessage(json);
        } catch (...) {
            qDebug() << s8("出错啦") << jsonBa;
        }

        offset += packSize;
    }
}

/**
 * 数据包解析： https://segmentfault.com/a/1190000017328813?utm_source=tag-newest#tagDataPackage
 */
void MainWindow::handleMessage(QJsonObject json)
{
    QString cmd = json.value("cmd").toString();
    qDebug() << s8(">消息命令ZCOM：") << cmd;
    if (cmd == "LIVE") // 开播？
    {
        emit signalLiveStart(roomId);
        if (pking || pkToLive + 30 > QDateTime::currentSecsSinceEpoch()) // PK导致的开播下播情况
            return ;
        QString roomId = json.value("roomid").toString();
        if (roomId.isEmpty())
            roomId = QString::number(static_cast<qint64>(json.value("roomid").toDouble()));
//        if (roomId == this->roomId || roomId == this->shortId) // 是当前房间的
        {
            QString text = ui->startLiveWordsEdit->text();
            if (ui->startLiveSendCheck->isChecked() && !text.trimmed().isEmpty())
                sendAutoMsg(text);
            ui->popularityLabel->setText("已开播");
            liveStatus = true;
            if (ui->timerConnectServerCheck->isChecked() && connectServerTimer->isActive())
                connectServerTimer->stop();
            if (ui->recordCheck->isChecked())
                startLiveRecord();
            slotStartWork();
        }
    }
    else if (cmd == "PREPARING") // 下播
    {
        if (pking || pkToLive + 30 > QDateTime::currentSecsSinceEpoch()) // PK导致的开播下播情况
            return ;
        QString roomId = json.value("roomid").toString();
//        if (roomId == this->roomId || roomId == this->shortId) // 是当前房间的
        {
            QString text = ui->endLiveWordsEdit->text();
            if (ui->startLiveSendCheck->isChecked() &&!text.trimmed().isEmpty())
                sendAutoMsg(text);
            ui->popularityLabel->setText("已下播");
            liveStatus = false;

            if (ui->timerConnectServerCheck->isChecked() && !connectServerTimer->isActive())
                connectServerTimer->start();
        }

        releaseLiveData();
    }
    else if (cmd == "ROOM_CHANGE")
    {
        getRoomInfo(false);
    }
    else if (cmd == "DANMU_MSG") // 收到弹幕
    {
        QJsonArray info = json.value("info").toArray();
        if (info.size() <= 2)
            QMessageBox::information(this, "弹幕数据 info", QString(QJsonDocument(info).toJson()));
        QJsonArray array = info[0].toArray();
        if (array.size() <= 3)
            QMessageBox::information(this, "弹幕数据 array", QString(QJsonDocument(array).toJson()));
        qint64 textColor = array[3].toInt(); // 弹幕颜色
        qint64 timestamp = static_cast<qint64>(array[4].toDouble());
        QString msg = info[1].toString();
        QJsonArray user = info[2].toArray();
        if (user.size() <= 1)
            QMessageBox::information(this, "弹幕数据 user", QString(QJsonDocument(user).toJson()));
        qint64 uid = static_cast<qint64>(user[0].toDouble());
        QString username = user[1].toString();
//        int manager = user[2].toInt(); // 是否为房管（主播也属于房管）
//        int vip = user[3].toInt(); // 是否为老爷
//        int svip = user[4].toInt(); // 是否为年费老爷
//        int uidentity = user[5].toInt(); // 是否为非正式会员或正式会员（5000非，10000正）
//        int iphone = user[6].toInt(); // 是否绑定手机
        QString unameColor = user[7].toString();
        int level = info[4].toArray()[0].toInt();
        QJsonArray medal = info[3].toArray();
        int uguard = info[7].toInt(); // 用户本房间舰队身份：0非，1总督，2提督，3舰长
        int medal_level = 0;

        bool opposite = pking &&
                ((oppositeAudience.contains(uid) && !myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() && medal.size() >= 4 &&
                     snum(static_cast<qint64>(medal[3].toDouble())) == pkRoomId));

        // !弹幕的时间戳是13位，其他的是10位！
        qDebug() << s8("接收到弹幕：") << username << msg << QDateTime::fromMSecsSinceEpoch(timestamp);
        /*QString localName = danmakuWindow->getLocalNickname(uid);
        if (!localName.isEmpty())
            username = localName;*/

        // 统计弹幕次数
        int danmuCount = danmakuCounts->value("danmaku/"+snum(uid), 0).toInt()+1;
        danmakuCounts->setValue("danmaku/"+snum(uid), danmuCount);
        dailyDanmaku++;
        if (dailySettings)
            dailySettings->setValue("danmaku", dailyDanmaku);

        // 添加到列表
        QString cs = QString::number(textColor, 16);
        while (cs.size() < 6)
            cs = "0" + cs;
        LiveDanmaku danmaku(username, msg, uid, level, QDateTime::fromMSecsSinceEpoch(timestamp),
                                                 unameColor, "#"+cs);
        if (medal.size() >= 4)
        {
            medal_level = medal[0].toInt();
            danmaku.setMedal(snum(static_cast<qint64>(medal[3].toDouble())),
                    medal[1].toString(), medal_level, medal[2].toString());
        }
        if (snum(uid) == cookieUid && noReplyMsgs.contains(msg))
        {
            danmaku.setNoReply();
            noReplyMsgs.removeOne(msg);
        }
        else
            minuteDanmuPopular++;
        danmaku.setOpposite(opposite);
        appendNewLiveDanmaku(danmaku);

        // 新人发言
        if (danmuCount == 1)
        {
            dailyNewbieMsg++;
            if (dailySettings)
                dailySettings->setValue("newbie_msg", dailyNewbieMsg);
        }

        // 新人小号禁言
        bool blocked = false;
        auto testTipBlock = [&]{
            if (danmakuWindow && !ui->promptBlockNewbieKeysEdit->toPlainText().trimmed().isEmpty())
            {
                QString reStr = ui->promptBlockNewbieKeysEdit->toPlainText();
                if (reStr.endsWith("|"))
                    reStr = reStr.left(reStr.length()-1);
                if (msg.indexOf(QRegularExpression(reStr)) > -1) // 提示拉黑
                {
                    blocked = true;
                    danmakuWindow->showFastBlock(uid, msg);
                }
            }
        };
        if (snum(uid) == upUid || snum(uid) == cookieUid) // 是自己或UP主的，不屏蔽
        {
            // 不仅不屏蔽，反而支持主播特权
            processDanmakuCmd(msg);
        }
        else if ((level == 0 && medal_level <= 1 && danmuCount <= 3) || danmuCount <= 1)
        {
            // 自动拉黑
            if (ui->autoBlockNewbieCheck->isChecked() && !ui->autoBlockNewbieKeysEdit->toPlainText().trimmed().isEmpty())
            {
                QString reStr = ui->autoBlockNewbieKeysEdit->toPlainText();
                if (reStr.endsWith("|"))
                    reStr = reStr.left(reStr.length()-1);
                QRegularExpression re(reStr);
                QRegularExpressionMatch match;
                if (msg.indexOf(re, 0, &match) > -1 // 自动拉黑
                        && danmaku.getAnchorRoomid() != roomId // 不带有本房间粉丝牌
                        && !isInFans(uid) // 未刚关注主播（新人一般都是刚关注吧，在第一页）
                        && medal_level <= 2 // 勋章不到3级
                        )
                {
                    if (match.capturedTexts().size() > 1)
                    {
                        QString blockKey = match.captured(1); // 第一个括号的
                        showLocalNotify("检测到新人说【" + blockKey + "】，自动禁言");
                    }
                    qDebug() << "检测到新人违禁词，自动拉黑：" << username << msg;

                    // 拉黑
                    addBlockUser(uid, 720);
                    blocked = true;

                    // 通知
                    if (ui->autoBlockNewbieNotifyCheck->isChecked())
                    {
                        static int prevNotifyInCount = -20; // 上次发送通知时的弹幕数量
                        if (allDanmakus.size() - prevNotifyInCount >= 20) // 最低每20条发一遍
                        {
                            prevNotifyInCount = allDanmakus.size();

                            QStringList words = getEditConditionStringList(ui->autoBlockNewbieNotifyWordsEdit->toPlainText(), danmaku);
                            if (words.size())
                            {
                                int r = qrand() % words.size();
                                QString s = words.at(r);
                                if (!s.trimmed().isEmpty())
                                {
                                    sendNotifyMsg(s);
                                }
                            }
                        }
                    }
                }
            }

            // 提示拉黑
            if (!blocked)
            {
                testTipBlock();
            }
        }
        else if (ui->notOnlyNewbieCheck->isChecked())
        {
            testTipBlock();
        }

        if (!blocked)
            markNotRobot(uid);
    }
    else if (cmd == "SEND_GIFT") // 有人送礼
    {
        QJsonObject data = json.value("data").toObject();
        int giftId = data.value("giftId").toInt();
        int giftType = data.value("giftType").toInt();
        QString giftName = data.value("giftName").toString();
        QString username = data.value("uname").toString();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        int num = data.value("num").toInt();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble());
        QString coinType = data.value("coin_type").toString();
        int totalCoin = data.value("total_coin").toInt();

        qDebug() << s8("接收到送礼：") << username << giftId << giftName << num << s8("  总价值：") << totalCoin << coinType;
        QString localName = getLocalNickname(uid);
        /*if (!localName.isEmpty())
            username = localName;*/
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);
        bool merged = mergeGiftCombo(danmaku); // 如果有合并，则合并到之前的弹幕上面
        if (!merged)
        {
            appendNewLiveDanmaku(danmaku);
        }

        if (coinType == "silver")
        {
            int userSilver = danmakuCounts->value("silver/" + snum(uid)).toInt();
            userSilver += totalCoin;
            danmakuCounts->setValue("silver/"+snum(uid), userSilver);

            dailyGiftSilver += totalCoin;
            if (dailySettings)
                dailySettings->setValue("gift_silver", dailyGiftSilver);
        }
        if (coinType == "gold")
        {
            int userGold = danmakuCounts->value("gold/" + snum(uid)).toInt();
            userGold += totalCoin;
            danmakuCounts->setValue("gold/"+snum(uid), userGold);

            dailyGiftGold += totalCoin;
            if (dailySettings)
                dailySettings->setValue("gift_gold", dailyGiftGold);

            // 正在偷塔阶段
            if (pkEnding && uid == cookieUid.toLongLong()) // 机器人账号
            {
                pkVoting -= totalCoin;
                if (pkVoting < 0) // 自己在其他地方送了更大的礼物
                {
                    pkVoting = 0;
                }
            }
        }

        // 都送礼了，总该不是机器人了吧
        markNotRobot(uid);

//        if (coinType == "silver" && totalCoin < 1000 && num < 6 && !strongNotifyUsers.contains(uid)) // 银瓜子，而且还是小于1000，就不感谢了
//            return ;
        if (!justStart && ui->autoSendGiftCheck->isChecked() && !merged)
        {
            QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
            if (!words.size())
                return ;
            int r = qrand() % words.size();
            QString msg = words.at(r);
            if (strongNotifyUsers.contains(uid))
            {
                if (ui->sendGiftTextCheck->isChecked())
                    sendAutoMsg(msg);
                if (ui->sendGiftVoiceCheck->isChecked())
                    speekVariantText(msg);
            }
            else
                sendGiftMsg(msg);
        }
    }
    else if (cmd == "SUPER_CHAT_MESSAGE") // 醒目留言
    {
        qDebug() << "醒目留言：" << json;
    }
    else if (cmd == "SUPER_CHAT_MESSAGE_JPN") // 醒目留言日文翻译
    {
        qDebug() << "删除醒目留言：" << json;
    }
    else if (cmd == "SUPER_CHAT_MESSAGE_DELETE") // 删除醒目留言
    {
        qDebug() << "删除醒目留言：" << json;
    }
    else if (cmd == "SPECIAL_GIFT") // 节奏风暴
    {
        qDebug() << "删除醒目留言：" << json;
    }
    else if (cmd == "WELCOME_GUARD") // 舰长进入（不会触发），通过guard_level=1/2/3分辨总督/提督/舰长
    {
        QJsonObject data = json.value("data").toObject();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("username").toString();
        qint64 startTime = static_cast<qint64>(data.value("start_time").toDouble());
        qint64 endTime = static_cast<qint64>(data.value("end_time").toDouble());
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble());
        QString unameColor = data.value("uname_color").toString();
        QString spreadDesc = data.value("spread_desc").toString();
        QString spreadInfo = data.value("spread_info").toString();
        qDebug() << s8("舰长进入：") << username;
        /*QString localName = danmakuWindow->getLocalNickname(uid);
        if (!localName.isEmpty())
            username = localName;*/
        appendNewLiveDanmaku(LiveDanmaku(username, uid, QDateTime::fromSecsSinceEpoch(timestamp)
                                         , true, unameColor, spreadDesc, spreadInfo));
    }
    else  if (cmd == "ENTRY_EFFECT") // 舰长进入、高能榜（不知道到榜几）的同时会出现
    {
        /*{
            "cmd": "ENTRY_EFFECT",
            "data": {
                "id": 4,
                "uid": 20285041,
                "target_id": 688893202,
                "mock_effect": 0,
                "face": "https://i2.hdslb.com/bfs/face/24420cdcb6eeb119dbcd1f1843fdd8ada5b7d045.jpg",
                "privilege_type": 3,
                "copy_writing": "欢迎舰长 \\u003c%心乂鸽鸽%\\u003e 进入直播间",
                "copy_color": "#ffffff",
                "highlight_color": "#E6FF00",
                "priority": 70,
                "basemap_url": "https://i0.hdslb.com/bfs/live/mlive/f34c7441cdbad86f76edebf74e60b59d2958f6ad.png",
                "show_avatar": 1,
                "effective_time": 2,
                "web_basemap_url": "",
                "web_effective_time": 0,
                "web_effect_close": 0,
                "web_close_time": 0,
                "business": 1,
                "copy_writing_v2": "欢迎 \\u003c^icon^\\u003e 舰长 \\u003c%心乂鸽鸽%\\u003e 进入直播间",
                "icon_list": [
                    2
                ],
                "max_delay_time": 7
            }
        }*/

        QJsonObject data = json.value("data").toObject();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString copy_writing = data.value("copy_writing").toString();
        qDebug() << ">>>>>>舰长进入：" << copy_writing;
        QStringList results = QRegularExpression("欢迎(?:舰长|提督|总督).+<%(.+)%>").match(copy_writing).capturedTexts();
        if (results.size() < 2)
        {
            QStringList results = QRegularExpression("^欢迎\\s*<%(.+)%>").match(copy_writing).capturedTexts();
            if (results.size() < 2)
            {
                qDebug() << "识别舰长进入失败：" << copy_writing;
                qDebug() << data;
            }
            return ;
        }
        QString uname = results.at(1); // 这个昵称会被系统自动省略（太长后面会是两个点）

        LiveDanmaku danmaku(1, uname, uid, QDateTime::currentDateTime());
        if (cmAudience.contains(uid)) // 去了对面串门
        {
            if (cmAudience.value(uid) == 1) // 去对面串门回来
            {
                cmAudience[uid] = 0;
                danmaku.setViewReturn(true);
                showLocalNotify(uname + " 去对面串门回来");
            }
        }
        appendNewLiveDanmaku(danmaku);

        if (ui->autoSendWelcomeCheck->isChecked() && !notWelcomeUsers.contains(uid))
        {
            sendWelcome(danmaku);
        }
    }
    else if (cmd == "WELCOME") // 欢迎老爷，通过vip和svip区分月费和年费老爷
    {
        QJsonObject data = json.value("data").toObject();
        qDebug() << data;
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("uname").toString();
        bool isAdmin = data.value("isAdmin").toBool();
        qDebug() << s8("欢迎观众：") << username << isAdmin;
    }
    else if (cmd == "INTERACT_WORD")
    {
        /*{
            "cmd": "INTERACT_WORD",
            "data": {
                "contribution": {
                    "grade": 0
                },
                "fans_medal": {
                    "anchor_roomid": 0,
                    "guard_level": 0,
                    "icon_id": 0,
                    "is_lighted": 0,
                    "medal_color": 0,
                    "medal_color_border": 12632256,
                    "medal_color_end": 12632256,
                    "medal_color_start": 12632256,
                    "medal_level": 0,
                    "medal_name": "",
                    "score": 0,
                    "special": "",
                    "target_id": 0
                },
                "identities": [
                    1
                ],
                "is_spread": 0,
                "msg_type": 2,
                "roomid": 22532956,
                "score": 1610445087293,
                "spread_desc": "",
                "spread_info": "",
                "tail_icon": 0,
                "timestamp": 1610445087,
                "uid": 7696257,
                "uname": "黑心帝王瓜",
                "uname_color": ""
            }
        }*/

        QJsonObject data = json.value("data").toObject();
        int msgType = data.value("msg_type").toInt(); // 1进入直播间，2关注，3分享直播间
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("uname").toString();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble());
        bool isadmin = data.value("isadmin").toBool(); // 一般房管都是舰长吧？
        QString unameColor = data.value("uname_color").toString();
        bool isSpread = data.value("is_spread").toBool();
        QString spreadDesc = data.value("spread_desc").toString();
        QString spreadInfo = data.value("spread_info").toString();
        QJsonObject fansMedal = data.value("fans_medal").toObject();
        bool opposite = pking &&
                ((oppositeAudience.contains(uid) && !myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() &&
                     snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())) == pkRoomId));
        qDebug() << s8("观众进入：") << username << msgType;
        QString localName = getLocalNickname(uid);
        /*if (!localName.isEmpty())
            username = localName;*/
        LiveDanmaku danmaku(username, uid, QDateTime::fromSecsSinceEpoch(timestamp), isadmin,
                            unameColor, spreadDesc, spreadInfo);
        danmaku.setMedal(snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())),
                         fansMedal.value("medal_name").toString(),
                         fansMedal.value("medal_level").toInt(),
                         QString("#%1").arg(fansMedal.value("medal_color").toInt(), 6, 16, QLatin1Char('0')),
                         "");
        danmaku.setOpposite(opposite);

        if (msgType == 1) // 进入直播间
        {
            // [%come_time% > %timestamp%-3600]*%ai_name%，你回来了~ // 一小时内
            // [%come_time%>0, %come_time%<%timestamp%-3600*24]*%ai_name%，你终于来喽！
            int userCome = danmakuCounts->value("come/" + snum(uid)).toInt();
            danmaku.setNumber(userCome);
            danmaku.setPrevTimestamp(danmakuCounts->value("comeTime/"+snum(uid), 0).toLongLong());
            appendNewLiveDanmaku(danmaku);

            userCome++;
            danmakuCounts->setValue("come/"+snum(uid), userCome);
            danmakuCounts->setValue("comeTime/"+snum(uid), timestamp);

            dailyCome++;
            if (dailySettings)
                dailySettings->setValue("come", dailyCome);
            if (opposite)
            {
                // myAudience.insert(uid); // 加到自己这边来，免得下次误杀（即只提醒一次）
            }
            else if (cmAudience.contains(uid))
            {
                if (cmAudience.value(uid) == 1)
                {
                    cmAudience[uid] = 0;
                    danmaku.setViewReturn(true);
                    showLocalNotify(username + " 去对面串门回来");
                }
            }

            qint64 currentTime = QDateTime::currentSecsSinceEpoch();
            if (!justStart && ui->autoSendWelcomeCheck->isChecked())
            {
                int cd = ui->sendWelcomeCDSpin->value() * 1000 * 10; // 10倍冷却时间
                if (!strongNotifyUsers.contains(uid) && userComeTimes.contains(uid) && userComeTimes.value(uid) + cd > currentTime)
                    return ; // 避免同一个人连续欢迎多次（好像B站自动不发送？）
                userComeTimes[uid] = currentTime;
                sendWelcomeIfNotRobot(danmaku);
            }
            else
            {
                userComeTimes[uid] = currentTime; // 直接更新了
                judgeRobotAndMark(danmaku);
            }
        }
        else if (msgType == 2) // 关注 5不知道是啥啊
        {
            danmaku.transToAttention(timestamp);
            appendNewLiveDanmaku(danmaku);

            if (!justStart && ui->autoSendAttentionCheck->isChecked())
            {
                sendAttentionThankIfNotRobot(danmaku);
            }
            else
                judgeRobotAndMark(danmaku);
        }
        else if (msgType == 3) // 分享直播间
        {
            qDebug() << json;
            showLocalNotify(username + "分享了直播间", uid);
        }
        else
        {
            qDebug() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~新的进入msgType" << msgType << json;
        }
    }
    else if (cmd == "ROOM_BLOCK_MSG")
    {
        QString nickname = json.value("uname").toString();
        qint64 uid = static_cast<qint64>(json.value("uid").toDouble());
        appendNewLiveDanmaku(LiveDanmaku(nickname, uid));
    }
    else if (handlePK2(json)) // 太多了，换到单独一个方法里面
    {

    }
    else if (cmd == "GUARD_BUY") // 有人上舰
    {
        QJsonObject data = json.value("data").toObject();
        qDebug() << data;
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("username").toString();
        QString giftName = data.value("gift_name").toString();
        int price = data.value("price").toInt();
        int num = data.value("num").toInt();
        qint64 timestamp = static_cast <qint64>(data.value("timestamp").toDouble());
        qDebug() << username << s8("购买") << giftName << num;
        appendNewLiveDanmaku(LiveDanmaku(username, uid, giftName, num));

        int userGold = danmakuCounts->value("gold/" + snum(uid)).toInt();
        userGold += price;
        danmakuCounts->setValue("gold/"+snum(uid), userGold);

        dailyGuard += num;
        if (dailySettings)
            dailySettings->setValue("guard", dailyGuard);

        if (!justStart && ui->autoSendGiftCheck->isChecked())
        {
            QString localName = getLocalNickname(uid);
            QString nick = localName.isEmpty() ? nicknameSimplify(username) : localName;
            if (nick.isEmpty())
                nick = username;
            sendNotifyMsg("哇塞，感谢"+nick+"开通"+giftName+"！"); // 不好发，因为后期大概率是续费
        }
    }
    else if (cmd == "USER_TOAST_MSG") // 续费舰长会附带的；购买不知道
    {
        /*{
            "cmd": "USER_TOAST_MSG",
            "data": {
                "anchor_show": true,
                "color": "#00D1F1",
                "end_time": 1610167490,
                "guard_level": 3,
                "is_show": 0,
                "num": 1,
                "op_type": 3,
                "payflow_id": "2101091244192762134756573",
                "price": 138000,
                "role_name": "舰长",
                "start_time": 1610167490,
                "svga_block": 0,
                "target_guard_count": 128,
                "toast_msg": "<%分说的佛酱%> 自动续费了舰长",
                "uid": 480643475,
                "unit": "月",
                "user_show": true,
                "username": "分说的佛酱"
            }
        }*/
    }
    else if (cmd == "ONLINE_RANK_V2") // 礼物榜（高能榜）更新
    {
        /*{
            "cmd": "ONLINE_RANK_V2",
            "data": {
                "list": [
                    {
                        "face": "http://i0.hdslb.com/bfs/face/5eae154b4ed09c8ae4017325f5fa1ed8fa3757a9.jpg",
                        "guard_level": 3,
                        "rank": 1,
                        "score": "1380",
                        "uid": 480643475,
                        "uname": "分说的佛酱"
                    },
                    {
                        "face": "http://i2.hdslb.com/bfs/face/65536122b97302b86b93847054d4ab8cc155afe3.jpg",
                        "guard_level": 3,
                        "rank": 2,
                        "score": "610",
                        "uid": 407543009,
                        "uname": "布可人"
                    },
                    ..........
                ],
                "rank_type": "gold-rank"
            }
        }*/
    }
    else if (cmd == "ONLINE_RANK_TOP3") // 高能榜
    {
        /*{
            "cmd": "ONLINE_RANK_TOP3",
            "data": {
                "list": [
                    {
                        "msg": "恭喜 <%分说的佛酱%> 成为高能榜",
                        "rank": 1
                    }
                ]
            }
        }*/
    }
    else if (cmd == "ONLINE_RANK_COUNT")
    {
        /*{
            "cmd": "ONLINE_RANK_COUNT",
            "data": {
                "count": 9
            }
        }*/
    }
    else if (cmd == "NOTICE_MSG") // 为什么压缩的消息还有一遍？
    {
        /*{
            "business_id": "",
            "cmd": "NOTICE_MSG",
            "full": {
                "background": "#FFB03CFF",
                "color": "#FFFFFFFF",
                "head_icon": "https://i0.hdslb.com/bfs/live/72337e86020b8d0874d817f15c48a610894b94ff.png",
                "head_icon_fa": "https://i0.hdslb.com/bfs/live/72337e86020b8d0874d817f15c48a610894b94ff.png",
                "head_icon_fan": 1,
                "highlight": "#B25AC1FF",
                "tail_icon": "https://i0.hdslb.com/bfs/live/822da481fdaba986d738db5d8fd469ffa95a8fa1.webp",
                "tail_icon_fa": "https://i0.hdslb.com/bfs/live/38cb2a9f1209b16c0f15162b0b553e3b28d9f16f.png",
                "tail_icon_fan": 4,
                "time": 10
            },
            "half": {
                "background": "",
                "color": "",
                "head_icon": "",
                "highlight": "",
                "tail_icon": "",
                "time": 0
            },
            "link_url": "",
            "msg_common": "",
            "msg_self": "<%分说的佛酱%> 自动续费了主播的 <%舰长%>",
            "msg_type": 3,
            "real_roomid": 12735949,
            "roomid": 12735949,
            "scatter": {
                "max": 0,
                "min": 0
            },
            "shield_uid": -1,
            "side": {
                "background": "#FFE9C8FF",
                "border": "#FFCFA4FF",
                "color": "#EF903AFF",
                "head_icon": "https://i0.hdslb.com/bfs/live/31566d8cd5d468c30de8c148c5d06b3b345d8333.png",
                "highlight": "#D54900FF"
            }
        }*/
    }
    else if (cmd == "COMBO_SEND") // 连击礼物
    {
        /*{
            "cmd": "COMBO_SEND",
            "data": {
                "action": "投喂",
                "batch_combo_id": "batch:gift:combo_id:8833188:354580019:30607:1610168283.0188",
                "batch_combo_num": 9,
                "combo_id": "gift:combo_id:8833188:354580019:30607:1610168283.0182",
                "combo_num": 9,
                "combo_total_coin": 0,
                "gift_id": 30607,
                "gift_name": "小心心",
                "gift_num": 0,
                "is_show": 1,
                "medal_info": {
                    "anchor_roomid": 0,
                    "anchor_uname": "",
                    "guard_level": 0,
                    "icon_id": 0,
                    "is_lighted": 1,
                    "medal_color": 1725515,
                    "medal_color_border": 1725515,
                    "medal_color_end": 5414290,
                    "medal_color_start": 1725515,
                    "medal_level": 23,
                    "medal_name": "好听",
                    "special": "",
                    "target_id": 354580019
                },
                "name_color": "",
                "r_uname": "薄薄的温酱",
                "ruid": 354580019,
                "send_master": null,
                "total_num": 9,
                "uid": 8833188,
                "uname": "南酱的可露儿"
            }
        }*/
    }
    else if (cmd == "SPECIAL_GIFT")
    {
        /*{
            "cmd": "SPECIAL_GIFT",
            "data": {
                "39": {
                    "action": "end",
                    "id": 3032328093737
                }
            }
        }*/
    }
    else if (cmd == "ANCHOR_LOT_CHECKSTATUS") //  开启天选前的审核，审核过了才是真正开启
    {

    }
    else if (cmd == "ANCHOR_LOT_START") // 开启天选
    {
        /*{
            "cmd": "ANCHOR_LOT_START",
            "data": {
                "asset_icon": "https://i0.hdslb.com/bfs/live/992c2ccf88d3ea99620fb3a75e672e0abe850e9c.png",
                "award_image": "",
                "award_name": "5.2元红包",
                "award_num": 1,
                "cur_gift_num": 0,
                "current_time": 1610529938,
                "danmu": "娇娇赛高",
                "gift_id": 0,
                "gift_name": "",
                "gift_num": 1,
                "gift_price": 0,
                "goaway_time": 180,
                "goods_id": -99998,
                "id": 773667,
                "is_broadcast": 1,
                "join_type": 0,
                "lot_status": 0,
                "max_time": 600,
                "require_text": "关注主播",
                "require_type": 1, // 1是关注？
                "require_value": 0,
                "room_id": 22532956,
                "send_gift_ensure": 0,
                "show_panel": 1,
                "status": 1,
                "time": 599,
                "url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html?is_live_half_webview=1&hybrid_biz=live-lottery-anchor&hybrid_half_ui=1,5,100p,100p,000000,0,30,0,0,1;2,5,100p,100p,000000,0,30,0,0,1;3,5,100p,100p,000000,0,30,0,0,1;4,5,100p,100p,000000,0,30,0,0,1;5,5,100p,100p,000000,0,30,0,0,1;6,5,100p,100p,000000,0,30,0,0,1;7,5,100p,100p,000000,0,30,0,0,1;8,5,100p,100p,000000,0,30,0,0,1",
                "web_url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html"
            }
        }*/

        /*{
            "cmd": "ANCHOR_LOT_START",
            "data": {
                "asset_icon": "https://i0.hdslb.com/bfs/live/992c2ccf88d3ea99620fb3a75e672e0abe850e9c.png",
                "award_image": "",
                "award_name": "5.2元红包",
                "award_num": 1,
                "cur_gift_num": 0,
                "current_time": 1610535661,
                "danmu": "我就是天选之人！", // 送礼物的话也是需要发弹幕的（自动发送+自动送礼）
                "gift_id": 20008, // 冰阔落ID
                "gift_name": "冰阔落",
                "gift_num": 1,
                "gift_price": 1000,
                "goaway_time": 180,
                "goods_id": 15, // 物品ID？
                "id": 773836,
                "is_broadcast": 1,
                "join_type": 1,
                "lot_status": 0,
                "max_time": 600,
                "require_text": "无",
                "require_type": 0,
                "require_value": 0,
                "room_id": 22532956,
                "send_gift_ensure": 0,
                "show_panel": 1,
                "status": 1,
                "time": 599,
                "url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html?is_live_half_webview=1&hybrid_biz=live-lottery-anchor&hybrid_half_ui=1,5,100p,100p,000000,0,30,0,0,1;2,5,100p,100p,000000,0,30,0,0,1;3,5,100p,100p,000000,0,30,0,0,1;4,5,100p,100p,000000,0,30,0,0,1;5,5,100p,100p,000000,0,30,0,0,1;6,5,100p,100p,000000,0,30,0,0,1;7,5,100p,100p,000000,0,30,0,0,1;8,5,100p,100p,000000,0,30,0,0,1",
                "web_url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html"
            }
        }*/

        qDebug() << "天选时刻：" << json;
        QJsonObject data = json.value("data").toObject();
        qint64 id = static_cast<qint64>(data.value("id").toDouble());
        QString danmu = data.value("danmu").toString();
        int giftId = data.value("gift_id").toInt();
        qDebug() << "天选弹幕：" << danmu;
        if (!danmu.isEmpty() && giftId <= 0 && ui->autoLOTCheck->isChecked())
        {
            int requireType = data.value("require_type").toInt();
            joinLOT(id, requireType);
        }
    }
    else if (cmd == "ANCHOR_LOT_END") // 天选结束
    {

    }
    else if (cmd == "ANCHOR_LOT_AWARD") // 天选结果推送
    {

    }
    else
    {
        qDebug() << "未处理的命令：" << cmd << json;
    }
}

void MainWindow::sendWelcomeIfNotRobot(LiveDanmaku danmaku)
{
    if (!judgeRobot)
    {
        sendWelcome(danmaku);
        return ;
    }

    judgeUserRobotByFans(danmaku, [=](LiveDanmaku danmaku){
        sendWelcome(danmaku);
    }, [=](LiveDanmaku danmaku){
        // 实时弹幕显示机器人
        if (danmakuWindow)
            danmakuWindow->markRobot(danmaku.getUid());
    });
}

void MainWindow::sendAttentionThankIfNotRobot(LiveDanmaku danmaku)
{
    judgeUserRobotByFans(danmaku, [=](LiveDanmaku danmaku){
        sendAttentionThans(danmaku);
    }, [=](LiveDanmaku danmaku){
        // 实时弹幕显示机器人
        if (danmakuWindow)
            danmakuWindow->markRobot(danmaku.getUid());
    });
}

void MainWindow::judgeUserRobotByFans(LiveDanmaku danmaku, DanmakuFunc ifNot, DanmakuFunc ifIs)
{
    int val = robotRecord.value("robot/" + snum(danmaku.getUid()), 0).toInt();
    if (val == 1) // 是机器人
    {
        if (ifIs)
            ifIs(danmaku);
        return ;
    }
    else if (val == -1) // 是人
    {
        if (ifNot)
            ifNot(danmaku);
        return ;
    }
    else
    {
        if (danmaku.getMedalLevel() > 0 || danmaku.getLevel() > 1) // 使用等级判断
        {
            robotRecord.setValue("robot/" + snum(danmaku.getUid()), -1);
            if (ifNot)
                ifNot(danmaku);
            return ;
        }
    }

    // 网络判断
    QString url = "http://api.bilibili.com/x/relation/stat?vmid=" + snum(danmaku.getUid());
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取用户粉丝出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();

        int code = json.value("code").toInt();
        if (code != 0)
        {
            statusLabel->setText(json.value("message").toString());
            if(statusLabel->text().isEmpty() && code == 403)
                statusLabel->setText("您没有权限");
            return ;
        }
        QJsonObject obj = json.value("data").toObject();
        int following = obj.value("following").toInt(); // 关注
        int follower = obj.value("follower").toInt(); // 粉丝
        // int whisper = obj.value("whisper").toInt(); // 悄悄关注（自己关注）
        // int black = obj.value("black").toInt(); // 黑名单（自己登录）
        bool robot =  (following >= 100 && follower <= 5) || (follower > 0 && following > follower * 100); // 机器人，或者小号
//        qDebug() << "判断机器人：" << danmaku.getNickname() << "    粉丝数：" << following << follower << robot;
        if (robot)
        {
            // 进一步使用投稿数量判断
            judgeUserRobotByUpstate(danmaku, ifNot, ifIs);
        }
        else // 不是机器人
        {
            robotRecord.setValue("robot/" + snum(danmaku.getUid()), -1);
            if (ifNot)
                ifNot(danmaku);
        }
    });
    manager->get(*request);
}

void MainWindow::judgeUserRobotByUpstate(LiveDanmaku danmaku, DanmakuFunc ifNot, DanmakuFunc ifIs)
{
    QString url = "http://api.bilibili.com/x/space/upstat?mid=" + snum(danmaku.getUid());
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies()); // 这个好像需要cookies?
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取用户数据出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();

        int code = json.value("code").toInt();
        if (code != 0)
        {
            statusLabel->setText(json.value("message").toString());
            if(statusLabel->text().isEmpty() && code == 403)
                statusLabel->setText("您没有权限");
            return ;
        }
        QJsonObject obj = json.value("data").toObject();
        int achive_view = obj.value("archive").toObject().value("view").toInt();
        int article_view = obj.value("article").toObject().value("view").toInt();
        int article_like = obj.value("article").toObject().value("like").toInt();
        bool robot = (achive_view + article_view + article_like < 10); // 机器人，或者小号
//        qDebug() << "判断机器人：" << danmaku.getNickname() << "    视频播放量：" << achive_view
//                 << "  专栏阅读量：" << article_view << "  专栏点赞数：" << article_like << robot;
        robotRecord.setValue("robot/" + snum(danmaku.getUid()), robot ? 1 : -1);
        if (robot)
        {
            if (ifIs)
                ifIs(danmaku);
        }
        else // 不是机器人
        {
            if (ifNot)
            {
                ifNot(danmaku);
            }
        }
    });
    manager->get(*request);
}

void MainWindow::judgeUserRobotByUpload(LiveDanmaku danmaku, DanmakuFunc ifNot, DanmakuFunc ifIs)
{
    QString url = "http://api.vc.bilibili.com/link_draw/v1/doc/upload_count?uid=" + snum(danmaku.getUid());
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取用户投稿出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();

        int code = json.value("code").toInt();
        if (code != 0)
        {
            statusLabel->setText(json.value("message").toString());
            if(statusLabel->text().isEmpty() && code == 403)
                statusLabel->setText("您没有权限");
            return ;
        }
        QJsonObject obj = json.value("data").toObject();
        int allCount = obj.value("all_count").toInt();
        bool robot = (allCount <= 1); // 机器人，或者小号（1是因为有默认成为会员的相簿）
        qDebug() << "判断机器人：" << danmaku.getNickname() << "    投稿数量：" << allCount << robot;
        robotRecord.setValue("robot/" + snum(danmaku.getUid()), robot ? 1 : -1);
        if (robot)
        {
            if (ifIs)
                ifIs(danmaku);
        }
        else // 不是机器人
        {
            if (ifNot)
                ifNot(danmaku);
        }
    });
    manager->get(*request);
}

void MainWindow::sendWelcome(LiveDanmaku danmaku)
{
    if (notWelcomeUsers.contains(danmaku.getUid())
            || (!ui->sendWelcomeTextCheck->isChecked()
            && !ui->sendWelcomeVoiceCheck->isChecked())) // 不自动欢迎
        return ;
    QStringList words = getEditConditionStringList(ui->autoWelcomeWordsEdit->toPlainText(), danmaku);
//    if (strongNotifyUsers.contains(danmaku.getUid()))
//        showLocalNotify("TEST强提醒sendWelcome：" + words.join(";"));
    if (!words.size())
        return ;
    int r = qrand() % words.size();
    QString msg = words.at(r);
    if (strongNotifyUsers.contains(danmaku.getUid()))
    {
//        showLocalNotify("TEST强提醒sendWelcome2：" + msg);
        // 这里要特别判断一下语音，因为 sendNotifyMsg 是不会发送语音的
        if (ui->sendWelcomeTextCheck->isChecked())
            sendNotifyMsg(msg);
        if (ui->sendWelcomeVoiceCheck->isChecked())
            speekVariantText(msg);
    }
    else
    {
        if (danmaku.isOpposite())
            sendOppositeMsg(msg);
        else
        {
            if (danmaku.isGuard()) // 是舰长
                sendWelcomeGuard(msg);
            else
                sendWelcomeMsg(msg);
        }
    }
}

void MainWindow::sendAttentionThans(LiveDanmaku danmaku)
{
    QStringList words = getEditConditionStringList(ui->autoAttentionWordsEdit->toPlainText(), danmaku);
    if (!words.size())
        return ;
    int r = qrand() % words.size();
    QString msg = words.at(r);
    sendAttentionMsg(msg);
}

void MainWindow::judgeRobotAndMark(LiveDanmaku danmaku)
{
    if (!judgeRobot)
        return ;
    judgeUserRobotByFans(danmaku, nullptr, [=](LiveDanmaku danmaku){
        // 实时弹幕显示机器人
        if (danmakuWindow)
            danmakuWindow->markRobot(danmaku.getUid());
    });
}

void MainWindow::markNotRobot(qint64 uid)
{
    if (!judgeRobot)
        return ;
    int val = robotRecord.value("robot/" + snum(uid), 0).toInt();
    if (val != -1)
        robotRecord.setValue("robot/" + snum(uid), -1);
}

void MainWindow::speekVariantText(QString text)
{
    if(!tts || tts->state() != QTextToSpeech::Ready)
        return ;

    // 处理带变量的内容
    text = processTimeVariants(text);

    // 开始播放
    speekText(text);
}

void MainWindow::speekText(QString text)
{
    if(!tts || tts->state() != QTextToSpeech::Ready)
        return ;

    // 处理特殊字符
    text.replace("_", " ");

    // 开始播放
    tts->say(text);
}

void MainWindow::showScreenDanmaku(LiveDanmaku danmaku)
{
    if (!ui->enableScreenDanmakuCheck->isChecked()) // 不显示所有弹幕
        return ;
    if (!ui->enableScreenMsgCheck->isChecked() && danmaku.getMsgType() != MSG_DANMAKU) // 不显示所有msg
        return ;

    // 显示动画
    QLabel* label = new QLabel(nullptr);
    label->setWindowFlag(Qt::FramelessWindowHint, true);
    label->setWindowFlag(Qt::Tool, true);
    label->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    label->setAttribute(Qt::WA_TranslucentBackground, true); // 设置窗口透明
    label->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    if (danmaku.getMsgType() == MSG_DANMAKU || !danmaku.getText().isEmpty())
        label->setText(danmaku.getText());
    else
    {
        QString text = danmaku.toString();
        QRegularExpression re("^\\d+:\\d+:\\d+\\s+(.+)$"); // 简化表达式
        QRegularExpressionMatch match;
        if (text.indexOf(re, 0, &match) > -1)
            text = match.capturedTexts().at(1);
        label->setText(text);
    }
    label->setFont(screenDanmakuFont);

    auto isBlankColor = [=](QString c) -> bool {
        c = c.toLower();
        return c.isEmpty() || c == "#ffffff" || c == "#000000"
                || c == "#ffffffff" || c == "#00000000";
    };
    if (!danmaku.getTextColor().isEmpty() && !isBlankColor(danmaku.getTextColor()))
        label->setStyleSheet("color:" + danmaku.getTextColor() + ";");
    else
        label->setStyleSheet("color:" + QVariant(screenDanmakuColor).toString() + ";");
    label->adjustSize();
    label->show();

    QRect rect = getScreenRect();
    int left = rect.left() + rect.width() * ui->screenDanmakuLeftSpin->value() / 100;
    int right = rect.right() - rect.width() * ui->screenDanmakuRightSpin->value() / 100;
    int top = rect.top() + rect.height() * ui->screenDanmakuTopSpin->value() / 100;
    int bottom = rect.bottom() - rect.height() * ui->screenDanmakuBottomSpin->value() / 100;
    int sx = right, ex = left - label->width();
    if (left > right) // 从左往右
    {
        sx = right - label->width();
        ex = left;
    }

    // 避免重叠
    auto isOverlap = [=](int x, int y, int height){
        foreach (QLabel* label, screenLabels)
        {
            if (label->geometry().contains(x, y)
                    || label->geometry().contains(x, y+height))
                return true;
        }
        return false;
    };
    int forCount = 0;
    int y = 0;
    do {
        int ry = qrand() % (qMax(30, qAbs(bottom - top) - label->height()));
        y = qMin(top, bottom) + label->height() + ry;
        if (++forCount >= 32) // 最多循环32遍（如果弹幕极多，可能会必定叠在一起）
            break;
    } while (isOverlap(sx, y, label->height()));

    label->move(QPoint(sx, y));
    screenLabels.append(label);
    QPropertyAnimation* ani = new QPropertyAnimation(label, "pos");
    ani->setStartValue(QPoint(sx, y));
    ani->setEndValue(QPoint(ex, y));
    ani->setDuration(ui->screenDanmakuSpeedSpin->value() * 1000);
    connect(ani, &QPropertyAnimation::finished, ani, [=]{
        ani->deleteLater();
        screenLabels.removeOne(label);
        label->deleteLater();
    });
    ani->start();
}

/**
 * 合并消息
 * 在添加到消息队列前调用此函数判断
 * 若是，则同步合并实时弹幕中的礼物连击
 */
bool MainWindow::mergeGiftCombo(LiveDanmaku danmaku)
{
    if (danmaku.getMsgType() != MSG_GIFT)
        return false;

    // 判断，同人 && 礼物同名 && 3秒内
    qint64 uid = danmaku.getUid();
    int giftId = danmaku.getGiftId();
    qint64 time = danmaku.getTimeline().toSecsSinceEpoch();
    LiveDanmaku* merged = nullptr;

    // 遍历房间弹幕
    for (int i = roomDanmakus.size()-1; i >= 0; i--)
    {
        LiveDanmaku dm = roomDanmakus.at(i);
        qint64 t = dm.getTimeline().toSecsSinceEpoch();
        if (t == 0) // 有些是没带时间的
            continue;
        if (t + 3 < time) // 3秒以内
            return false;
        if (dm.getMsgType() != MSG_GIFT
                || dm.getUid() != uid
                || dm.getGiftId() != giftId)
            continue;

        // 是这个没错了
        merged = &roomDanmakus[i];
        break;
    }
    if (!merged)
        return false;

    // 开始合并
    qDebug() << "合并相同礼物至：" << merged->toString();
    merged->addGift(danmaku.getNumber(), danmaku.getTotalCoin(), danmaku.getTimeline());

    // 合并实时弹幕
    if (danmakuWindow)
        danmakuWindow->mergeGift(danmaku);

    return true;
}

bool MainWindow::handlePK(QJsonObject json)
{
    QString cmd = json.value("cmd").toString();

    if (cmd == "PK_BATTLE_START") // 开始大乱斗
    {
        pkStart(json);
    }
    else if (cmd == "PK_BATTLE_PROCESS") // 双方送礼信息
    {
        pkProcess(json);
    }
    else if (cmd == "PK_BATTLE_END") // 结束信息
    {
        pkEnd(json);
    }
    else if (cmd == "PK_BATTLE_SETTLE_USER")
    {
        /*{
            "cmd": "PK_BATTLE_SETTLE_USER",
            "data": {
                "battle_type": 0,
                "level_info": {
                    "first_rank_img": "https://i0.hdslb.com/bfs/live/078e242c4e2bb380554d55d0ac479410d75a0efc.png",
                    "first_rank_name": "白银斗士",
                    "second_rank_icon": "https://i0.hdslb.com/bfs/live/1f8c2a959f92592407514a1afeb705ddc55429cd.png",
                    "second_rank_num": 1
                },
                "my_info": {
                    "best_user": {
                        "award_info": null,
                        "award_info_list": [],
                        "badge": {
                            "desc": "",
                            "position": 0,
                            "url": ""
                        },
                        "end_win_award_info_list": {
                            "list": []
                        },
                        "exp": {
                            "color": 6406234,
                            "level": 1
                        },
                        "face": "http://i2.hdslb.com/bfs/face/d3d6f8659be3e309d6e58dd77accb3bb300215d5.jpg",
                        "face_frame": "http://i0.hdslb.com/bfs/live/78e8a800e97403f1137c0c1b5029648c390be390.png",
                        "pk_votes": 10,
                        "pk_votes_name": "乱斗值",
                        "uid": 64494330,
                        "uname": "我今天超可爱0"
                    },
                    "exp": {
                        "color": 9868950,
                        "master_level": {
                            "color": 5805790,
                            "level": 17
                        },
                        "user_level": 2
                    },
                    "face": "http://i2.hdslb.com/bfs/face/5b96b6ba5b078001e8159406710a8326d67cee5c.jpg",
                    "face_frame": "https://i0.hdslb.com/bfs/vc/d186b7d67d39e0894ebcc7f3ca5b35b3b56d5192.png",
                    "room_id": 22532956,
                    "uid": 688893202,
                    "uname": "娇娇子er"
                },
                "pk_id": "100729259",
                "result_info": {
                    "pk_crit_score": -1,
                    "pk_done_times": 17,
                    "pk_extra_score": 0,
                    "pk_extra_score_slot": "",
                    "pk_extra_value": 0,
                    "pk_resist_crit_score": -1,
                    "pk_task_score": 0,
                    "pk_times_score": 0,
                    "pk_total_times": -1,
                    "pk_votes": 10,
                    "pk_votes_name": "乱斗值",
                    "result_type_score": 12,
                    "task_score_list": [],
                    "total_score": 12,
                    "win_count": 2,
                    "win_final_hit": -1,
                    "winner_count_score": 0
                },
                "result_type": "2",
                "season_id": 29,
                "settle_status": 1,
                "winner": {
                    "best_user": {
                        "award_info": null,
                        "award_info_list": [],
                        "badge": {
                            "desc": "",
                            "position": 0,
                            "url": ""
                        },
                        "end_win_award_info_list": {
                            "list": []
                        },
                        "exp": {
                            "color": 6406234,
                            "level": 1
                        },
                        "face": "http://i2.hdslb.com/bfs/face/d3d6f8659be3e309d6e58dd77accb3bb300215d5.jpg",
                        "face_frame": "http://i0.hdslb.com/bfs/live/78e8a800e97403f1137c0c1b5029648c390be390.png",
                        "pk_votes": 10,
                        "pk_votes_name": "乱斗值",
                        "uid": 64494330,
                        "uname": "我今天超可爱0"
                    },
                    "exp": {
                        "color": 9868950,
                        "master_level": {
                            "color": 5805790,
                            "level": 17
                        },
                        "user_level": 2
                    },
                    "face": "http://i2.hdslb.com/bfs/face/5b96b6ba5b078001e8159406710a8326d67cee5c.jpg",
                    "face_frame": "https://i0.hdslb.com/bfs/vc/d186b7d67d39e0894ebcc7f3ca5b35b3b56d5192.png",
                    "room_id": 22532956,
                    "uid": 688893202,
                    "uname": "娇娇子er"
                }
            },
            "pk_id": 100729259,
            "pk_status": 401,
            "settle_status": 1,
            "timestamp": 1605748006
        }*/
    }
    else if (cmd == "PK_BATTLE_SETTLE_V2")
    {
        /*{
            "cmd": "PK_BATTLE_SETTLE_V2",
            "data": {
                "assist_list": [
                    {
                        "face": "http://i2.hdslb.com/bfs/face/d3d6f8659be3e309d6e58dd77accb3bb300215d5.jpg",
                        "id": 64494330,
                        "score": 10,
                        "uname": "我今天超可爱0"
                    }
                ],
                "level_info": {
                    "first_rank_img": "https://i0.hdslb.com/bfs/live/078e242c4e2bb380554d55d0ac479410d75a0efc.png",
                    "first_rank_name": "白银斗士",
                    "second_rank_icon": "https://i0.hdslb.com/bfs/live/1f8c2a959f92592407514a1afeb705ddc55429cd.png",
                    "second_rank_num": 1,
                    "uid": "688893202"
                },
                "pk_id": "100729259",
                "pk_type": "1",
                "result_info": {
                    "pk_extra_value": 0,
                    "pk_votes": 10,
                    "pk_votes_name": "乱斗值",
                    "total_score": 12
                },
                "result_type": 2,
                "season_id": 29
            },
            "pk_id": 100729259,
            "pk_status": 401,
            "settle_status": 1,
            "timestamp": 1605748006
        }*/
    }
    else if (cmd == "PK_LOTTERY_START") // 大乱斗胜利后的恭喜，实测在大乱斗送天空之翼后有，大概率更高礼物也有
    {
        /*{
            "cmd": "PK_LOTTERY_START",
            "data": {
                "asset_animation_pic": "https://i0.hdslb.com/bfs/vc/03be4c2912a4bd9f29eca3dac059c0e3e3fc69ce.gif",
                "asset_icon": "https://i0.hdslb.com/bfs/vc/44c367b09a8271afa22853785849e65797e085a1.png",
                "from_user": {
                    "face": "http://i2.hdslb.com/bfs/face/f25b706762e00a9adfe13e6147650891dd6f69a0.jpg",
                    "uid": 688893202,
                    "uname": "娇娇子er"
                },
                "id": 200105856,
                "max_time": 120,
                "pk_id": 200105856,
                "room_id": 22532956,
                "thank_text": "恭喜<%娇娇子er%>赢得大乱斗PK胜利",
                "time": 120,
                "time_wait": 0,
                "title": "恭喜主播大乱斗胜利",
                "weight": 0
            }
        }*/
    }
    else
    {
        return false;
    }

    return true;
}

bool MainWindow::handlePK2(QJsonObject json)
{
    QString cmd = json.value("cmd").toString();
    if (cmd == "PK_BATTLE_PRE") // 开始前的等待状态
    {
        pkPre(json);
    }
    else if (cmd == "PK_BATTLE_SETTLE") // 解决了对手？
    {
        /*{
            "cmd": "PK_BATTLE_SETTLE",
            "pk_id": 100729259,
            "pk_status": 401,
            "settle_status": 1,
            "timestamp": 1605748006,
            "data": {
                "battle_type": 1,
                "result_type": 2
            },
            "roomid": "22532956"
        }*/
        // result_type: 2赢，-1输
    }
    else
    {
        return false;
    }

    return true;
}

void MainWindow::refreshBlockList()
{
    if (browserData.isEmpty())
    {
        statusLabel->setText("请先设置用户数据");
        return ;
    }

    // 刷新被禁言的列表
    QString url = "https://api.live.bilibili.com/liveact/ajaxGetBlockList?roomid="+roomId+"&page=1";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");

    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取房间禁言出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();

        int code = json.value("code").toInt();
        if (code != 0)
        {
            statusLabel->setText(json.value("message").toString());
            if(statusLabel->text().isEmpty() && code == 403)
                statusLabel->setText("您没有权限");
            return ;
        }
        QJsonArray list = json.value("data").toArray();
        userBlockIds.clear();
        foreach (QJsonValue val, list)
        {
            QJsonObject obj = val.toObject();
            qint64 id = static_cast<qint64>(obj.value("id").toDouble());
            qint64 uid = static_cast<qint64>(obj.value("uid").toDouble());
            QString uname = obj.value("uname").toString();
            userBlockIds.insert(uid, id);
//            qDebug() << "已屏蔽:" << id << uname << uid;
        }

    });
    manager->get(*request);
}

bool MainWindow::isInFans(qint64 uid)
{
    foreach (auto fan, fansList)
    {
        if (fan.mid == uid)
            return true;
    }
    return false;
}

void MainWindow::sendGift(int giftId, int giftNum)
{
    if (roomId.isEmpty() || browserCookie.isEmpty())
    {
        qDebug() << "房间为空，或未登录";
        return ;
    }

    QUrl url("https://api.live.bilibili.com/gift/v2/Live/send");

    // 建立对象
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");

    // 设置数据（JSON的ByteArray）
    QStringList datas;
    datas << "uid=" + cookieUid;
    datas << "gift_id=" + snum(giftId);
    datas << "ruid=" + upUid;
    datas << "send_ruid=0";
    datas << "gift_num=" + snum(giftNum);
    datas << "coin_type=gold";
    datas << "bag_id=0";
    datas << "platform=pc";
    datas << "biz_code=live";
    datas << "biz_id=" + roomId;
    datas << "rnd=" + snum(QDateTime::currentSecsSinceEpoch());
    datas << "storm_beat_id=0";
    datas << "metadata=";
    datas << "price=0";
    datas << "csrf_token=" + csrf_token;
    datas << "csrf=" + csrf_token;
    datas << "visit_id=";

    QByteArray ba(datas.join("&").toStdString().data());

    // 连接槽
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
//        qDebug() << data;
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "赠送礼物出错：" << error.errorString();
            return ;
        }
        QJsonObject object = document.object();
        QString message = object.value("message").toString();
        statusLabel->setText("");
        if (message != "success")
        {
            statusLabel->setText(message);
            qDebug() << s8("warning: 发送失败：") << message << datas.join("&");
        }
    });

    manager->post(*request, ba);
}

void MainWindow::getRoomLiveVideoUrl(StringFunc func)
{
    if (roomId.isEmpty())
        return ;
    QString url = "http://api.live.bilibili.com/room/v1/Room/playUrl?cid=" + roomId
            + "&quality=4&qn=10000&platform=web&otype=json";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        SOCKET_INF << QString(data);
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取视频流网址出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取链接
        QJsonArray array = json.value("data").toObject().value("durl").toArray();
        /*"url": "http://d1--cn-gotcha04.bilivideo.com/live-bvc/521719/live_688893202_7694436.flv?cdn=cn-gotcha04\\u0026expires=1607505058\\u0026len=0\\u0026oi=1944862322\\u0026pt=\\u0026qn=10000\\u0026trid=40938d869aca4cb39041730f74ff9051\\u0026sigparams=cdn,expires,len,oi,pt,qn,trid\\u0026sign=ac701ba60c346bf3a173e56bcb14b7b9\\u0026ptype=0\\u0026src=9\\u0026sl=1\\u0026order=1",
          "length": 0,
          "order": 1,
          "stream_type": 0,
          "p2p_type": 0*/
        QString url = array.first().toObject().value("url").toString();
        if (func)
            func(url);
    });
    manager->get(*request);
}

void MainWindow::on_autoSendWelcomeCheck_stateChanged(int arg1)
{
    settings.setValue("danmaku/sendWelcome", ui->autoSendWelcomeCheck->isChecked());
    ui->sendWelcomeTextCheck->setEnabled(arg1);
    ui->sendWelcomeVoiceCheck->setEnabled(arg1);
}

void MainWindow::on_autoSendGiftCheck_stateChanged(int arg1)
{
    settings.setValue("danmaku/sendGift", ui->autoSendGiftCheck->isChecked());
    ui->sendGiftTextCheck->setEnabled(arg1);
    ui->sendGiftVoiceCheck->setEnabled(arg1);
}

void MainWindow::on_autoWelcomeWordsEdit_textChanged()
{
    settings.setValue("danmaku/autoWelcomeWords", ui->autoWelcomeWordsEdit->toPlainText());
}

void MainWindow::on_autoThankWordsEdit_textChanged()
{
    settings.setValue("danmaku/autoThankWords", ui->autoThankWordsEdit->toPlainText());
}

void MainWindow::on_startLiveWordsEdit_editingFinished()
{
    settings.setValue("live/startWords", ui->startLiveWordsEdit->text());
}

void MainWindow::on_endLiveWordsEdit_editingFinished()
{
    settings.setValue("live/endWords", ui->endLiveWordsEdit->text());
}

void MainWindow::on_startLiveSendCheck_stateChanged(int arg1)
{
    settings.setValue("live/startSend", ui->startLiveSendCheck->isChecked());
}

void MainWindow::on_autoSendAttentionCheck_stateChanged(int arg1)
{
    settings.setValue("danmaku/sendAttention", ui->autoSendAttentionCheck->isChecked());
    ui->sendAttentionTextCheck->setEnabled(arg1);
    ui->sendAttentionVoiceCheck->setEnabled(arg1);
}

void MainWindow::on_autoAttentionWordsEdit_textChanged()
{
    settings.setValue("danmaku/autoAttentionWords", ui->autoAttentionWordsEdit->toPlainText());
}

void MainWindow::on_sendWelcomeCDSpin_valueChanged(int arg1)
{
    settings.setValue("danmaku/sendWelcomeCD", arg1);
}

void MainWindow::on_sendGiftCDSpin_valueChanged(int arg1)
{
    settings.setValue("danmaku/sendGiftCD", arg1);
}

void MainWindow::on_sendAttentionCDSpin_valueChanged(int arg1)
{
    settings.setValue("danmaku/sendAttentionCD", arg1);
}

void MainWindow::showDiangeHistory()
{
    QStringList list;
    int first = qMax(0, diangeHistory.size() - 10);
    for (int i = diangeHistory.size()-1; i >= first; i--)
    {
        Diange dg = diangeHistory.at(i);
        list << dg.name + "  -  " + dg.nickname + "  " + dg.time.toString("hh:mm:ss");
    }
    QString text = list.size() ? list.join("\n") : "没有点歌记录";
    QMessageBox::information(this, "点歌历史", text);
}

void MainWindow::addBlockUser(qint64 uid, int hour)
{
    if(browserData.isEmpty())
    {
        statusLabel->setText("请先设置登录信息");
        return ;
    }

    QString url = "https://api.live.bilibili.com/banned_service/v2/Silent/add_block_user";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");

    QString data = QString("roomid=%1&block_uid=%2&hour=%3&csrf_token=%4&csrd=%5&visit_id=")
                    .arg(roomId).arg(uid).arg(hour).arg(csrf_token).arg(csrf_token);

    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

        qDebug() << "拉黑用户：" << uid << hour << QString(data);
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "添加禁言出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();

        if (json.value("code").toInt() != 0)
        {
            statusLabel->setText(json.value("message").toString());
            return ;
        }
        QJsonObject d = json.value("data").toObject();
        qint64 id = static_cast<qint64>(d.value("id").toDouble());
        userBlockIds[uid] = id;
    });
    manager->post(*request, data.toStdString().data());
}

void MainWindow::delBlockUser(qint64 uid)
{
    if(browserData.isEmpty())
    {
        statusLabel->setText("请先设置登录信息");
        return ;
    }

    if (userBlockIds.contains(uid))
    {
        qDebug() << "取消用户：" << uid << "  id =" << userBlockIds.value(uid);
        delRoomBlockUser(userBlockIds.value(uid));
        userBlockIds.remove(uid);
        return ;
    }

    // 获取直播间的网络ID，再取消屏蔽
    QString url = "https://api.live.bilibili.com/liveact/ajaxGetBlockList?roomid="+roomId+"&page=1";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");

    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "解除屏蔽出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();

        int code = json.value("code").toInt();
        if (code != 0)
        {
            statusLabel->setText(json.value("message").toString());
            if(statusLabel->text().isEmpty() && code == 403)
                statusLabel->setText("您没有权限");
            return ;
        }
        QJsonArray list = json.value("data").toArray();
        foreach (QJsonValue val, list)
        {
            QJsonObject obj = val.toObject();
            if (static_cast<qint64>(obj.value("uid").toDouble()) == uid)
            {
                delRoomBlockUser(static_cast<qint64>(obj.value("id").toDouble())); // 获取房间ID
                break;
            }
        }

    });
    manager->get(*request);
}

void MainWindow::delRoomBlockUser(qint64 id)
{
    QString url = "https://api.live.bilibili.com/banned_service/v1/Silent/del_room_block_user";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
    int posl = browserData.indexOf("csrf_token=") + 11;
    int posr = browserData.indexOf("&", posl);
    if (posr == -1) posr = browserData.length();
    QString csrf = browserData.mid(posl, posr - posl);
    QString data = QString("id=%1&roomid=%2&csrf_token=%4&csrd=%5&visit_id=")
                    .arg(id).arg(roomId).arg(csrf).arg(csrf);

    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

        qDebug() << "取消用户：" << id << QString(data);
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "解除禁言出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();

        if (json.value("code").toInt() != 0)
        {
            statusLabel->setText(json.value("message").toString());
            return ;
        }

        // if (userBlockIds.values().contains(id))
        //    userBlockIds.remove(userBlockIds.key(id));
    });
    manager->post(*request, data.toStdString().data());
}

void MainWindow::on_enableBlockCheck_clicked()
{
    bool enable = ui->enableBlockCheck->isChecked();
    settings.setValue("block/enableBlock", enable);
    if (danmakuWindow)
        danmakuWindow->setEnableBlock(enable);

    if (enable)
        refreshBlockList();
}


void MainWindow::on_newbieTipCheck_clicked()
{
    bool enable = ui->newbieTipCheck->isChecked();
    settings.setValue("block/newbieTip", enable);
    if (danmakuWindow)
        danmakuWindow->setNewbieTip(enable);
}

void MainWindow::on_autoBlockNewbieCheck_clicked()
{
    settings.setValue("block/autoBlockNewbie", ui->autoBlockNewbieCheck->isChecked());
    ui->autoBlockNewbieNotifyCheck->setEnabled(ui->autoBlockNewbieCheck->isChecked());
}

void MainWindow::on_autoBlockNewbieKeysEdit_textChanged()
{
    settings.setValue("block/autoBlockNewbieKeys", ui->autoBlockNewbieKeysEdit->toPlainText());
}

void MainWindow::on_autoBlockNewbieNotifyCheck_clicked()
{
    settings.setValue("block/autoBlockNewbieNotify", ui->autoBlockNewbieNotifyCheck->isChecked());
}

void MainWindow::on_autoBlockNewbieNotifyWordsEdit_textChanged()
{
    settings.setValue("block/autoBlockNewbieNotifyWords", ui->autoBlockNewbieNotifyWordsEdit->toPlainText());
}

void MainWindow::on_saveDanmakuToFileCheck_clicked()
{
    bool enabled = ui->saveDanmakuToFileCheck->isChecked();
    settings.setValue("danmaku/saveDanmakuToFile", enabled);
    if (enabled)
        startSaveDanmakuToFile();
    else
        finishSaveDanmuToFile();
}

void MainWindow::on_promptBlockNewbieCheck_clicked()
{
    settings.setValue("block/promptBlockNewbie", ui->promptBlockNewbieCheck->isChecked());
}

void MainWindow::on_promptBlockNewbieKeysEdit_textChanged()
{
    settings.setValue("block/promptBlockNewbieKeys", ui->promptBlockNewbieKeysEdit->toPlainText());
}

void MainWindow::on_timerConnectServerCheck_clicked()
{
    bool enable = ui->timerConnectServerCheck->isChecked();
    settings.setValue("live/timerConnectServer", enable);
    if (!liveStatus && enable)
        startConnectRoom();
}

void MainWindow::on_startLiveHourSpin_valueChanged(int arg1)
{
    settings.setValue("live/startLiveHour", ui->startLiveHourSpin->value());
    if (!justStart && ui->timerConnectServerCheck->isChecked() && connectServerTimer && !connectServerTimer->isActive())
        connectServerTimer->start();
}

void MainWindow::on_endLiveHourSpin_valueChanged(int arg1)
{
    settings.setValue("live/endLiveHour", ui->endLiveHourSpin->value());
    if (!justStart && ui->timerConnectServerCheck->isChecked() && connectServerTimer && !connectServerTimer->isActive())
        connectServerTimer->start();
}

void MainWindow::on_calculateDailyDataCheck_clicked()
{
    bool enable = ui->calculateDailyDataCheck->isChecked();
    settings.setValue("live/calculateDaliyData", enable);
    if (enable)
        startCalculateDailyData();
}

void MainWindow::on_pushButton_clicked()
{
    QString text = QDateTime::currentDateTime().toString("yyyy-MM-dd\n");
    text += "\n进来人次：" + snum(dailyCome);
    text += "\n观众人数：" + snum(userComeTimes.count());
    text += "\n弹幕数量：" + snum(dailyDanmaku);
    text += "\n新人弹幕：" + snum(dailyNewbieMsg);
    text += "\n新增关注：" + snum(dailyNewFans);
    text += "\n银瓜子数：" + snum(dailyGiftSilver);
    text += "\n金瓜子数：" + snum(dailyGiftGold);
    text += "\n上船次数：" + snum(dailyGuard);

    text += "\n\n累计粉丝：" + snum(currentFans);
    QMessageBox::information(this, "今日数据", text);
}

void MainWindow::on_removeDanmakuTipIntervalSpin_valueChanged(int arg1)
{
    this->removeDanmakuTipInterval = arg1 * 1000;
    settings.setValue("danmaku/removeTipInterval", arg1);
}

void MainWindow::on_doveCheck_clicked()
{

}

void MainWindow::on_notOnlyNewbieCheck_clicked()
{
    bool enable = ui->notOnlyNewbieCheck->isChecked();
    settings.setValue("block/notOnlyNewbie", enable);
}

void MainWindow::on_pkAutoMelonCheck_clicked()
{
    bool enable = ui->pkAutoMelonCheck->isChecked();
    settings.setValue("pk/autoMelon", enable);
}

void MainWindow::on_pkMaxGoldButton_clicked()
{
    bool ok = false;
    // 最大设置的是1000元，有钱人……
    int v = QInputDialog::getInt(this, "自动偷塔礼物上限", "单次PK偷塔赠送的金瓜子上限\n1元=1000金瓜子=100或10乱斗值（按B站规则会变）\n注意：每次偷塔、反偷塔时都单独判断上限，而非一次大乱斗的累加", pkMaxGold, 0, 1000000, 100, &ok);
    if (!ok)
        return ;

    settings.setValue("pk/maxGold", pkMaxGold = v);
}

void MainWindow::on_pkJudgeEarlyButton_clicked()
{
    bool ok = false;
    int v = QInputDialog::getInt(this, "自动偷塔提前判断", "每次偷塔结束前n毫秒判断，根据网速酌情设置\n1秒=1000毫秒，下次大乱斗生效", pkJudgeEarly, 0, 5000, 500, &ok);
    if (!ok)
        return ;

    settings.setValue("pk/judgeEarly", pkJudgeEarly = v);
}

template<typename T>
bool MainWindow::isConditionTrue(T a, T b, QString op) const
{
    if (op == "==" || op == "=")
        return a == b;
    if (op == "!=" || op == "<>")
        return a != b;
    if (op == ">")
        return a > b;
    if (op == ">=")
        return a >= b;
    if (op == "<")
        return a < b;
    if (op == "<=")
        return a <= b;
    qDebug() << "无法识别的比较模板类型：" << a << op << b;
    return false;
}

void MainWindow::on_roomIdEdit_returnPressed()
{
    if (socket->state() == QAbstractSocket::UnconnectedState)
    {
        startConnectRoom();
    }
}

void MainWindow::on_actionData_Path_triggered()
{
    QDesktopServices::openUrl(QUrl("file:///" + QApplication::applicationDirPath(), QUrl::TolerantMode));
}

/**
 * 显示实时弹幕
 */
void MainWindow::on_actionShow_Live_Danmaku_triggered()
{
    if (!danmakuWindow)
    {
        danmakuWindow = new LiveDanmakuWindow(settings, this);

        connect(this, SIGNAL(signalNewDanmaku(LiveDanmaku)), danmakuWindow, SLOT(slotNewLiveDanmaku(LiveDanmaku)));
        connect(this, SIGNAL(signalRemoveDanmaku(LiveDanmaku)), danmakuWindow, SLOT(slotOldLiveDanmakuRemoved(LiveDanmaku)));
        connect(danmakuWindow, SIGNAL(signalSendMsg(QString)), this, SLOT(sendMsg(QString)));
        connect(danmakuWindow, SIGNAL(signalAddBlockUser(qint64, int)), this, SLOT(addBlockUser(qint64, int)));
        connect(danmakuWindow, SIGNAL(signalDelBlockUser(qint64)), this, SLOT(delBlockUser(qint64)));
        connect(danmakuWindow, &LiveDanmakuWindow::signalChangeWindowMode, this, [=]{
            danmakuWindow->deleteLater();
            danmakuWindow = nullptr;
            on_actionShow_Live_Danmaku_triggered(); // 重新加载
        });
        connect(danmakuWindow, &LiveDanmakuWindow::signalSendMsgToPk, this, [=](QString msg){
            if (!pking || pkRoomId.isEmpty())
                return ;
            qDebug() << "发送PK对面消息：" << pkRoomId << msg;
            sendRoomMsg(pkRoomId, msg);
        });
        danmakuWindow->setAutoTranslate(ui->languageAutoTranslateCheck->isChecked());
        danmakuWindow->setAIReply(ui->AIReplyCheck->isChecked());
        danmakuWindow->setEnableBlock(ui->enableBlockCheck->isChecked());
        danmakuWindow->setNewbieTip(ui->newbieTipCheck->isChecked());
        danmakuWindow->setUpUid(upUid.toLongLong());
        danmakuWindow->hide();
        danmakuWindow->setWindowIcon(this->windowIcon());
        danmakuWindow->setWindowTitle("直播流-" + this->windowTitle());
    }

    bool hidding = danmakuWindow->isHidden();

    if (hidding)
    {
        danmakuWindow->show();
    }
    else
    {
        danmakuWindow->hide();
    }
    settings.setValue("danmaku/liveWindow", hidding);
}

void MainWindow::on_actionSet_Cookie_triggered()
{
    bool ok = false;
    QString s = QInputDialog::getText(this, "设置Cookie", "设置用户登录的cookie", QLineEdit::Normal, browserCookie, &ok);
    if (!ok)
        return ;

    if (!s.contains("SESSDATA="))
    {
        QMessageBox::warning(this, "设置Cookie", "设置Cookie失败，这不是Bilibili的浏览器Cookie");
        return ;
    }

    if (s.toLower().startsWith("cookie:"))
        s = s.replace(0, 7, "").trimmed();

    settings.setValue("danmaku/browserCookie", browserCookie = s);
    if (browserCookie.isEmpty())
        return ;

    getUserInfo();

    // 自动设置弹幕格式
    int posl = browserCookie.indexOf("bili_jct=") + 9;
    if (posl == -1)
        return ;
    int posr = browserCookie.indexOf(";", posl);
    if (posr == -1) posr = browserCookie.length();
    csrf_token = browserCookie.mid(posl, posr - posl);
    qDebug() << "检测到csrf_token:" << csrf_token;

    if (browserData.isEmpty())
        browserData = "color=4546550&fontsize=25&mode=4&msg=&rnd=1605156247&roomid=&bubble=5&csrf_token=&csrf=";
    browserData.replace(QRegularExpression("csrf_token=[^&]*"), "csrf_token=" + csrf_token);
    browserData.replace(QRegularExpression("csrf=[^&]*"), "csrf=" + csrf_token);
    settings.setValue("danmaku/browserData", browserData);
    qDebug() << "设置弹幕格式：" << browserData;
}

void MainWindow::on_actionSet_Danmaku_Data_Format_triggered()
{
    bool ok = false;
    QString s = QInputDialog::getText(this, "设置Data", "设置用户登录的data", QLineEdit::Normal, browserData, &ok);
    if (!ok)
        return ;

    settings.setValue("danmaku/browserData", browserData = s);
    int posl = browserData.indexOf("csrf_token=") + 11;
    int posr = browserData.indexOf("&", posl);
    if (posr == -1) posr = browserData.length();
    csrf_token = browserData.mid(posl, posr - posl);
}

void MainWindow::on_actionCookie_Help_triggered()
{
    QString steps = "发送弹幕前需按以下步骤注入登录信息：\n\n";
    steps += "步骤一：\n浏览器登录bilibili账号，按下F12（开发者调试工具）\n\n";
    steps += "步骤二：\n找到右边顶部的“Network”项，选择它下面的XHR\n\n";
    steps += "步骤三：\n刷新B站页面，中间多出一排列表，点其中任意一个，看右边“Headers”中的代码\n\n";
    steps += "步骤四：\n复制“Request Headers”下的“cookie”冒号后的一长串内容，粘贴到本程序“设置Cookie”中\n\n";
    steps += "设置好直播间ID、要发送的内容，即可发送弹幕！\n";
    steps += "注意：请勿过于频繁发送，容易被临时拉黑！";

    /*steps += "\n\n变量列表：\n";
    steps += "\\n：分成多条弹幕发送，间隔1.5秒";
    steps += "\n%hour%：根据时间替换为“早上”、“中午”、“晚上”等";
    steps += "\n%all_greet%：根据时间替换为“你好啊”、“早上好呀”、“晚饭吃了吗”、“还没睡呀”等";
    steps += "\n%greet%：根据时间替换为“你好”、“早上好”、“中午好”等";
    steps += "\n%tone%：随机替换为“啊”、“呀”";
    steps += "\n%punc%：随机替换为“~”、“！”";
    steps += "\n%tone/punc%：随机替换为“啊”、“呀”、“~”、“！”";*/
    QMessageBox::information(this, "定时弹幕", steps);
}

void MainWindow::on_actionCreate_Video_LRC_triggered()
{
    VideoLyricsCreator* vlc = new VideoLyricsCreator(nullptr);
    vlc->show();
}

void MainWindow::on_actionShow_Order_Player_Window_triggered()
{
    if (!playerWindow)
    {
        playerWindow = new OrderPlayerWindow(nullptr);
        connect(playerWindow, &OrderPlayerWindow::signalOrderSongSucceed, this, [=](Song song, qint64 latency, int waiting){
            qDebug() << "点歌成功" << song.simpleString() << latency;
            if (latency < 180000) // 小于3分钟
            {
                QString tip = "成功点歌：【" + song.simpleString() + "】";
                showLocalNotify(tip);
                if (ui->diangeReplyCheck->isChecked())
                {
                    if (waiting == 1 && latency > 20000)
                        sendNotifyMsg("成功点歌，下一首播放");
                    else if (waiting == 2 && latency > 20000)
                        sendNotifyMsg("成功点歌，下两首播放");
                    else // 多首队列
                        sendNotifyMsg("成功点歌");
                }
            }
            else // 超过3分钟
            {
                int minute = (latency+20000) / 60000;
                showLocalNotify(snum(minute) + "分钟后播放【" + song.simpleString() + "】");
                if (ui->diangeReplyCheck->isChecked())
                    sendNotifyMsg("成功点歌，" + snum(minute) + "分钟后播放");
            }
        });
        connect(playerWindow, &OrderPlayerWindow::signalOrderSongPlayed, this, [=](Song song){
            showLocalNotify("开始播放：" + song.simpleString());
        });
        connect(playerWindow, &OrderPlayerWindow::signalWindowClosed, this, [=]{
            QTimer::singleShot(5000, this, [=]{ // 延迟5秒，避免程序关闭时先把点歌姬关了，但下次还是需要显示的
                settings.setValue("danmaku/playerWindow", false);
            });
        });
    }

    bool hidding = playerWindow->isHidden();

    if (hidding)
    {
        playerWindow->show();
    }
    else
    {
        playerWindow->hide();
    }
    settings.setValue("danmaku/playerWindow", hidding);
}

void MainWindow::on_diangeReplyCheck_clicked()
{
    settings.setValue("danmaku/diangeReply", ui->diangeReplyCheck->isChecked());
}

void MainWindow::on_actionAbout_triggered()
{
    QString text;
    text += "由小乂独立开发。\n此程序仅供个人学习、研究之用，禁止用于商业用途。\n请在下载后24小时内删除！\n";
    text += "email : wxy@iwxyi.com\ngithub: https://github.com/iwxyi";
    QMessageBox::information(this, "关于", text);
}

void MainWindow::on_actionGitHub_triggered()
{
    QDesktopServices::openUrl(QUrl("https://github.com/iwxyi/Bilibili-MagicalDanmaku"));
}

void MainWindow::on_actionCustom_Variant_triggered()
{
    QString text = saveCustomVariant();
    bool ok;
    text = TextInputDialog::getText(this, "自定义变量", "请输入自定义变量，可在答谢、定时中使用：\n示例格式：%var%=val", text, &ok);
    if (!ok)
        return ;
    settings.setValue("danmaku/customVariant", text);

    restoreCustomVariant(text);
}

void MainWindow::on_actionSend_Long_Text_triggered()
{
    bool ok;
    QString text = QInputDialog::getText(this, "发送长文本", "请输入长文本（支持变量），分割为每次20字发送\n注意带有敏感词或特殊字符的部分将无法发送", QLineEdit::Normal, "", &ok);
    text = text.trimmed();

    QStringList sl;
    int len = text.length();
    const int maxOne = 20;
    int count = (len + maxOne - 1) / maxOne;
    for (int i = 0; i < count; i++)
    {
        sl << text.mid(i * maxOne, maxOne);
    }
    sendAutoMsg(sl.join("\\n"));
}

void MainWindow::on_actionShow_Lucky_Draw_triggered()
{
    if (!luckyDrawWindow)
    {
        luckyDrawWindow = new LuckyDrawWindow(nullptr);
        connect(this, SIGNAL(signalNewDanmaku(LiveDanmaku)), luckyDrawWindow, SLOT(slotNewDanmaku(LiveDanmaku)));
    }
    luckyDrawWindow->show();
}

void MainWindow::on_actionGet_Play_Url_triggered()
{
    getRoomLiveVideoUrl([=](QString url) {
        QApplication::clipboard()->setText(url);
    });
}

void MainWindow::on_actionShow_Live_Video_triggered()
{
    /*if (videoPlayer == nullptr)
    {
        videoPlayer = new LiveVideoPlayer(settings, nullptr);
        connect(this, &MainWindow::signalRoomChanged, this, [=](QString roomId){
            videoPlayer->setRoomId(roomId);
        });
    }
    videoPlayer->show();
    videoPlayer->setRoomId(roomId);*/
    if (roomId.isEmpty())
        return ;

    LiveVideoPlayer* player = new LiveVideoPlayer(settings, nullptr);
    connect(this, SIGNAL(signalLiveStart(QString)), player, SLOT(slotLiveStart(QString))); // 重新开播，需要刷新URL
    player->setAttribute(Qt::WA_DeleteOnClose, true);
    player->setRoomId(roomId);
    player->show();
}

void MainWindow::on_actionShow_PK_Video_triggered()
{
    if (pkRoomId.isEmpty())
        return ;

    LiveVideoPlayer* player = new LiveVideoPlayer(settings, nullptr);
    player->setAttribute(Qt::WA_DeleteOnClose, true);
    player->setRoomId(pkRoomId);
    player->show();
}

void MainWindow::on_pkChuanmenCheck_clicked()
{
    pkChuanmenEnable = ui->pkChuanmenCheck->isChecked();
    settings.setValue("pk/chuanmen", pkChuanmenEnable);

    if (pkChuanmenEnable)
    {
        connectPkRoom();
    }
}

void MainWindow::on_pkMsgSyncCheck_clicked()
{
    pkMsgSync = ui->pkMsgSyncCheck->isChecked();
    settings.setValue("pk/msgSync", pkMsgSync);
}

void MainWindow::pkPre(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_PRE",
        "pk_status": 101,
        "pk_id": 100970480,
        "timestamp": 1607763991,
        "data": {
            "battle_type": 1, // 自己开始匹配？
            "uname": "SLe\\u4e36\\u82cf\\u4e50",
            "face": "http:\\/\\/i2.hdslb.com\\/bfs\\/face\\/4636d48aeefa1a177bc2bdfb595892d3648b80b1.jpg",
            "uid": 13330958,
            "room_id": 271270,
            "season_id": 30,
            "pre_timer": 10,
            "pk_votes_name": "\\u4e71\\u6597\\u503c",
            "end_win_task": null
        },
        "roomid": 22532956
    }*/

    /*{ 对面匹配过来的情况
        "cmd": "PK_BATTLE_PRE",
        "pk_status": 101,
        "pk_id": 100970387,
        "timestamp": 1607763565,
        "data": {
            "battle_type": 2, // 对面开始匹配？
            "uname": "\\u519c\\u6751\\u9493\\u9c7c\\u5c0f\\u6b66\\u5929\\u5929\\u76f4\\u64ad",
            "face": "http:\\/\\/i0.hdslb.com\\/bfs\\/face\\/fbaa9cfbc214164236cdbe79a77bcaae5334e9ef.jpg",
            "uid": 199775659, // 对面用户ID
            "room_id": 12298098, // 对面房间ID
            "season_id": 30,
            "pre_timer": 10,
            "pk_votes_name": "\\u4e71\\u6597\\u503c", // 乱斗值
            "end_win_task": null
        },
        "roomid": 22532956
    }*/

    QJsonObject data = json.value("data").toObject();
    QString uname = data.value("uname").toString();
    QString uid = QString::number(static_cast<qint64>(data.value("uid").toDouble()));
    QString room_id = QString::number(static_cast<qint64>(data.value("room_id").toDouble()));
    pkUname = uname;
    pkRoomId = room_id;
    pkUid = uid;

    qDebug() << "准备大乱斗，已匹配：" << static_cast<qint64>(json.value("pk_id").toDouble());
    if (danmakuWindow)
    {
        if (uname.isEmpty())
            danmakuWindow->setStatusText("大乱斗匹配中...");
        else if (!pkRoomId.isEmpty())
        {
            int pkCount = danmakuCounts->value("pk/" + pkRoomId, 0).toInt();
            QString text = "匹配：" + uname;
            if(pkCount > 0)
                text += "[" + QString::number(pkCount) + "]";
            danmakuWindow->setStatusText(text);
            qDebug() << "主播：" << uname << pkUid << pkRoomId;
        }
    }
    pkToLive = QDateTime::currentSecsSinceEpoch();
}

void MainWindow::pkStart(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_START",
        "data": {
            "battle_type": 1, // 不知道其他类型是啥
            "final_hit_votes": 0,
            "pk_end_time": 1605748342,
            "pk_frozen_time": 1605748332,
            "pk_start_time": 1605748032,
            "pk_votes_add": 0,
            "pk_votes_name": "乱斗值",
            "pk_votes_type": 0
        },
        "pk_id": 100729281,
        "pk_status": 201,
        "timestamp": 1605748032
    }*/

    QJsonObject data = json.value("data").toObject();
    pking = true;
    qint64 startTime = static_cast<qint64>(data.value("pk_start_time").toDouble());
    qint64 endTime = static_cast<qint64>(data.value("pk_end_time").toDouble());
    pkEndTime = startTime + 300; // 因为endTime要延迟10秒，还是用startTime来判断吧
    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    qint64 deltaEnd = pkEndTime - currentTime;
    QString roomId = this->roomId;
    oppositeTouta = false;
    pkToLive = currentTime;
    cmAudience.clear();
    int battle_type = data.value("battle_type").toInt();
    if (battle_type == 1) // 普通大乱斗
        pkVideo = false;
    else if (battle_type == 2) // 视频大乱斗
        pkVideo = true;
    else
        pkVideo = false;

    // 结束后
    QTimer::singleShot(deltaEnd, [=]{
        pkEnding = false;
        pkVoting = 0;
    });

    // 结束前2秒
    QTimer::singleShot(deltaEnd*1000 - pkJudgeEarly, [=]{
        if (!pking || roomId != this->roomId) // 比如换房间了
        {
            qDebug() << "大乱斗结束前，逻辑不正确" << pking << roomId
                     << QDateTime::currentSecsSinceEpoch() << endTime;
            return ;
        }
        qDebug() << "大乱斗结束前情况：" << myVotes << matchVotes
                 << QDateTime::currentSecsSinceEpoch() << pkEndTime;

        pkEnding = true;
        pkVoting = 0;
        // 几个吃瓜就能解决的……
        if (ui->pkAutoMelonCheck->isChecked()
                && myVotes <= matchVotes && myVotes + pkMaxGold/goldTransPk > matchVotes)
        {
            // 调用送礼
            int melon = 100 / goldTransPk; // 单个吃瓜有多少乱斗值
            int num = static_cast<int>((matchVotes-myVotes+melon)/melon);
            sendGift(20004, num);
            showLocalNotify("[偷塔] " + snum(matchVotes-myVotes+1) + "，赠送 " + snum(num) + " 个吃瓜");
            pkVoting += melon * num; // 增加吃瓜的votes，抵消反偷塔机制中的网络延迟
            qDebug() << "大乱斗赠送" << num << "个吃瓜：" << myVotes << "vs" << matchVotes;
            toutaCount++;
            chiguaCount += num;
            saveTouta();
        }
        else
        {
            QString text = QString("大乱斗尾声：%1 vs %2")
                    .arg(myVotes).arg(matchVotes);
            if (!pkRoomId.isEmpty())
            {
                int totalCount = danmakuCounts->value("pk/" + pkRoomId, 0).toInt();
                int toutaCount = danmakuCounts->value("touta/" + pkRoomId, 0).toInt();
                if (totalCount > 1) // 开始的时候就已经+1了
                    text += QString("  偷塔概率:%1/%2")
                                    .arg(toutaCount).arg(totalCount);
            }

            showLocalNotify(text);
        }
    });

    pkTimer->start();
    if (danmakuWindow)
    {
        danmakuWindow->showStatusText();
        danmakuWindow->setToolTip(pkUname);
        danmakuWindow->setPkStatus(1);
    }
    qint64 pkid = static_cast<qint64>(json.value("pk_id").toDouble());
    qDebug() << "开启大乱斗, id =" << pkid << "  room=" << pkRoomId << "  user=" << pkUid << "   battle_type=" << battle_type;

    // 1605757123 1605757123 1605757433 时间测试
    // qDebug() << QDateTime::currentSecsSinceEpoch() << startTime << endTime;

    // 保存PK信息
    int pkCount = danmakuCounts->value("pk/" + pkRoomId, 0).toInt();
    danmakuCounts->setValue("pk/" + pkRoomId, pkCount+1);

    // PK提示
    QString text = "开启大乱斗：" + pkUname;
    if (pkCount)
        text += "  PK过" + QString::number(pkCount) + "次";
    showLocalNotify(text, pkUid.toLongLong());

    if (pkChuanmenEnable /*&& battle_type == 2*/)
    {
        connectPkRoom();
    }
    ui->actionShow_PK_Video->setEnabled(true);
}

void MainWindow::pkProcess(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_PROCESS",
        "data": {
            "battle_type": 1,
            "init_info": {
                "best_uname": "我今天超可爱0",
                "room_id": 22532956,
                "votes": 132
            },
            "match_info": {
                "best_uname": "银河的偶尔限定女友粉",
                "room_id": 21398069,
                "votes": 156
            }
        },
        "pk_id": 100729411,
        "pk_status": 201,
        "timestamp": 1605749908
    }*/
    QJsonObject data = json.value("data").toObject();
    int prevMyVotes = myVotes;
    int prevMatchVotes = matchVotes;
    if (snum(static_cast<qint64>(data.value("init_info").toObject().value("room_id").toDouble())) == roomId)
    {
        myVotes = data.value("init_info").toObject().value("votes").toInt();
        matchVotes = data.value("match_info").toObject().value("votes").toInt();
    }
    else
    {
        myVotes = data.value("match_info").toObject().value("votes").toInt();
        matchVotes = data.value("init_info").toObject().value("votes").toInt();
    }

    if (!pkTimer->isActive())
        pkTimer->start();

    if (pkEnding)
    {
        qDebug() << "大乱斗进度(偷塔阶段)：" << myVotes << matchVotes << "   等待送到：" << pkVoting;
        // 反偷塔，防止对方也在最后几秒刷礼物
        if (ui->pkAutoMelonCheck->isChecked()
                && myVotes + pkVoting <= matchVotes && myVotes + pkVoting + pkMaxGold/goldTransPk > matchVotes
                && !oppositeTouta) // 对面之前未偷塔（可能是连刷，这时候几个吃瓜偷塔没用）
        {
            // 调用送礼
            int melon = 100 / goldTransPk; // 单个吃瓜有多少乱斗值
            int num = static_cast<int>((matchVotes-myVotes-pkVoting+melon)/melon);
            sendGift(20004, num);
            showLocalNotify("[反偷塔] " + snum(matchVotes-myVotes-pkVoting+1) + "，赠送 " + snum(num) + " 个吃瓜");
            pkVoting += melon * num;
            qDebug() << "大乱斗再次赠送" << num << "个吃瓜：" << myVotes << "vs" << matchVotes;
            toutaCount++;
            chiguaCount += num;
            saveTouta();
        }
        // 显示偷塔情况
        if (prevMyVotes < myVotes)
        {
            showLocalNotify("[己方偷塔] + " + snum(myVotes - prevMyVotes));
        }
        if (prevMatchVotes < matchVotes)
        {
            if (!oppositeTouta)
                oppositeTouta = true;
            showLocalNotify("[对方偷塔] + " + snum(matchVotes - prevMatchVotes));
        }
    }
    else
    {
        qDebug() << "大乱斗进度：" << myVotes << matchVotes;
    }
}

void MainWindow::pkEnd(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_END",
        "data": {
            "battle_type": 1,
            "init_info": {
                "best_uname": "我今天超可爱0",
                "room_id": 22532956,
                "votes": 10,
                "winner_type": 2
            },
            "match_info": {
                "best_uname": "",
                "room_id": 22195813,
                "votes": 0,
                "winner_type": -1
            },
            "timer": 10
        },
        "pk_id": "100729259",
        "pk_status": 401,
        "timestamp": 1605748006
    }*/
    // winner_type: 2赢，-1输，两边2平局

    QJsonObject data = json.value("data").toObject();
    pkToLive = QDateTime::currentSecsSinceEpoch();
    int winnerType1 = data.value("init_info").toObject().value("winner_type").toInt();
    int winnerType2 = data.value("match_info").toObject().value("winner_type").toInt();
    qint64 thisRoomId = static_cast<qint64>(data.value("init_info").toObject().value("room_id").toDouble());
    if (pkTimer)
        pkTimer->stop();
    if (danmakuWindow)
    {
        danmakuWindow->hideStatusText();
        danmakuWindow->setToolTip("");
        danmakuWindow->setPkStatus(0);
    }
    bool ping = winnerType1 == winnerType2;
    bool result = (winnerType1 > 0 && snum(thisRoomId) == roomId)
            || (winnerType1 < 0 && snum(thisRoomId) != roomId);
    if (snum(thisRoomId) == roomId)
    {
        myVotes = data.value("init_info").toObject().value("votes").toInt();
        matchVotes = data.value("match_info").toObject().value("votes").toInt();
    }
    else
    {
        matchVotes = data.value("init_info").toObject().value("votes").toInt();
        myVotes = data.value("match_info").toObject().value("votes").toInt();
    }

    showLocalNotify(QString("大乱斗结果：%1，积分：%2 vs %3")
                                     .arg(ping ? "平局" : (result ? "胜利" : "失败"))
                                     .arg(myVotes)
                                     .arg(matchVotes));
    myVotes = 0;
    matchVotes = 0;
    qDebug() << "大乱斗结束，结果：" << (ping ? "平局" : (result ? "胜利" : "失败"));
    QTimer::singleShot(60000, [=]{
        if (pking) // 下一把PK，已经清空了
            return ;
        cmAudience.clear();
    });

    // 保存对面偷塔次数
    if (oppositeTouta && !pkUname.isEmpty())
    {
        int count = danmakuCounts->value("touta/" + pkRoomId, 0).toInt();
        danmakuCounts->setValue("touta/" + pkRoomId, count+1);
    }

    // 清空大乱斗数据
    pking = false;
    pkEnding = false;
    pkVoting = 0;
    pkEndTime = 0;
    pkUname = "";
    pkUid = "";
    pkRoomId = "";
    myAudience.clear();
    oppositeAudience.clear();
    pkVideo = false;
    ui->actionShow_PK_Video->setEnabled(false);

    if (pkSocket)
    {
        try {
            if (pkSocket->state() == QAbstractSocket::ConnectedState)
                pkSocket->close(); // 会自动deleterLater
            // pkSocket->deleteLater();
        } catch (...) {
            qDebug() << "delete pkSocket failed";
        }
        pkSocket = nullptr;
    }
}

void MainWindow::getRoomCurrentAudiences(QString roomId, QSet<qint64> &audiences)
{
    QString url = "https://api.live.bilibili.com/ajax/msg";
    QStringList param{"roomid", roomId};
    connect(new NetUtil(url, param), &NetUtil::finished, this, [&](QString result){
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "pullLiveDanmaku.ERROR:" << error.errorString();
            qDebug() << result;
            return ;
        }
        QJsonObject json = document.object();
        QJsonArray danmakus = json.value("data").toObject().value("room").toArray();
        audiences.clear();
//        qDebug() << "初始化房间" << roomId << "观众：";
        for (int i = 0; i < danmakus.size(); i++)
        {
            LiveDanmaku danmaku = LiveDanmaku::fromDanmakuJson(danmakus.at(i).toObject());
//            if (!audiences.contains(danmaku.getUid()))
//                qDebug() << "    添加观众：" << danmaku.getNickname();
            audiences.insert(danmaku.getUid());
        }
    });
}

void MainWindow::connectPkRoom()
{
    if (pkRoomId.isEmpty())
        return ;

    // 根据弹幕消息
    getRoomCurrentAudiences(roomId, myAudience);
    getRoomCurrentAudiences(pkRoomId, oppositeAudience);

    // 额外保存的许多本地弹幕消息
    for (int i = 0; i < roomDanmakus.size(); i++)
    {
        qint64 uid = roomDanmakus.at(i).getUid();
        if (uid)
            myAudience.insert(uid);
    }

    // 保存自己主播、对面主播（带头串门？？？）
    myAudience.insert(upUid.toLongLong());
    oppositeAudience.insert(pkUid.toLongLong());

    // 连接socket
    if (pkSocket)
        pkSocket->deleteLater();

    pkSocket = new QWebSocket();

    connect(pkSocket, &QWebSocket::connected, this, [=]{
        SOCKET_DEB << "pkSocket connected";
        // 5秒内发送认证包
        sendVeriPacket(pkSocket, pkRoomId, pkToken);
    });

    connect(pkSocket, &QWebSocket::disconnected, this, [=]{
        // 正在直播的时候突然断开了
        if (liveStatus && pkSocket)
        {
            pkSocket->deleteLater();
            pkSocket = nullptr;
            return ;
        }
    });

    connect(pkSocket, &QWebSocket::binaryMessageReceived, this, [=](const QByteArray &message){
        slotPkBinaryMessageReceived(message);
    });

    // ========== 开始连接 ==========
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getDanmuInfo";
    url += "?id="+pkRoomId+"&type=0";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray dataBa = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(dataBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "连接PK房间出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << s8("pk返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonObject data = json.value("data").toObject();
        pkToken = data.value("token").toString();
        QJsonArray hostArray = data.value("host_list").toArray();
        QString link = hostArray.first().toObject().value("host").toString();
        int port = hostArray.first().toObject().value("wss_port").toInt();
        QString host = QString("wss://%1:%2/sub").arg(link).arg(port);
        SOCKET_DEB << "pk.socket.host:" << host;

        // ========== 连接Socket ==========
        QSslConfiguration config = pkSocket->sslConfiguration();
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        config.setProtocol(QSsl::TlsV1SslV3);
        pkSocket->setSslConfiguration(config);
        pkSocket->open(host);
    });
    manager->get(*request);
}

void MainWindow::slotPkBinaryMessageReceived(const QByteArray &message)
{
    int operation = ((uchar)message[8] << 24)
            + ((uchar)message[9] << 16)
            + ((uchar)message[10] << 8)
            + ((uchar)message[11]);
    QByteArray body = message.right(message.length() - 16);
    SOCKET_DEB << "pk操作码=" << operation << "  正文=" << (body.left(35)) << "...";

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(body, &error);
    QJsonObject json;
    if (error.error == QJsonParseError::NoError)
        json = document.object();

    if (operation == SEND_MSG_REPLY) // 普通包
    {
        QString cmd;
        if (!json.isEmpty())
        {
            cmd = json.value("cmd").toString();
            qDebug() << "pk普通CMD：" << cmd;
            SOCKET_INF << json;
        }

        if (cmd == "NOTICE_MSG") // 全站广播（不用管）
        {
        }
        else if (cmd == "ROOM_RANK")
        {
        }
        else // 压缩弹幕消息
        {
            short protover = (message[6]<<8) + message[7];
            SOCKET_INF << "pk协议版本：" << protover;
            if (protover == 2) // 默认协议版本，zlib解压
            {
                uncompressPkBytes(body);
            }
        }
    }

//    delete[] body.data();
//    delete[] message.data();
    SOCKET_DEB << "PkSocket消息处理结束";
}

void MainWindow::showWidget(QSystemTrayIcon::ActivationReason reason)
{
    switch(reason)
    {
    case QSystemTrayIcon::Trigger:
        if (!this->isHidden())
            this->hide();
        else
            this->showNormal();
        break;
    case QSystemTrayIcon::MiddleClick:
        on_actionShow_Live_Danmaku_triggered();
        break;
    default:
        break;
    }
}

void MainWindow::uncompressPkBytes(const QByteArray &body)
{
    QByteArray unc = zlibToQtUncompr(body.data(), body.size()+1);
    int offset = 0;
    short headerSize = 16;
    while (offset < unc.size() - headerSize)
    {
        int packSize = ((uchar)unc[offset+0] << 24)
                + ((uchar)unc[offset+1] << 16)
                + ((uchar)unc[offset+2] << 8)
                + (uchar)unc[offset+3];
        QByteArray jsonBa = unc.mid(offset + headerSize, packSize - headerSize);
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(jsonBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << s8("pk解析解压后的JSON出错：") << error.errorString();
            qDebug() << s8("pk包数值：") << offset << packSize << "  解压后大小：" << unc.size();
            qDebug() << s8(">>pk当前JSON") << jsonBa;
            qDebug() << s8(">>pk解压正文") << unc;
            return ;
        }
        QJsonObject json = document.object();
        QString cmd = json.value("cmd").toString();
        SOCKET_INF << "pk解压后获取到CMD：" << cmd;
        if (cmd != "ROOM_BANNER" && cmd != "ACTIVITY_BANNER_UPDATE_V2" && cmd != "PANEL"
                && cmd != "ONLINERANK")
            SOCKET_INF << "pk单个JSON消息：" << offset << packSize << QString(jsonBa);
        handlePkMessage(json);

        offset += packSize;
    }
}

void MainWindow::handlePkMessage(QJsonObject json)
{
    QString cmd = json.value("cmd").toString();
    qDebug() << s8(">pk消息命令：") << cmd;
    if (cmd == "DANMU_MSG") // 收到弹幕
    {
        if (!pkMsgSync || !pkVideo)
            return ;
        QJsonArray info = json.value("info").toArray();
        QJsonArray array = info[0].toArray();
        qint64 textColor = array[3].toInt(); // 弹幕颜色
        qint64 timestamp = static_cast<qint64>(array[4].toDouble()); // 13位
        QString msg = info[1].toString();
        QJsonArray user = info[2].toArray();
        qint64 uid = static_cast<qint64>(user[0].toDouble());
        QString username = user[1].toString();
        QString unameColor = user[7].toString();
        int level = info[4].toArray()[0].toInt();
        QJsonArray medal = info[3].toArray();
        int medal_level = 0;

        bool toView = pking &&
                ((!oppositeAudience.contains(uid) && myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() && medal.size() >= 4 &&
                     snum(static_cast<qint64>(medal[3].toDouble())) == roomId));

        // !弹幕的时间戳是13位，其他的是10位！
        qDebug() << s8("pk接收到弹幕：") << username << msg << QDateTime::fromMSecsSinceEpoch(timestamp);

        // 添加到列表
        QString cs = QString::number(textColor, 16);
        while (cs.size() < 6)
            cs = "0" + cs;
        LiveDanmaku danmaku(username, msg, uid, level, QDateTime::fromMSecsSinceEpoch(timestamp),
                                                 unameColor, "#"+cs);
        if (medal.size() >= 4)
        {
            medal_level = medal[0].toInt();
            danmaku.setMedal(snum(static_cast<qint64>(medal[3].toDouble())),
                    medal[1].toString(), medal_level, medal[2].toString());
        }
        if (noReplyMsgs.contains(msg))
        {
            danmaku.setNoReply();
            noReplyMsgs.clear();
        }
        else
            minuteDanmuPopular++;
        danmaku.setToView(toView);
        danmaku.setPkLink(true);
        appendNewLiveDanmaku(danmaku);
    }
    else if (cmd == "SEND_GIFT") // 有人送礼
    {
        /*QJsonObject data = json.value("data").toObject();
        QString giftName = data.value("giftName").toString();
        QString username = data.value("uname").toString();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        int num = data.value("num").toInt();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble());
        QString coinType = data.value("coin_type").toString();
        int totalCoin = data.value("total_coin").toInt();

        qDebug() << s8("pk接收到送礼：") << username << giftName << num << s8("  总价值：") << totalCoin << coinType;
        QString localName = getLocalNickname(uid);
        LiveDanmaku danmaku(username, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);
        danmaku.setPkLink(true);
        appendNewLiveDanmaku(danmaku);*/
    }
    else if (cmd == "INTERACT_WORD")
    {
        if (!pkChuanmenEnable) // 可能是中途关了
            return ;
        QJsonObject data = json.value("data").toObject();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("uname").toString();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble());
        bool isadmin = data.value("isadmin").toBool();
        QString unameColor = data.value("uname_color").toString();
        bool isSpread = data.value("is_spread").toBool();
        QString spreadDesc = data.value("spread_desc").toString();
        QString spreadInfo = data.value("spread_info").toString();
        QJsonObject fansMedal = data.value("fans_medal").toObject();
        bool toView = pking &&
                ((!oppositeAudience.contains(uid) && myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() &&
                     snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())) == roomId));
        if (!toView) // 不是自己方过去串门的
            return ;
        if (!cmAudience.contains(uid))
            cmAudience.insert(uid, 1);
        showLocalNotify(username + " 跑去对面串门", uid); // 显示一个短通知，就不作为一个弹幕了

        /*qDebug() << s8("pk观众进入：") << username;
        if (isSpread)
            qDebug() << s8("    pk来源：") << spreadDesc;
        QString localName = getLocalNickname(uid);
        LiveDanmaku danmaku(username, uid, QDateTime::fromSecsSinceEpoch(timestamp), isadmin,
                            unameColor, spreadDesc, spreadInfo);
        danmaku.setMedal(snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())),
                         fansMedal.value("medal_name").toString(),
                         fansMedal.value("medal_level").toInt(),
                         QString("#%1").arg(fansMedal.value("medal_color").toInt(), 6, 16, QLatin1Char('0')),
                         "");
        danmaku.setToView(toView);
        danmaku.setPkLink(true);
        appendNewLiveDanmaku(danmaku);*/
    }
}

void MainWindow::releaseLiveData()
{
    ui->roomRankLabel->setText("");
    ui->roomRankLabel->setToolTip("");

    pking = false;
    pkUid = "";
    pkUname = "";
    pkRoomId = "";
    pkEnding = false;
    pkVoting = 0;
    pkVideo = false;
    myAudience.clear();
    oppositeAudience.clear();
    fansList.clear();

    danmuPopularQueue.clear();
    minuteDanmuPopular = 0;
    danmuPopularValue = 0;

    diangeHistory.clear();
    ui->diangeHistoryListWidget->clear();

    statusLabel->setText("");
    rankLabel->setText("");
    rankLabel->setToolTip("");
    fansLabel->setText("");

    if (danmakuWindow)
    {
        danmakuWindow->hideStatusText();
        danmakuWindow->setUpUid(0);
        danmakuWindow->releaseLiveData();
    }

    if (pkSocket)
    {
        if (pkSocket->state() == QAbstractSocket::ConnectedState)
            pkSocket->close();
        pkSocket = nullptr;
    }

    finishLiveRecord();
}

QRect MainWindow::getScreenRect()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenRect = screen->availableVirtualGeometry();
    return screenRect;
}

QPixmap MainWindow::toRoundedPixmap(QPixmap pixmap, int radius) const
{
    QPixmap tmp(pixmap.size());
    tmp.fill(Qt::transparent);
    QPainter painter(&tmp);
    painter.setRenderHints(QPainter::Antialiasing, true);
    painter.setRenderHints(QPainter::SmoothPixmapTransform, true);
    QPainterPath path;
    path.addRoundedRect(0, 0, tmp.width(), tmp.height(), radius, radius);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, tmp.width(), tmp.height(), pixmap);
    return tmp;
}

void MainWindow::switchMedalTo(qint64 targetRoomId)
{
    QString url = "https://api.live.bilibili.com/fans_medal/v1/FansMedal/get_list_in_room";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        SOCKET_INF << QString(data);
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "获取用户信息出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取用户信息
        QJsonArray medals = json.value("data").toArray();
        /*  buff_msg: "(舰长buff：上限提升至150%)"
            can_delete: false     // 变灰了才能删除
            day_limit: 250000
            guard_level: 3
            guard_type: 3
            icon_code: 2
            icon_text: "最近获得"
            intimacy: 1380
            is_lighted: 1         // 是否未变灰
            is_receive: 1
            last_wear_time: 1613491199
            level: 21
            live_stream_status: 0  // 是否正在直播
            lpl_status: 0
            master_available: 1
            master_status: 0
            medal_color: 1725515
            medal_color_border: 6809855
            medal_color_end: 5414290
            medal_color_start: 1725515
            medal_id: 373753
            medal_level: 21
            medal_name: "181mm"
            next_intimacy: 2000
            rank: "-"
            receive_channel: 4
            receive_time: "2021-01-10 09:33:22"
            room_id: 11584296      // 牌子房间
            score: 50001380
            source: 1
            status: 1              // 是否佩戴中
            sup_code: 2
            sup_text: "最近获得"
            target_face: ""
            target_id: 20285041
            target_name: "懒一夕智能科技"
            today_feed: 0
            today_intimacy: 0
            uid: 20285041          // 牌子用户
        */

        foreach (QJsonValue val, medals)
        {
            QJsonObject medal = val.toObject();
            qint64 roomId = static_cast<qint64>(medal.value("room_id").toDouble());
            int status = medal.value("status").toInt(); // 1佩戴，0未佩戴

            if (roomId == targetRoomId)
            {
                if (status) // 已佩戴，就不用管了
                    return ;

                // 佩带牌子
                /*int isLighted = medal.value("is_lighted").toBool(); // 1能用，0变灰
                if (!isLighted) // 牌子是灰的，可以不用管，发个弹幕就点亮了
                    return ;
                */

                qint64 medalId = static_cast<qint64>(medal.value("medal_id").toDouble());
                wearMedal(medalId);
                return ;
            }
        }
        qDebug() << "未检测到粉丝勋章，无法自动切换";

    });
    manager->get(*request);
}

void MainWindow::wearMedal(qint64 medalId)
{
    QUrl url("https://api.live.bilibili.com/xlive/web-room/v1/fansMedal/wear");

    // 建立对象
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");

    // 设置数据（JSON的ByteArray）
    QStringList datas;
    datas << "medal_id=" + QString::number(medalId);
    datas << "csrf_token=" + csrf_token;
    datas << "csrf=" + csrf_token;
    datas << "visit_id=";

    QByteArray ba(datas.join("&").toStdString().data());

    // 连接槽
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "设置Medal出错：" << error.errorString();
            return ;
        }
        QJsonObject object = document.object();
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }
        qDebug() << "佩戴主播粉丝勋章成功";
    });

    manager->post(*request, ba);
}

/**
 * 自动签到
 */
void MainWindow::doSign()
{
    if (csrf_token.isEmpty())
    {
        ui->autoDoSignCheck->setText("未设置Cookie");
        QTimer::singleShot(10000, [=]{
            ui->autoDoSignCheck->setText("每日自动签到");
        });
        return ;
    }

    QUrl url("https://api.live.bilibili.com/xlive/web-ucenter/v1/sign/DoSign");

    // 建立对象
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");

    // 连接槽
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "签到出错：" << error.errorString();
            return ;
        }
        QJsonObject object = document.object();
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            QString msg = json.value("message").toString();
            qDebug() << s8("返回结果不为0：") << msg;
            ui->autoDoSignCheck->setText(msg);
        }
        else
        {
            ui->autoDoSignCheck->setText("签到成功");
            ui->autoDoSignCheck->setToolTip("最近签到时间：" + QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss"));
        }
        QTimer::singleShot(10000, [=]{
            ui->autoDoSignCheck->setText("每日自动签到");
        });
    });

    manager->get(*request);
}

void MainWindow::joinLOT(qint64 id, bool follow)
{
    if (!id )
        return ;
    if (csrf_token.isEmpty())
    {
        ui->autoDoSignCheck->setText("未设置Cookie");
        QTimer::singleShot(10000, [=]{
            ui->autoDoSignCheck->setText("自动参与天选");
        });
        return ;
    }

    QUrl url("https://api.live.bilibili.com/xlive/lottery-interface/v1/Anchor/Join"
             "?id="+QString::number(id)+(follow?"&follow=true":"")+"&platform=pc&csrf_token="+csrf_token+"&csrf="+csrf_token+"&visit_id=");
    qDebug() << "参与天选：" << id << follow << url;

    // 建立对象
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::CookieHeader, getCookies());
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");

    // 连接槽
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "参加天选出错：" << error.errorString();
            qDebug() << QString(data);
            return ;
        }
        QJsonObject object = document.object();
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            QString msg = json.value("message").toString();
            qDebug() << s8("返回结果不为0：") << msg;
            ui->autoLOTCheck->setText(msg);
        }
        else
        {
            ui->autoLOTCheck->setText("参与成功");
            qDebug() << "参与天选成功！";
            ui->autoDoSignCheck->setToolTip("最近参与时间：" + QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss"));
        }
        QTimer::singleShot(10000, [=]{
            ui->autoLOTCheck->setText("自动参与天选");
        });
    });

    manager->post(*request, QByteArray());
}

void MainWindow::on_actionMany_Robots_triggered()
{
    if (!hostList.size()) // 未连接
        return ;

    HostInfo hostServer = hostList.at(0);
    QString host = QString("wss://%1:%2/sub").arg(hostServer.host).arg(hostServer.wss_port);

    QSslConfiguration config = this->socket->sslConfiguration();
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    config.setProtocol(QSsl::TlsV1SslV3);

    for (int i = 0; i < 1000; i++)
    {
        QWebSocket* socket = new QWebSocket();
        connect(socket, &QWebSocket::connected, this, [=]{
            qDebug() << "rSocket" << i << "connected";
            // 5秒内发送认证包
            sendVeriPacket(socket, pkRoomId, pkToken);
        });
        connect(socket, &QWebSocket::disconnected, this, [=]{
            robots_sockets.removeOne(socket);
            socket->deleteLater();
        });
        socket->setSslConfiguration(config);
        socket->open(host);
        robots_sockets.append(socket);
    }
}

void MainWindow::on_judgeRobotCheck_clicked()
{
    judgeRobot = ui->judgeRobotCheck->isChecked();
    settings.setValue("danmaku/judgeRobot", judgeRobot);
}

void MainWindow::on_actionAdd_Room_To_List_triggered()
{
    if (roomId.isEmpty())
        return ;

    QStringList list = settings.value("custom/rooms", "").toString().split(";", QString::SkipEmptyParts);
    for (int i = 0; i < list.size(); i++)
    {
        QStringList texts = list.at(i).split(",", QString::SkipEmptyParts);
        if (texts.size() < 1)
            continue ;
        QString id = texts.first();
        QString name = texts.size() >= 2 ? texts.at(1) : id;
        if (id == roomId) // 找到这个
        {
            ui->menu_3->removeAction(ui->menu_3->actions().at(ui->menu_3->actions().size() - (list.size() - i)));
            list.removeAt(i);
            settings.setValue("custom/rooms", list.join(";"));
            return ;
        }
    }

    QString id = roomId; // 不能用成员变量，否则无效（lambda中值会变，自己试试就知道了）
    QString name = upName;

    list.append(id + "," + name.replace(";","").replace(",",""));
    settings.setValue("custom/rooms", list.join(";"));

    QAction* action = new QAction(name, this);
    ui->menu_3->addAction(action);
    connect(action, &QAction::triggered, this, [=]{
        ui->roomIdEdit->setText(id);
        on_roomIdEdit_editingFinished();
    });
}

void MainWindow::on_recordCheck_clicked()
{
    bool check = ui->recordCheck->isChecked();
    settings.setValue("danmaku/record", check);

    if (check)
    {
        if (!roomId.isEmpty() && liveStatus)
            startLiveRecord();
    }
    else
        finishLiveRecord();
}

void MainWindow::on_recordSplitSpin_valueChanged(int arg1)
{
    settings.setValue("danmaku/recordSplit", arg1);
    if (recordTimer)
        recordTimer->setInterval(arg1 * 60000);
}

void MainWindow::on_sendWelcomeTextCheck_clicked()
{
    settings.setValue("danmaku/sendWelcomeText", ui->sendWelcomeTextCheck->isChecked());
}

void MainWindow::on_sendWelcomeVoiceCheck_clicked()
{
    settings.setValue("danmaku/sendWelcomeVoice", ui->sendWelcomeVoiceCheck->isChecked());
    if (!tts && ui->sendWelcomeVoiceCheck->isChecked())
        tts = new QTextToSpeech(this);
}

void MainWindow::on_sendGiftTextCheck_clicked()
{
    settings.setValue("danmaku/sendGiftText", ui->sendGiftTextCheck->isChecked());
}

void MainWindow::on_sendGiftVoiceCheck_clicked()
{
    settings.setValue("danmaku/sendGiftVoice", ui->sendGiftVoiceCheck->isChecked());
    if (!tts && ui->sendGiftVoiceCheck->isChecked())
        tts = new QTextToSpeech(this);
}

void MainWindow::on_sendAttentionTextCheck_clicked()
{
    settings.setValue("danmaku/sendAttentionText", ui->sendAttentionTextCheck->isChecked());
}

void MainWindow::on_sendAttentionVoiceCheck_clicked()
{
    settings.setValue("danmaku/sendAttentionVoice", ui->sendAttentionVoiceCheck->isChecked());
    if (!tts && ui->sendAttentionVoiceCheck->isChecked())
        tts = new QTextToSpeech(this);
}

void MainWindow::on_enableScreenDanmakuCheck_clicked()
{
    settings.setValue("screendanmaku/enableDanmaku", ui->enableScreenDanmakuCheck->isChecked());
    ui->enableScreenMsgCheck->setEnabled(ui->enableScreenDanmakuCheck->isChecked());
}

void MainWindow::on_enableScreenMsgCheck_clicked()
{
    settings.setValue("screendanmaku/enableMsg", ui->enableScreenMsgCheck->isChecked());
}

void MainWindow::on_screenDanmakuLeftSpin_valueChanged(int arg1)
{
    settings.setValue("screendanmaku/left", arg1);
}

void MainWindow::on_screenDanmakuRightSpin_valueChanged(int arg1)
{
    settings.setValue("screendanmaku/right", arg1);
}

void MainWindow::on_screenDanmakuTopSpin_valueChanged(int arg1)
{
    settings.setValue("screendanmaku/top", arg1);
}

void MainWindow::on_screenDanmakuBottomSpin_valueChanged(int arg1)
{
    settings.setValue("screendanmaku/bottom", arg1);
}

void MainWindow::on_screenDanmakuSpeedSpin_valueChanged(int arg1)
{
    settings.setValue("screendanmaku/speed", arg1);
}

void MainWindow::on_screenDanmakuFontButton_clicked()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok,this);
    if (!ok)
        return ;
    this->screenDanmakuFont = font;
    this->setFont(font);
    settings.setValue("screendanmaku/font", screenDanmakuFont.toString());
}

void MainWindow::on_screenDanmakuColorButton_clicked()
{
    QColor c = QColorDialog::getColor(screenDanmakuColor, this, "选择昵称颜色", QColorDialog::ShowAlphaChannel);
    if (!c.isValid())
        return ;
    if (c != screenDanmakuColor)
    {
        settings.setValue("screendanmaku/color", screenDanmakuColor = c);
    }
}

void MainWindow::on_autoSpeekDanmakuCheck_clicked()
{
    settings.setValue("danmaku/autoSpeek", ui->autoSpeekDanmakuCheck->isChecked());
    if (!tts && ui->autoSpeekDanmakuCheck->isChecked())
        tts = new QTextToSpeech(this);
}

void MainWindow::on_diangeFormatEdit_textEdited(const QString &text)
{
    diangeFormatString = text;
    settings.setValue("danmaku/diangeFormat", diangeFormatString);
}

void MainWindow::on_diangeNeedMedalCheck_clicked()
{
    settings.setValue("danmaku/diangeNeedMedal", ui->diangeNeedMedalCheck->isChecked());
}

void MainWindow::on_showOrderPlayerButton_clicked()
{
    on_actionShow_Order_Player_Window_triggered();
}

void MainWindow::on_diangeShuaCheck_clicked()
{
    settings.setValue("danmaku/diangeShua", ui->diangeShuaCheck->isChecked());
}

void MainWindow::on_pkMelonValButton_clicked()
{
    bool ok = false;
    int val = QInputDialog::getInt(this, "乱斗值比例", "1乱斗值等于多少金瓜子？10或100？", goldTransPk, 1, 100, 1, &ok);
    if (!ok)
        return ;
    goldTransPk = val;
    settings.setValue("pk/goldTransPk", goldTransPk);
}

/**
 * 机器人开始工作
 * 开播时连接/连接后开播
 */
void MainWindow::slotStartWork()
{
    // 自动更换勋章
    if (ui->autoSwitchMedalCheck->isChecked())
    {
        switchMedalTo(roomId.toLongLong());
    }
}

void MainWindow::on_autoSwitchMedalCheck_clicked()
{
    settings.setValue("danmaku/autoSwitchMedal", ui->autoSwitchMedalCheck->isChecked());
    if (!roomId.isEmpty() && liveStatus)
    {
        switchMedalTo(roomId.toLongLong());
    }
}

void MainWindow::on_sendAutoOnlyLiveCheck_clicked()
{
    settings.setValue("danmaku/sendAutoOnlyLive", ui->sendAutoOnlyLiveCheck->isChecked());
}

void MainWindow::on_autoDoSignCheck_clicked()
{
    settings.setValue("danmaku/autoDoSign", ui->autoDoSignCheck->isChecked());
    if (ui->autoDoSignCheck->isChecked())
    {
        doSign();
        doSignTimer->start();
    }
    else
        doSignTimer->stop();
}

void MainWindow::on_actionRoom_Status_triggered()
{
    RoomStatusDialog* rsd = new RoomStatusDialog(settings, nullptr);
    rsd->show();
}

void MainWindow::on_autoLOTCheck_clicked()
{
    settings.setValue("danmaku/autoLOT", ui->autoLOTCheck->isChecked());
}
