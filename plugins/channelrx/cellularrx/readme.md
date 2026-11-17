<h1>Cellular Rx plugin</h1>

<h2>Introduction</h2>

This plugin receives the carriers of the phones of the cellular base stations. Put one on each receiving device set that a [Cellular](../../feature/cellular/readme.md) network uses.

The plugin has no radio settings of its own. The Cellular feature gives it the carriers of all networks that use the device. For each carrier it shifts the channel to the center, filters it, demodulates the FM and hands the audio of all carriers to the feature, one block of 10 ms at a time. Its clock is the sample count of the device, which keeps all carriers and networks in step.

<h2>Interface</h2>

The top and bottom bars of the channel window are described [here](../../../sdrgui/channel/readme.md)

<h3>Stream</h3>

The center frequency and sample rate of the device. The sample rate has to be an even multiple of 48 kHz (960 kS/s, 1.92 MS/s, 2.4 MS/s ...); the plugin says so when it is not. It also says when no network uses the device.

<h3>Carrier boxes</h3>

One box per carrier: the network and the channel number, the offset of the carrier from the center of the stream in kHz, and the received level in dB of full scale. A level around -100 dB is noise: no phone is transmitting.

<h2>Notes</h2>

* All carriers of one network have to be inside the span of the device, which is 75 % of the sample rate.
* A carrier at the exact center of the stream is at the DC of the receiver, which is a weak spot of many SDRs. Move the center by a few 100 kHz.
