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

#ifndef INCLUDE_CELLULARTXAUDIO_H
#define INCLUDE_CELLULARTXAUDIO_H

#include <atomic>

#include <QObject>

class CellularTx;
class MessageQueue;

// The audio of the controller on its way into the queues of the carriers, in the thread of the
// channel (not the thread of the GUI): the batches come through a message pipe from the
// controller to the channel.
class CellularTxAudio : public QObject
{
    Q_OBJECT
public:
    explicit CellularTxAudio(CellularTx *channel) : m_channel(channel), m_queue(nullptr) {}

    // from the thread of the channel object
    void setQueue(MessageQueue *queue);

private slots:
    void handleQueue();

private:
    CellularTx *m_channel;
    std::atomic<MessageQueue*> m_queue;
};

#endif // INCLUDE_CELLULARTXAUDIO_H
