<h1>Cellular Tx plugin</h1>

<h2>Introduction</h2>

This plugin transmits the carriers of the cellular base stations. Put one on each transmitting device set that a [Cellular](../../feature/cellular/readme.md) network uses. Only one: a network does not start on a device set with two.

The plugin has no radio settings of its own. The Cellular feature gives it the carriers of all networks that use the device and the audio of each carrier. The plugin keeps a queue of audio for each carrier, modulates the FM, shifts the carriers to their place in the stream and adds them up, each at an equal share of the output. The sum stays about 1.4 dB below full scale: the channel filters overshoot a little when they band-limit the FM, and the output never clips. Set the output power with the gain of the device.

The queue holds the lead of the transmitter over the receiver and follows the small difference between the clocks of the two devices by resampling by at most 200 ppm. If the receiving and the transmitting device are on different SDRs the difference is typically a few ppm to a few tens of ppm. A queue that runs empty sends silence until it holds its whole lead again, so the delay does not shrink.

<h2>Interface</h2>

The top and bottom bars of the channel window are described [here](../../../sdrgui/channel/readme.md)

<h3>Stream</h3>

The center frequency and sample rate of the device, the lead of the queues in ms, the number of blocks received and how often a queue was restarted because audio piled up. The sample rate has to be an even multiple of 48 kHz (960 kS/s, 1.92 MS/s, 2.4 MS/s ...); the plugin says so when it is not.

<h3>Carrier boxes</h3>

One box per carrier: the network and the channel number, the offset of the carrier from the center of the stream in kHz, the milliseconds of audio in its queue and how many times the queue ran empty. The fill should stay near the lead. A queue that runs empty over and over means that the device takes audio faster than the receiver delivers it (or the lead is too small): the carrier then has gaps and phones lose frames.

<h2>Notes</h2>

* The carriers of one device share its output: the more carriers, the less power each one has.
* Transmitting needs a licence. Use a dummy load or an attenuator for tests with a receiver, and check that the frequencies are free before using an antenna.
