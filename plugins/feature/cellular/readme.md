<h1>Cellular plugin</h1>

<h2>Introduction</h2>

This feature runs analogue cellular base stations (NMT-450 and NMT-900) on the radios of SDRangel. Phones register, make and receive calls and send short messages. Calls to numbers that are not registered are answered by an echo or by music.

The feature holds the networks and all their protocol state. The radio part is done by two channel plugins, one on each device:

* [Cellular Rx](../../channelrx/cellularrx/readme.md) on the receiving device set: it receives the carriers of the phones.
* [Cellular Tx](../../channeltx/cellulartx/readme.md) on the transmitting device set: it transmits the carriers of the base stations.

A network is one base station with its own carriers, registered phones, calls and event log. Several networks may run at the same time, on the same radios or on different ones. A network uses exactly one receiving and one transmitting device set, which may be on different SDRs.

<h2>Setting up</h2>

1. Add a receiving device set with a Cellular Rx channel and a transmitting device set with a Cellular Tx channel.
2. Set the sample rate of both devices to an even multiple of 48 kHz: 960 kS/s, 1.92 MS/s, 2.4 MS/s ... The carriers of a network have to be inside the span of the device (75 % of the sample rate). Channel 1 of NMT-450 in Denmark is 463.000 MHz down and 453.000 MHz up, so a device at 453.3 MHz and one at 463.3 MHz with 960 kS/s carry the channels 1 to about 27.
3. Run both devices.
4. Add this feature, give the device set numbers of the radios in the settings of the network (Networks tab), switch it on and press Start.

<h2>Interface</h2>

The window fits small screens: the tabs are on its side, the tables scroll, the settings of a network are in a dialog.

<h3>Start / Stop, summary, options</h3>

Start takes the networks that are on to the air, Stop takes all networks off the air. Next to it: how many networks are on the air, the phones registered and the calls. The options button opens the options of the tester:

* Transmit lead: how far the transmitters run ahead of the receivers. The lead is part of the round trip between a phone and the base station: the answer to a phone is late by the time the audio takes through the receiving device, the controller, the lead and the transmitting device. Keep the round trip below about 300 ms. If the transmit queues run empty (the Empty column of Carriers) the lead is too small. Taken when the tester starts.

<h3>Phones</h3>

Every phone a network heard (its identity in a frame for the channel it came on) or that registered, one row per phone and network: number, what it does now, channel, network, registered (yes: it made a roaming update to this network), when it was last heard, the received level and the quality of its last frame, the power level in its frames (high, medium, low) and the type of its last frame. A phone keeps its row. Right click: the actions of the Test tab on that phone; double click: the phone in the Test tab.

<h3>Test</h3>

Actions of a network on one phone. Network: the network that does it. Phone: the country digit and the six digits, typed or one the network heard; below it what the phone does now.

* Call: the network calls the phone, registered or not; it rings, and when it answers it hears itself (echo, after 1 s) or music, as set in Other numbers. Hang up ends that call. Release ends any call of the phone from the network side.
* SMS: Send sends a short message to the phone; the log tells whether the phone took it. Keep leaves it in the SMS centre until the phone calls the centre for its waiting messages. Messages the phones send to the SMS centre are in the log too.
* Indicator: a message waiting indicator (SMS, voice mail, fax, e-mail, data). A phone that is idle is called without ringing for it and gets it in the clearing of that call; a phone in a conversation gets it at once and again in the clearing of its call; in another procedure of a call only in the clearing. When the phone is busy with something else the indicator is postponed and goes out when the phone is free (the log tells both). The SMS indicator tells the phone that messages wait in its SMS centre: a phone with SMS calls the centre by itself and asks for them; the log tells it, and a kept message goes to it then. The network cannot switch an indicator off: the phone does, e.g. the voice mail lamp when the user calls the voice mail. Seen with one NMT-450 phone: the voice mail lamp lit at once in a conversation; the SMS indicator made it fetch only when it came in a clearing alone, not when it came in the conversation (also not with the copy in the clearing that follows), and not while its message memory was full (it fetched as soon as there was room).
* Power: orders the phone in its call to high, medium or low power.
* Move: switches the call to another traffic channel of the network (any: the first free one).

