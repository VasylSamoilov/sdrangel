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
4. Add this feature, give the device set numbers of the radios in the network tab, and press Start.

<h2>Interface</h2>

<h3>Start / Stop</h3>

Starts the controller: the networks that are on the air start. Stop takes all networks off the air.

<h3>Add network / Remove network</h3>

Adds a network in its own tab (it is off until switched on) or removes the network of the open tab.

<h3>Transmit lead</h3>

How far the transmitters run ahead of the receivers. The lead is part of the round trip between a phone and the base station: the answer to a phone is late by the time the audio takes through the receiving device, the controller, the lead and the transmitting device. Keep the round trip below about 300 ms. If the transmit queues run empty (the Empty column) the lead is too small. Taken when the controller starts.

<h3>Network tab</h3>

Changes of a running network are taken when it is switched off and on again; the tab says so.

* Name: the name of the network.
* On the air while the controller runs: switch a network on or off without touching the others.
* System: NMT-450 or NMT-900.
* Country: the frequency plan (channel numbers and frequencies) and the country digit of the traffic area. Countries that were not tried with a phone are marked untested.
* Traffic area: the second digit of the traffic area (1 to 9, up to 15 in some countries). Phones that were registered in another area register again.
* Area number: 1 to 4 keeps the phones of that area, none takes every phone.
* Phone power: the power level the phones send with, 3 is the highest.
* Carriers: the channels of the network, comma separated: `channel[:type[:supervisory]]`. The types are `cc` (calling channel), `tc` (traffic channel), `cctc` (both, the default for a small network), `actc`, `cca`, `ccb` (NMT-900) and `test`. A traffic channel may have a supervisory tone 1 to 4. Example: `1:cctc, 5, 6:tc:2`. A network needs a calling channel and as many traffic channels as calls should run at once.
* Compandor on speech, Send the number of the caller, Send the time of day: options of the network.
* SMS centre: the number the phones send short messages to. Empty: no short messages from the phones.
* Other numbers: what answers a call to a number that is not registered, echo or music.
* Receiver device set, Transmitter device set: the device sets with the Cellular Rx and the Cellular Tx channel of the network.

Below the settings: the state of the network, a table of its carriers (frequencies, state of the channel, received level, milliseconds of audio in the transmit queue, number of times the queue ran empty) and its event log. A carrier that cannot go on the air is red, with the reason: a channel that is not in the plan of the country, a frequency outside the span of the device, or the frequency of a channel of another network on the same device.

A network is refused if its carriers do not fit one receiving and one transmitting device.

<h2>Web API</h2>

Settings (`GET`, `PUT`, `PATCH` on `/sdrangel/featureset/{n}/feature/{m}/settings`) and report (`GET .../report`) are documented in the web API documentation under `CellularSettings` and `CellularReport`. A `PATCH` with a `networks` list changes the networks that it names by `id` and starts or stops them as `enabled` says; fields that are not given keep their value.

<h2>Limits</h2>

* No calls between networks yet, only inside a network (phone to phone) and to the echo or music.
* Access control (which phones may register) is not implemented: every phone is taken.
* The AMPS and TACS standards are not implemented.
