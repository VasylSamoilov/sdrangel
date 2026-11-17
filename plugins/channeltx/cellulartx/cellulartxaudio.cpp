///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov                                             //
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

#include "maincore.h"
#include "util/messagequeue.h"

#include "carrierwire.h"

#include "cellulartx.h"
#include "cellulartxaudio.h"

void CellularTxAudio::setQueue(MessageQueue *queue)
{
    MessageQueue *old = m_queue.exchange(queue);

    if (old) {
        disconnect(old, &MessageQueue::messageEnqueued, this, &CellularTxAudio::handleQueue);
    }
    if (queue) {
        // the object lives in the thread of the channel: the queue is drained there
        connect(queue, &MessageQueue::messageEnqueued, this, &CellularTxAudio::handleQueue, Qt::QueuedConnection);
    }
}

void CellularTxAudio::handleQueue()
{
    MessageQueue *queue = m_queue.load();
    Message *message;

    if (!queue) {
        return;
    }
    while ((message = queue->pop()))
    {
        if (MainCore::MsgPacket::match(*message))
        {
            const QByteArray bytes = static_cast<const MainCore::MsgPacket&>(*message).getPacket();
            modemcellular::CarrierBatch batch;

            if (modemcellular::decodeBatch(reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size()), batch)) {
                m_channel->writeBatch(batch);
            }
        }
        delete message;
    }
}