<h3>Log</h3>

The events of all networks, of the tester itself, or of one network. With Frames, every frame from the phones (with its quality and level) and every frame to them except the indications a channel sends all the time; a frame sent again and again is shown once with its count. The view stays where it was scrolled to; at the end it follows the new lines. The bin clears it.

<h3>Networks</h3>

The networks of the tester. On: on the air while the tester runs; a network is switched on or off without touching the others. The plus button adds a network (off until switched on), the pencil (or a double click) opens the settings of the selected network, the bin removes it. Changed settings of a running network are taken with OK: the network starts again with them, and its calls are cleared. Renaming a network does not start it again. The settings of a network:

* Name: the name of the network.
* On the air while the tester runs: as On in the list.
* System: NMT-450 or NMT-900.
* Country: the frequency plan (channel numbers and frequencies) and the country digit of the traffic area. Countries that were not tried with a phone are marked untested.
* Country digit Y1: "country" sends the first digit of the traffic area that belongs to the country (DK 5, SE 6, NO 7, FI 8 ...). A value 0..15 (NMT-900 0..7) makes a test network with that digit on the frequency plan of the country. A phone takes only calling channels with the Y1 of its country selector, so test networks with different Y1 at the same time show which one a phone is set to.
* Traffic area: the second digit of the traffic area (1 to 9, up to 15 in some countries). Phones that were registered in another area register again.
* Area number: 1 to 4 keeps the phones of that area, none takes every phone.
* Phone power: the power level the phones send with, 3 is the highest.
* Carriers: the channels of the network, comma separated: `channel:type[:supervisory]`. Every channel needs its type: `cc` (calling channel), `tc` (traffic channel), `cctc` (both), `actc`, `cca`, `ccb` (NMT-900) or `test`. A traffic channel may have a supervisory tone 1 to 4. Example: `1:cctc:1, 5:tc:2, 6:tc:3`. A network needs a calling channel and as many traffic channels as calls should run at once. When no channel gives a supervisory tone, the traffic channels get the tones 1, 2, 3, 4, 1 ... in turn. When one channel gives a tone, every traffic channel needs its tone; 0 is no tone, which is warned about: a call that loses the phone is not cleared. A network without a calling or without a traffic channel is warned about in its log.
* Compandor on speech, Send the number of the caller, Send the time of day: options of the network.
* SMS centre: the number the phones send short messages to. Empty: no short messages from the phones.
* Other numbers: what answers a call to a number that is not registered, echo or music.
* Receiver device set, Transmitter device set: the device sets with the Cellular Rx and the Cellular Tx channel of the network.

A network is refused if its carriers do not fit one receiving and one transmitting device.

<h3>Carriers</h3>

The carriers of the networks: network, channel, type, frequencies, state of the channel, received level, milliseconds of audio in the transmit queue, number of times the queue ran empty, the phone the channel serves. A carrier that cannot go on the air is red, with the reason: a channel that is not in the plan of the country, a frequency outside the span of the device, or the frequency of a channel of another network on the same device.

<h2>Web API</h2>

Settings (`GET`, `PUT`, `PATCH` on `/sdrangel/featureset/{n}/feature/{m}/settings`) and report (`GET .../report`) are documented in the web API documentation under `CellularSettings` and `CellularReport`. The actions on a phone are `POST .../actions` with `CellularActions` (`networkId`, `action`: call, hangup, release, sms, mwi, power, move or hold, `number`, `text`, `value`). `hold` is Keep of the Test tab. For `mwi` to a phone in a call, `text` `incall` sends the indicators only in the conversation and `clearing` only in the clearing sequence; empty sends them in both, as the Indicator button does. A `PATCH` with a `networks` list replaces the networks: those it names by `id` are changed and started or stopped as `enabled` says, those it does not name are removed; fields that are not given keep their value; a network that runs starts again when its settings changed. A `PUT` does the same and also starts the networks that run again, with the new settings.

<h2>Limits</h2>

* No calls between networks yet, only inside a network (phone to phone) and to the echo or music.
* Access control (which phones may register) is not implemented: every phone is taken.
* The AMPS and TACS standards are not implemented.
