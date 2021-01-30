#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>
#include "livevideoplayer.h"
#include "ui_livevideoplayer.h"

LiveVideoPlayer::LiveVideoPlayer(QSettings &settings, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LiveVideoPlayer),
    settings(settings)
{
    ui->setupUi(this);
    setModal(false);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

//    videoWidget = new QVideoWidget(this);
//    QVBoxLayout* vLayout = new QVBoxLayout(this);
//    vLayout->addWidget(videoWidget);

    player = new QMediaPlayer(this, QMediaPlayer::VideoSurface);
    connect(player, &QMediaPlayer::positionChanged, this, [=](qint64 position){
//        qDebug() << "position changed:" << position << player->duration();
    });
    player->setVideoOutput(ui->videoWidget);
//    player->setVideoOutput(videoWidget);

    connect(player, &QMediaPlayer::stateChanged, this, [=](QMediaPlayer::State state) {
//        qDebug() << "videoPlayer.stateChanged:" << state << QDateTime::currentDateTime() << playList->currentIndex() << playList->mediaCount();
    });
    connect(this, SIGNAL(signalPlayUrl(QString)), this, SLOT(setPlayUrl(QString)));
}

LiveVideoPlayer::~LiveVideoPlayer()
{
//    delete ui;
}

void LiveVideoPlayer::setRoomId(QString roomId)
{
    this->roomId = roomId;
    refreshPlayUrl();
}

void LiveVideoPlayer::slotLiveStart(QString roomId)
{
    if (this->roomId != roomId)
        return ;
    QTimer::singleShot(500, [=]{
        refreshPlayUrl();
    });
}

void LiveVideoPlayer::setPlayUrl(QString url)
{
    QMediaContent c((QUrl(url)));
    player->setMedia(c);
    if (player->state() == QMediaPlayer::StoppedState)
        player->play();
}

void LiveVideoPlayer::refreshPlayUrl()
{
    if (roomId.isEmpty())
        return ;
    QString url = "http://api.live.bilibili.com/room/v1/Room/playUrl?cid=" + roomId
            + "&quality=4&qn=10000&platform=web&otype=json";
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
            qDebug() << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << ("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取链接
        QJsonArray array = json.value("data").toObject().value("durl").toArray();
        if (!array.size())
        {
            QMessageBox::warning(this, "播放视频", "未找到可用的链接\n" + data);
            return ;
        }
        QString url = array.first().toObject().value("url").toString(); // 第一个链接
        qDebug() << "playUrl:" << url;

        QTimer::singleShot(0, [=]{
            emit signalPlayUrl(url);
        });
    });
    manager->get(*request);
}

void LiveVideoPlayer::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    restoreGeometry(settings.value("videoplayer/geometry").toByteArray());

    if (!player->media().isNull())
    {
        player->play();
    }
}

void LiveVideoPlayer::hideEvent(QHideEvent *e)
{
    QDialog::hideEvent(e);
    settings.setValue("videoplayer/geometry", this->saveGeometry());

    if (player->state() == QMediaPlayer::PlayingState)
    {
        player->pause();
    }
}

void LiveVideoPlayer::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
}
