#ifndef XFYTTS_H
#define XFYTTS_H

#include <QObject>
#include <QCryptographicHash>
#include <QtWebSockets/QWebSocket>
#include <QAuthenticator>
#include <QtConcurrent/QtConcurrent>
#include <QAudioFormat>
#include <QAudioOutput>

#define AUTH_DEB if (0) qDebug()

class XfyTTS : public QObject
{
    Q_OBJECT
public:
    XfyTTS(QString APPID, QString APIKey, QString APISecret, QObject* parent = nullptr);

    void speakText(QString text);
    void playFile(QString filePath, bool deleteAfterPlay = false);

private:
    void startConnect();
    QString getAuthorization() const;
    QString getSignature() const;
    QString getDate() const;

    void sendText(QString text);
    void generalAudio();

signals:
    void signalTTSPrepared(QString filePath);

private:
    QString APPID;
    QString APIKey;
    QString APISecret;

    QString savedDir;
    QWebSocket* socket = nullptr;
    QString hostUrl = "wss://tts-api.xfyun.cn/v2/tts";

    QString vcn = "xiaoyan"; // 发音人
    int pitch = 50; // 音调
    int speed = 50; // 音速

    QStringList speakQueue;
    QByteArray receivedBytes;
};

#endif // XFYTTS_H
