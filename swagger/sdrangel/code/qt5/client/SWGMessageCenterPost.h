// SPDX-License-Identifier: GPL-3.0-or-later
// Qt model for MessageCenter.yaml, following the Swagger client model interface.
#ifndef SWGMessageCenterPost_H_
#define SWGMessageCenterPost_H_
#include <QJsonObject>
#include <QString>
#include "SWGObject.h"
#include "export.h"

namespace SWGSDRangel {
class SWG_API SWGMessageCenterPost : public SWGObject {
public:
    SWGMessageCenterPost();
    explicit SWGMessageCenterPost(QString* json);
    ~SWGMessageCenterPost() override;
    void init();
    void cleanup();
    QString asJson() override;
    QJsonObject* asJsonObject() override;
    void fromJsonObject(QJsonObject& json) override;
    SWGMessageCenterPost* fromJson(QString& json) override;
    bool isSet() override;
    QString* getId();
    void setId(QString* value);
    QString* getTransmitter();
    void setTransmitter(QString* value);
    QString* getProtocol();
    void setProtocol(QString* value);
    QString* getAddress();
    void setAddress(QString* value);
    qint32 getFunction();
    void setFunction(qint32 value);
    qint32 getPriority();
    void setPriority(qint32 value);
    QString* getType();
    void setType(QString* value);
    QString* getText();
    void setText(QString* value);
    QString* getVoiceFile();
    void setVoiceFile(QString* value);
    qint32 getSpeed();
    void setSpeed(qint32 value);
    QString* getPolarity();
    void setPolarity(QString* value);
    qint32 getShortType();
    void setShortType(qint32 value);
    qint32 getSource();
    void setSource(qint32 value);
    qint32 getBlocking();
    void setBlocking(qint32 value);
    qint32 getMailDrop();
    void setMailDrop(qint32 value);
    qint32 getRightToLeft();
    void setRightToLeft(qint32 value);
    qint32 getHeaderMessage();
    void setHeaderMessage(qint32 value);
    qint32 getStatusInfo();
    void setStatusInfo(qint32 value);
    qint32 getSecureType();
    void setSecureType(qint32 value);
private:
    qint32 m_blocking = 0;
    bool m_blockingIsSet = false;
    qint32 m_mailDrop = 0;
    bool m_mailDropIsSet = false;
    qint32 m_rightToLeft = 0;
    bool m_rightToLeftIsSet = false;
    qint32 m_headerMessage = 0;
    bool m_headerMessageIsSet = false;
    qint32 m_statusInfo = 0;
    bool m_statusInfoIsSet = false;
    qint32 m_secureType = 0;
    bool m_secureTypeIsSet = false;
    qint32 m_shortType = 0;
    bool m_shortTypeIsSet = false;
    qint32 m_source = 0;
    bool m_sourceIsSet = false;
    QString* m_id = nullptr;
    bool m_idIsSet = false;
    QString* m_transmitter = nullptr;
    bool m_transmitterIsSet = false;
    QString* m_protocol = nullptr;
    bool m_protocolIsSet = false;
    QString* m_address = nullptr;
    bool m_addressIsSet = false;
    qint32 m_function = 0;
    bool m_functionIsSet = false;
    qint32 m_priority = 0;
    bool m_priorityIsSet = false;
    QString* m_type = nullptr;
    bool m_typeIsSet = false;
    QString* m_text = nullptr;
    bool m_textIsSet = false;
    QString* m_voiceFile = nullptr;
    bool m_voiceFileIsSet = false;
    qint32 m_speed = 0;
    bool m_speedIsSet = false;
    QString* m_polarity = nullptr;
    bool m_polarityIsSet = false;
};
}
#endif
