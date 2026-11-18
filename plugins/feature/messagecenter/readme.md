<h1>Message Center feature plugin</h1>

<h2>Introduction</h2>

The Message Center collects the messages of the radio paging channels (Radio Paging demodulator and modulator) in one log, routes them to files, and queues pages to the transmitters. It is the place where messages, events and voice messages are exchanged between the decoders and the encoders.

The channels publish their messages as message records on the "messages" pipe, and announce themselves with event records: the Message Center knows the receivers and transmitters without configuration. It is added to feature set 0 when the first paging channel announces itself and there is none.

```
  source ---> route (filter) ---> sink(s)
  sources: the channels, a JSON lines file
  sinks:   the log, a JSON lines file, a CSV file, the transmit queue
```

<h2>Interface</h2>

The window is in sections that can be rolled up.

<h3>Log</h3>

The messages received and sent, newest at the bottom (at most the log size, the oldest go first).

* Filters: protocol, direction (RX, TX or both), polarity, message type, channel alias, address (wildcards * and ?), text (contained, not case sensitive), hide duplicates, voice pages with a file only. Only the messages the filters select are shown.
* The table has the columns of the message tables of the channels: time, direction, channel, protocol, rate, polarity, address, type, message, fragment, info, errors and state. Right click on the header selects the columns.
* A click on the icon of a voice message plays its file. Double click on a message opens a window with all the fields of its record: the common fields, protocolData (the flags and parameters of the protocol) and the raw payload, also as bits.
* Right click on a message: details, copy the cell, the row or the record as JSON, play the voice, select the audio output, and decode a received message again as the other type or with another codepage of its protocol.
* Clear: removes all the messages of the log, and of the history file when it is kept, after a confirmation.

<h3>Channels</h3>

The channels that announced themselves: alias, role (receiver or transmitter), status (online, or offline when it has not announced itself for the silent time), protocols, speeds, polarity, the pages queued to it, and the time it was last seen.

<h3>Queue</h3>

The pages to transmit, with the transmitter (alias), protocol, address, text, state, tries, retries and the reason of a failure. A page goes through pending, queued, claimed, transmitted and sent: the transmitter claims it and reports its state back. A page given to a transmitter that does not claim it within the claim timeout goes back to the queue; a page that can not be sent is dead.

* Requeue: a dead page goes back to pending with its attempts reset.
* Cancel: withdraws a page nobody has claimed, or a dead page.
* Clear sent: removes the pages sent.

<h3>Page</h3>

The composer of a page, the same as in the Radio Paging modulator: transmitter (alias), protocol, address, type, text and the fields of the protocol. Send queues the page for the transmitter.

<h3>Routes</h3>

The records go through the routes in order. A route sends the records its filter selects to its sinks (log, queue or a file); stop: no route after it. A record no route selects is logged. Add, Edit (or double click) and Remove manage the routes; a route can be switched off.

<h3>Files</h3>

* Sinks: files the routes can send records to, in JSON lines (one record per line, lossless) or CSV (the columns of the record).
* Sources: JSON lines files whose new records are read as if they came from the channels.

<h3>Options</h3>

* Log size: messages kept in the log (default 10000).
* History: keep the log in the history file (messagecenter/history.jsonl in the application data directory), so that it is there again when the Message Center starts. One Message Center keeps the history. Over its size limit (default 50 MB) the file is written again with the newest messages of the log, up to half the limit.
* Claim timeout: a page given to a transmitter that does not claim it in this time goes back to the queue (default 10 s).
* Silent time: a channel that does not announce itself for this time is offline (default 20 s).

<h2>REST API</h2>

Settings (`MessageCenterSettings`, feature type `MessageCenter`): title, rgbColor, logSize, historyEnabled, historyMaxMB, claimTimeoutSec, silentSec, routes, sinks, sources and the reverse API settings.

Report (`MessageCenterReport`): the number of messages logged and routed, the endpoints, the pages, the log and the errors.

Actions (`MessageCenterActions`):

* `post`: a page to transmit (`MessageCenterPost`): transmitter alias, protocol, address, type, text and the fields of the protocol. The FLEX address takes the capcode forms of STD-43A Appendix A.
* `requeue`, `cancel`: the id of a page.
* `clearSent`: 1 to remove the pages sent.
* `redecode`: a record of the log decoded again (`MessageCenterRedecode`: id, charset, type).
