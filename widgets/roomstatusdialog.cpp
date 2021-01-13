#include <QInputDialog>
#include <QMenu>
#include <QAction>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonParseError>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QDesktopServices>
#include "roomstatusdialog.h"
#include "ui_roomstatusdialog.h"

RoomStatusDialog::RoomStatusDialog(QSettings &settings, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::RoomStatusDialog),
    settings(settings)
{
    ui->setupUi(this);
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    int count = settings.value("rooms/count", 0).toInt();
    ui->roomsTable->setRowCount(count);
    for (int i = 0; i < count; i++)
    {
        QString id = settings.value("rooms/r" + QString::number(i)).toString();
        roomIds.append(id);
        ui->roomsTable->setItem(i, 0, new QTableWidgetItem(id));
    }

    ui->roomsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    on_refreshStatusButton_clicked();
}

RoomStatusDialog::~RoomStatusDialog()
{
    delete ui;
}

void RoomStatusDialog::on_addRoomButton_clicked()
{
    QString id = QInputDialog::getText(this, "添加房间", "请输入房间ID", QLineEdit::Normal);
    if (id.isEmpty())
        return ;
    if (roomIds.contains(id))
        return ;

    roomIds.append(id);
    ui->roomsTable->setRowCount(roomIds.size());
    ui->roomsTable->setItem(roomIds.size()-1, 0, new QTableWidgetItem(id));
    refreshRoomStatus(id);

    settings.setValue("rooms/count", roomIds.size());
    settings.setValue("rooms/r" + QString::number(roomIds.size()-1), id);
}

void RoomStatusDialog::on_refreshStatusButton_clicked()
{
    for (int i = 0; i < roomIds.size(); i++)
        refreshRoomStatus(roomIds.at(i));
}

void RoomStatusDialog::on_roomsTable_customContextMenuRequested(const QPoint &pos)
{
    int row = ui->roomsTable->currentRow();

    QMenu* menu = new QMenu(this);
    QAction* actionRoom = new QAction("前往直播间", this);
    QAction* actionPage = new QAction("用户主页", this);
    QAction* actionDelete = new QAction("删除", this);

    if (row < 0)
    {
        actionDelete->setEnabled(false);
    }

    menu->addAction(actionRoom);
//    menu->addAction(actionPage);
    menu->addAction(actionDelete);

    connect(actionRoom, &QAction::triggered, this, [=]{
        QDesktopServices::openUrl(QUrl("https://live.bilibili.com/" + roomIds.at(row)));
    });
    connect(actionPage, &QAction::triggered, this, [=]{
        //QDesktopServices::openUrl(QUrl("https://space.bilibili.com/" + uid));
    });

    connect(actionDelete, &QAction::triggered, this, [=]{
        roomIds.removeAt(row);
        ui->roomsTable->removeRow(row);

        settings.setValue("rooms/count", roomIds.size());
        for (int i = row; i < roomIds.size(); i++)
            settings.setValue("rooms/r" + QString::number(roomIds.size()-1), roomIds.at(i));
    });

    menu->exec(QCursor::pos());

    actionDelete->deleteLater();
}

void RoomStatusDialog::refreshRoomStatus(QString roomId)
{
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getInfoByRoom?room_id=" + roomId;
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
            qDebug() << "获取房间信息出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << "返回结果不为0：" << json.value("message").toString();
            return ;
        }

        // 获取房间信息
        QJsonObject dataObj = json.value("data").toObject();
        QJsonObject roomInfo = dataObj.value("room_info").toObject();
//        QString roomId = QString::number(roomInfo.value("room_id").toInt()); // 应当一样
        QString shortId = QString::number(roomInfo.value("short_id").toInt());
        QString upUid = QString::number(static_cast<qint64>(roomInfo.value("uid").toDouble()));
        QString roomTitle = roomInfo.value("title").toString();
        int liveStatus = roomInfo.value("live_status").toInt();
        int pkStatus = roomInfo.value("pk_status").toInt();
        /*qDebug() << "getRoomInfo: roomid=" << roomId
                 << "  shortid=" << shortId
                 << "  uid=" << upUid
                 << "  liveStatus=" << liveStatus
                 << "  pkStatus=" << pkStatus;*/

        QJsonObject anchorInfo = dataObj.value("anchor_info").toObject();
        QString upName = anchorInfo.value("base_info").toObject().value("uname").toString();

        int index = roomIds.indexOf(roomId);
        if (index == -1)
        {
            qDebug() << "未找到对应的ID：" << roomId;
            return ;
        }

        QString liveStr = "";
        if (liveStatus == 1)
            liveStr = "直播中";
        else if (liveStatus == 2)
            liveStr = "轮播中";

        QString pkStr = "";
        if (pkStatus == 1)
            pkStr = "PK中";
        else if (pkStatus == 2)
            pkStr = "视频PK中";

        ui->roomsTable->setItem(index, 1, new QTableWidgetItem(upName));
        ui->roomsTable->setItem(index, 2, new QTableWidgetItem(roomTitle));
        ui->roomsTable->setItem(index, 3, new QTableWidgetItem(liveStr));
        ui->roomsTable->setItem(index, 4, new QTableWidgetItem(pkStr));
    });
    manager->get(*request);
}

void RoomStatusDialog::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_F5)
    {
        on_refreshStatusButton_clicked();
    }
    return QDialog::keyPressEvent(e);
}

void RoomStatusDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);

    restoreGeometry(settings.value("rooms/geometry").toByteArray());
}

void RoomStatusDialog::closeEvent(QCloseEvent *e)
{
    settings.setValue("rooms/geometry", this->saveGeometry());

    QDialog::closeEvent(e);
}
