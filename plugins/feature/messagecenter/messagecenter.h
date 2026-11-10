///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
// (at your option) any later version.                                           //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#ifndef INCLUDE_FEATURE_MESSAGECENTER_H_
#define INCLUDE_FEATURE_MESSAGECENTER_H_

#include <QMap>
#include <QPointer>
#include <QRecursiveMutex>
#include <QTimer>

#include "availablechannelorfeaturehandler.h"
#include "feature/feature.h"
#include "util/message.h"
#include "util/messagerecord.h"

#include "messagecenterengine.h"
#include "messagecentersettings.h"

class WebAPIAdapterInterface;
class ChannelAPI;

namespace SWGSDRangel {
    class SWGFeatureSettings;
    class SWGFeatureReport;
    class SWGFeatureActions;
}

// The message center feature: the messages of the channels (the "messages" pipe) and of the files
// are routed to the log, the files and the queue; the pages in the queue are given to the
// modulators that announced themselves, and their states come back on the pipe (see
// MessageCenterEngine). One thread: the one of the feature; the GUI reads the engine.
class MessageCenter : public Feature
{
    Q_OBJECT
public:
    class MsgConfigureMessageCenter : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const MessageCenterSettings& getSettings() const { return m_settings; }
        const QStringList& getSettingsKeys() const { return m_settingsKeys; }
        bool getForce() const { return m_force; }

        static MsgConfigureMessageCenter* create(const MessageCenterSettings& settings, const QStringList& settingsKeys, bool force) {
            return new MsgConfigureMessageCenter(settings, settingsKeys, force);
        }

    private:
        MessageCenterSettings m_settings;
        QStringList m_settingsKeys;
        bool m_force;

        MsgConfigureMessageCenter(const MessageCenterSettings& settings, const QStringList& settingsKeys, bool force) :
            Message(),
            m_settings(settings),
            m_settingsKeys(settingsKeys),
            m_force(force)
        { }
    };

    //! A page to transmit, composed in the GUI
    class MsgPostPage : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const MessageRecord& getRecord() const { return m_record; }

        static MsgPostPage* create(const MessageRecord& record) {
            return new MsgPostPage(record);
        }

    private:
        MessageRecord m_record;

        MsgPostPage(const MessageRecord& record) : Message(), m_record(record) { }
    };

    //! Something to do with a page of the queue
    class MsgPageAction : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        enum Action { Requeue, Cancel, ClearSent };

        const QString& getId() const { return m_id; }
        Action getAction() const { return m_action; }

        static MsgPageAction* create(const QString& id, Action action) {
            return new MsgPageAction(id, action);
        }

    private:
        QString m_id;
        Action m_action;

        MsgPageAction(const QString& id, Action action) : Message(), m_id(id), m_action(action) { }
    };

    //! To the GUI: the log, the endpoints or the queue changed
    class MsgUpdated : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        static MsgUpdated* create() { return new MsgUpdated(); }

    private:
        MsgUpdated() : Message() { }
    };

    MessageCenter(WebAPIAdapterInterface *webAPIAdapterInterface);
    ~MessageCenter() override;

    void destroy() override { delete this; }
    bool handleMessage(const Message& cmd) override;

    void getIdentifier(QString& id) const override { id = objectName(); }
    QString getIdentifier() const override { return objectName(); }
    void getTitle(QString& title) const override { title = m_settings.m_title; }

    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data) override;

    int webapiSettingsGet(
        SWGSDRangel::SWGFeatureSettings& response,
        QString& errorMessage) override;

    int webapiSettingsPutPatch(
        bool force,
        const QStringList& featureSettingsKeys,
        SWGSDRangel::SWGFeatureSettings& response,
        QString& errorMessage) override;

    int webapiReportGet(
        SWGSDRangel::SWGFeatureReport& response,
        QString& errorMessage) override;

    int webapiActionsPost(
        const QStringList& featureActionsKeys,
        SWGSDRangel::SWGFeatureActions& query,
        QString& errorMessage) override;

    static void webapiFormatFeatureSettings(
        SWGSDRangel::SWGFeatureSettings& response,
        const MessageCenterSettings& settings);

    static void webapiUpdateFeatureSettings(
        MessageCenterSettings& settings,
        const QStringList& featureSettingsKeys,
        SWGSDRangel::SWGFeatureSettings& response);

    //! A page: the transmitter it is for, the message and how to send it. The address is as the protocol has it
    //! (POCSAG: the address, FLEX: the capcode, GSC: up to 6 digits); the function 0 to 3
    static MessageRecord pageRecord(const QString& transmitter, const QString& protocol, const QString& address, int function,
        int priority, const QString& type, const QString& text, const QString& voiceFile, int speed, const QString& polarity);

    //! The engine, for the GUI to read (same thread)
    const MessageCenterEngine& engine() const { return m_engine; }
    const MessageCenterSettings& settings() const { return m_settings; }
    //! Errors of the sinks and sources since the last call
    QStringList takeErrors();

    static const char* const m_featureIdURI;
    static const char* const m_featureId;

private:
    MessageCenterSettings m_settings;
    MessageCenterEngine m_engine;
    AvailableChannelOrFeatureHandler m_availableChannelHandler;
    QTimer m_timer;
    QMap<QString, QPointer<ChannelAPI>> m_transmitters;     //!< By alias, the channels that announced themselves as transmitters
    QStringList m_errors;
    bool m_dirty = false;
    mutable QRecursiveMutex m_mutex;        //!< The engine: the feature thread writes it, the WebAPI reads it

    void applySettings(const MessageCenterSettings& settings, const QStringList& settingsKeys, bool force = false);
    void applyEngineSettings();
    void ingest(const MessageRecord& record, const QObject *source);
    void tick();
    //! The page to the transmitter named on it, as the "tx" action of its channel. The HTTP code and the error
    int postToTransmitter(const MessageCenterPage& page, QString& error);

private slots:
    void handleChannelMessageQueue(MessageQueue *messageQueue);
};

#endif // INCLUDE_FEATURE_MESSAGECENTER_H_
