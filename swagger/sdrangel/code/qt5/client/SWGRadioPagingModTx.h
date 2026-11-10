// SPDX-License-Identifier: GPL-3.0-or-later
// Qt model for RadioPagingMod.yaml, following the Swagger client model interface.
#ifndef SWGRadioPagingModTx_H_
#define SWGRadioPagingModTx_H_
#include <QJsonObject>
#include <QString>
#include "SWGObject.h"
#include "export.h"

namespace SWGSDRangel {
class SWG_API SWGRadioPagingModTx : public SWGObject {
public:
    SWGRadioPagingModTx();
    explicit SWGRadioPagingModTx(QString* json);
    ~SWGRadioPagingModTx() override;
    void init();
    void cleanup();
    QString asJson() override;
    QJsonObject* asJsonObject() override;
    void fromJsonObject(QJsonObject& json) override;
    SWGRadioPagingModTx* fromJson(QString& json) override;
    bool isSet() override;
    qint32 getAddress();
    void setAddress(qint32 value);
    qint32 getFunction();
    void setFunction(qint32 value);
    qint64 getCapcode();
    void setCapcode(qint64 value);
    qint32 getPriority();
    void setPriority(qint32 value);
    QString* getProtocol();
    void setProtocol(QString* value);
    QString* getType();
    void setType(QString* value);
    QString* getText();
    void setText(QString* value);
    qint32 getSpeed();
    void setSpeed(qint32 value);
    QString* getId();
    void setId(QString* value);
    QString* getPolarity();
    void setPolarity(QString* value);
    qint32 getShortType();
    void setShortType(qint32 value);
    qint32 getSource();
    void setSource(qint32 value);
private:
    qint32 m_shortType = 0;
    bool m_shortTypeIsSet = false;
    qint32 m_source = 0;
    bool m_sourceIsSet = false;
    qint32 m_address = 0;
    bool m_addressIsSet = false;
    qint32 m_function = 0;
    bool m_functionIsSet = false;
    qint64 m_capcode = 0;
    bool m_capcodeIsSet = false;
    qint32 m_priority = 0;
    bool m_priorityIsSet = false;
    QString* m_protocol = nullptr;
    bool m_protocolIsSet = false;
    QString* m_type = nullptr;
    bool m_typeIsSet = false;
    QString* m_text = nullptr;
    bool m_textIsSet = false;
    qint32 m_speed = 0;
    bool m_speedIsSet = false;
    QString* m_id = nullptr;
    bool m_idIsSet = false;
    QString* m_polarity = nullptr;
    bool m_polarityIsSet = false;
};
}
#endif
