/*

 * Copyright 2016  Riccardo Iaconelli <riccardo@kde.org>
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

#ifndef USERDATA_H
#define USERDATA_H

#include <QObject>
#include "libruqolacore_export.h"
#include <QQmlEngine>
#include <QJSEngine>
#include "ddpclient.h"
#include "roommodel.h"
#include "messagemodel.h"
#include "notification.h"
#include "messagequeue.h"
#include "authentication.h"

class RoomFilterProxyModel;

class TypingNotification;
class UserModel;
class RestApiRequest;
class LIBRUQOLACORE_EXPORT Ruqola : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY userNameChanged)
    Q_PROPERTY(QString userID READ userID WRITE setUserID NOTIFY userIDChanged)
    Q_PROPERTY(QString serverURL READ serverURL WRITE setServerURL NOTIFY serverURLChanged)
    Q_PROPERTY(QString password WRITE setPassword)
    Q_PROPERTY(DDPClient::LoginStatus loginStatus READ loginStatus NOTIFY loginStatusChanged)
//     Q_PROPERTY(QString activeRoom READ activeRoom WRITE setActiveRoom NOTIFY activeRoomChanged)

public:

    /**
    * @brief Singleton provider
    *
    * @return Returns the singleton object m_self
    */
    static Ruqola *self();

    void setUserName(const QString &username);
    QString userName() const;

    void setUserID(const QString &userID);
    QString userID() const;

    void setPassword(const QString &password);
    QString password() const;

    void setAuthToken(const QString &token);
    QString authToken() const;

    bool connected();
    DDPClient::LoginStatus loginStatus();

    QString serverURL() const;
    void setServerURL(const QString &serverURL);

//     QString activeRoom() const;
//     void setActiveRoom(const QString &activeRoom);

    DDPClient *ddp();
    Notification *notification();
    MessageQueue *messageQueue();
    Authentication *authentication();

    Q_INVOKABLE RoomModel *roomModel();
    Q_INVOKABLE UserModel *userModel() const;

    /**
    * @brief Constructs a Json with @param roomID and @param message and @param type, then calls DDPClient's method to send text message over the network
    */
    Q_INVOKABLE void sendMessage(const QString &roomID, const QString &message, const QString &type);

    Q_INVOKABLE void leaveRoom(const QString &roomID);
    Q_INVOKABLE void hideRoom(const QString &roomID);

    /**
    * @brief Returns a model for room with ID @param roomID
    *
    * @return MessageModel Pointer, model for room
    */
    Q_INVOKABLE MessageModel *getMessageModelForRoom(const QString &roomID);

    /**
    * @brief Reset models, load cache and call DDPClient's object to automatically try to connect and log in via username and password
    */
    Q_INVOKABLE void tryLogin();

    /**
    * @brief Clear models, stores cache and logs out the user
    */
    Q_INVOKABLE void logOut();
    /**
    * @brief Finds room with @param roomID
    *
    * @return RoomWrapper Pointer, The room model for @param roomID
    */
    Q_INVOKABLE RoomWrapper *getRoom(const QString &roomID);

    Q_INVOKABLE void attachmentButtonClicked(const QString &roomId);
    Q_INVOKABLE void textEditing(const QString &roomId, const QString &str);

    /**
    * @brief Returns standard cache path
    *
    * @return QString, The standard cache path
    */
    QString cacheBasePath() const;

    Q_INVOKABLE RoomFilterProxyModel *roomFilterProxyModel() const;

    RestApiRequest *restapi();
Q_SIGNALS:
    void userNameChanged();
    void userIDChanged();
    void serverURLChanged();
    void loginStatusChanged();

private:
    void slotInformTypingStatus(const QString &room, bool typing);
    explicit Ruqola(QObject *parent = nullptr);
    QString m_password;
    QString m_userName;
    QString m_userID;
    QString m_authToken;
    QString m_serverURL;

    DDPClient *m_ddp;
    MessageQueue *m_messageQueue;
    RoomModel *m_roomModel;
    RoomFilterProxyModel *mRoomFilterProxyModel;
    Notification *m_notification;
    Authentication *m_authentication;

    QHash< QString, MessageModel * > m_messageModels;
    TypingNotification *mTypingNotification;
    UserModel *mUserModel = nullptr;
    RestApiRequest *mRestApi = nullptr;
};

inline static QObject *ruqola_singletontype_provider(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

    Ruqola *userData = Ruqola::self();
    return userData;
}

#endif // USERDATA_H
