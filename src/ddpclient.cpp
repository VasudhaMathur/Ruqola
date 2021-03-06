/*

 * Copyright 2016  Riccardo Iaconelli <riccardo@kde.org>
 * Copyright 2017 Laurent Montel <montel@kde.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "ddpclient.h"
#include "utils.h"
#include "ruqola.h"
#include "ruqola_debug.h"
#include "rocketchatmessage.h"
#include "ruqolawebsocket.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

void empty_callback(const QJsonDocument &doc)
{
    Q_UNUSED(doc);
}

DDPClient::DDPClient(QObject *parent)
    : QObject(parent)
    , m_uid(1)
    , m_loginJob(0)
    , m_loginStatus(NotConnected)
    , m_loginType(Password)
    , m_connected(false)
    , m_attemptedPasswordLogin(false)
    , m_attemptedTokenLogin(false)
    , mRocketChatMessage(new RocketChatMessage)
{
}

DDPClient::~DDPClient()
{
    mWebSocket->close();
    delete mWebSocket;
    delete mRocketChatMessage;
}

void DDPClient::setServerUrl(const QString &url)
{
    m_url = url;
}

void DDPClient::setWebSocket(AbstractWebSocket *socket)
{
    delete mWebSocket;
    mWebSocket = socket;
    initializeWebSocket();
}

void DDPClient::initializeWebSocket()
{
    mWebSocket->ignoreSslErrors();
    connect(mWebSocket, &AbstractWebSocket::connected, this, &DDPClient::onWSConnected);
    connect(mWebSocket, &AbstractWebSocket::textMessageReceived, this, &DDPClient::onTextMessageReceived);
    connect(mWebSocket, &AbstractWebSocket::disconnected, this, &DDPClient::onWSclosed);
}

void DDPClient::start()
{
    if (!mWebSocket) {
        mWebSocket = new RuqolaWebSocket(this);
        initializeWebSocket();
    }
    connect(Ruqola::self(), &Ruqola::serverURLChanged, this, &DDPClient::onServerURLChange);

    if (!m_url.isEmpty()) {
        const QUrl serverUrl = adaptUrl(m_url);
        mWebSocket->openUrl(serverUrl);
        qCDebug(RUQOLA_LOG) << "Trying to connect to URL" << serverUrl;
    } else {
        qCDebug(RUQOLA_LOG) << "url is empty";
    }
}

QUrl DDPClient::adaptUrl(const QString &url)
{
    return Utils::generateServerUrl(url);
}

void DDPClient::onServerURLChange()
{
    if (Ruqola::self()->serverURL() != m_url || !mWebSocket->isValid()) {
        if (mWebSocket->isValid()) {
            mWebSocket->flush();
            mWebSocket->close();
        }
        m_url = Ruqola::self()->serverURL();
        mWebSocket->openUrl(adaptUrl(m_url));
        connect(mWebSocket, &AbstractWebSocket::connected, this, &DDPClient::onWSConnected);
        qCDebug(RUQOLA_LOG) << "Reconnecting" << m_url;
    }
}

DDPClient::LoginStatus DDPClient::loginStatus() const
{
    return m_loginStatus;
}

void DDPClient::setLoginStatus(DDPClient::LoginStatus l)
{
    qCDebug(RUQOLA_LOG) << "Setting login status to" << l;
    m_loginStatus = l;
    Q_EMIT loginStatusChanged();

    // reset flags
    if (l == LoginFailed) {
        m_attemptedPasswordLogin = false;
        m_attemptedTokenLogin = false;
    }
}

DDPClient::LoginType DDPClient::loginType() const
{
    return m_loginType;
}

void DDPClient::setLoginType(DDPClient::LoginType t)
{
    if (m_loginType != t) {
        qCDebug(RUQOLA_LOG) << "Setting login type to" << t;
        m_loginType = t;
        Q_EMIT loginTypeChanged();
    }
}

bool DDPClient::isConnected() const
{
    return m_connected;
}

bool DDPClient::isLoggedIn() const
{
    return m_loginStatus == LoggedIn;
}

QString DDPClient::cachePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

QQueue<QPair<QString, QJsonDocument> > DDPClient::messageQueue() const
{
    return m_messageQueue;
}

quint64 DDPClient::leaveRoom(const QString &roomID)
{
    const RocketChatMessage::RocketChatMessageResult result = mRocketChatMessage->leaveRoom(roomID, m_uid);
    return method(result, empty_callback, DDPClient::Persistent);
}

quint64 DDPClient::hideRoom(const QString &roomID)
{
    const RocketChatMessage::RocketChatMessageResult result = mRocketChatMessage->hideRoom(roomID, m_uid);
    return method(result, empty_callback, DDPClient::Persistent);
}

quint64 DDPClient::informTypingStatus(const QString &roomId, bool typing, const QString &userName)
{
    const RocketChatMessage::RocketChatMessageResult result = mRocketChatMessage->informTypingStatus(roomId, userName, typing, m_uid);
    qint64 bytes = mWebSocket->sendTextMessage(result.result);
    if (bytes < result.result.length()) {
        qCDebug(RUQOLA_LOG) << "ERROR! I couldn't send all of my message. This is a bug! (try again)";
    }
    const int value = m_uid;
    m_uid++;
    return value;
}

quint64 DDPClient::method(const RocketChatMessage::RocketChatMessageResult &result, std::function<void(QJsonDocument)> callback, DDPClient::MessageType messageType)
{
    qint64 bytes = mWebSocket->sendTextMessage(result.result);
    if (bytes < result.result.length()) {
        qCDebug(RUQOLA_LOG) << "ERROR! I couldn't send all of my message. This is a bug! (try again)";
        qCDebug(RUQOLA_LOG) << mWebSocket->isValid() << mWebSocket->error() << mWebSocket->requestUrl();

        if (messageType == DDPClient::Persistent) {
            m_messageQueue.enqueue(qMakePair(result.method, result.jsonDocument));
            Ruqola::self()->messageQueue()->processQueue();
        }
    } else {
        qCDebug(RUQOLA_LOG) << "Successfully sent " << result.result;
    }

    m_callbackHash[m_uid] = callback;

    const int value = m_uid;
    m_uid++;
    return value;
}

quint64 DDPClient::method(const QString &m, const QJsonDocument &params, DDPClient::MessageType messageType)
{
    return method(m, params, empty_callback, messageType);
}

quint64 DDPClient::method(const QString &method, const QJsonDocument &params, std::function<void(QJsonDocument)> callback, DDPClient::MessageType messageType)
{
    qDebug() << " params" << params.toJson(QJsonDocument::Indented);
    const RocketChatMessage::RocketChatMessageResult result = mRocketChatMessage->generateMethod(method, params, m_uid);
    qint64 bytes = mWebSocket->sendTextMessage(result.result);
    if (bytes < result.result.length()) {
        qCDebug(RUQOLA_LOG) << "ERROR! I couldn't send all of my message. This is a bug! (try again)";
        qCDebug(RUQOLA_LOG) << mWebSocket->isValid() << mWebSocket->error() << mWebSocket->requestUrl();

        if (messageType == DDPClient::Persistent) {
            m_messageQueue.enqueue(qMakePair(result.method, result.jsonDocument));
            Ruqola::self()->messageQueue()->processQueue();
        }
    } else {
        qCDebug(RUQOLA_LOG) << "Successfully sent " << result.result;
    }

    m_callbackHash[m_uid] = callback;

    const quint64 uidCurrent = m_uid;
    m_uid++;
    return uidCurrent;
}

void DDPClient::subscribe(const QString &collection, const QJsonArray &params)
{
    QJsonObject json;
    json[QStringLiteral("msg")] = QStringLiteral("sub");
    json[QStringLiteral("id")] = QString::number(m_uid);
    json[QStringLiteral("name")] = collection;
    json[QStringLiteral("params")] = params;

    qint64 bytes = mWebSocket->sendTextMessage(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
    if (bytes < json.length()) {
        qCDebug(RUQOLA_LOG) << "ERROR! I couldn't send all of my message. This is a bug! (try again)";
    }
    m_uid++;
}

void DDPClient::onTextMessageReceived(const QString &message)
{
    QJsonDocument response = QJsonDocument::fromJson(message.toUtf8());
    if (!response.isNull() && response.isObject()) {
        QJsonObject root = response.object();

        QString messageType = root.value(QStringLiteral("msg")).toString();

        if (messageType == QLatin1String("updated")) {
            qDebug() << " message updated ! not implemented yet" << root;
        } else if (messageType == QLatin1String("result")) {
            unsigned id = root.value(QStringLiteral("id")).toString().toInt();

            if (m_callbackHash.contains(id)) {
                std::function<void(QJsonDocument)> callback = m_callbackHash.take(id);

                callback(QJsonDocument(root.value(QStringLiteral("result")).toObject()));
            }
            Q_EMIT result(id, QJsonDocument(root.value(QStringLiteral("result")).toObject()));

            if (id == m_loginJob) {
                if (root.value(QStringLiteral("error")).toObject().value(QStringLiteral("error")).toInt() == 403) {
                    qCDebug(RUQOLA_LOG) << "Wrong password or token expired";

                    login(); // Let's keep trying to log in
                } else {
                    Ruqola::self()->setAuthToken(root.value(QStringLiteral("result")).toObject().value(QStringLiteral("token")).toString());
                    setLoginStatus(DDPClient::LoggedIn);
                }
            }
        } else if (messageType == QLatin1String("connected")) {
            qCDebug(RUQOLA_LOG) << "Connected";
            m_connected = true;
            Q_EMIT connectedChanged();
            setLoginStatus(DDPClient::LoggingIn);
            //Ruqola::self()->authentication()->OAuthLogin();

            login(); // Try to resume auth token login
        } else if (messageType == QLatin1String("error")) {
            qCDebug(RUQOLA_LOG) << "ERROR!!" << message;
        } else if (messageType == QLatin1String("ping")) {
            qCDebug(RUQOLA_LOG) << "Ping - Pong";
            QJsonObject pong;
            pong[QStringLiteral("msg")] = QStringLiteral("pong");
            mWebSocket->sendBinaryMessage(QJsonDocument(pong).toJson(QJsonDocument::Compact));
        } else if (messageType == QLatin1String("added")) {
            qCDebug(RUQOLA_LOG) << "ADDING" <<root;
            Q_EMIT added(root);
        } else if (messageType == QLatin1String("changed")) {
            Q_EMIT changed(root);
        } else if (messageType == QLatin1String("ready")) {
            // do nothing
        } else {
            qCDebug(RUQOLA_LOG) << "received something unhandled:" << message;
        }
    }
}

void DDPClient::login()
{
    if (!Ruqola::self()->password().isEmpty()) {
        // If we have a password and we couldn't log in, let's stop here
        if (m_attemptedPasswordLogin) {
            setLoginStatus(LoginFailed);
            return;
        }
        m_attemptedPasswordLogin = true;
        QJsonObject user;
        user[QStringLiteral("username")] = Ruqola::self()->userName();
        QJsonObject json;
        json[QStringLiteral("password")] = Ruqola::self()->password();
        json[QStringLiteral("user")] = user;
        m_loginJob = method(QStringLiteral("login"), QJsonDocument(json));
    } else if (!Ruqola::self()->authToken().isEmpty() && !m_attemptedTokenLogin) {
        m_attemptedPasswordLogin = true;
        QJsonObject json;
        json[QStringLiteral("resume")] = Ruqola::self()->authToken();
        m_loginJob = method(QStringLiteral("login"), QJsonDocument(json));
    } else {
        setLoginStatus(LoginFailed);
    }
}

void DDPClient::onWSConnected()
{
    qCDebug(RUQOLA_LOG) << "Websocket connected at URL" << m_url;

    QJsonArray supportedVersions;
    supportedVersions.append(QStringLiteral("1"));
    QJsonObject protocol;
    protocol[QStringLiteral("msg")] = QStringLiteral("connect");
    protocol[QStringLiteral("version")] = QStringLiteral("1");
    protocol[QStringLiteral("support")] = supportedVersions;
    QByteArray serialize = QJsonDocument(protocol).toJson(QJsonDocument::Compact);
    qint64 bytes = mWebSocket->sendTextMessage(QString::fromUtf8(serialize));
    if (bytes < serialize.length()) {
        qCDebug(RUQOLA_LOG) << "onWSConnected: ERROR! I couldn't send all of my message. This is a bug! (try again)";
    } else {
        qCDebug(RUQOLA_LOG) << "Successfully sent " << serialize;
    }
}

void DDPClient::onWSclosed()
{
    qCDebug(RUQOLA_LOG) << "WebSocket CLOSED" << mWebSocket->closeReason() << mWebSocket->error() << mWebSocket->closeCode();
    setLoginStatus(NotConnected);
}
