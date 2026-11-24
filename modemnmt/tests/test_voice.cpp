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

// The speech path: filters, supervisory tone, resampling, and the supervision of a call.

#include <cmath>
#include <functional>

#include "compandor.h"
#include "phone.h"

static const double kPi = 3.14159265358979323846;

// Amplitude of a sine at the frequency in x[from, to), Hamming windowed.
static double toneAmplitude(const std::vector<float>& x, size_t from, size_t to, double hz, double rate)
{
    double re = 0.0, im = 0.0, sum = 0.0;
    const double n = static_cast<double>(to - from);

    for (size_t i = from; i < to; i++)
    {
        const double w = 0.54 - 0.46 * std::cos(2.0 * kPi * static_cast<double>(i - from) / n);
        const double phase = 2.0 * kPi * hz * static_cast<double>(i) / rate;

        re += w * x[i] * std::cos(phase);
        im -= w * x[i] * std::sin(phase);
        sum += w;
    }

    return 2.0 * std::sqrt(re * re + im * im) / sum;
}

static std::vector<float> sine(double hz, double amplitude, double rate, size_t count)
{
    std::vector<float> x(count);

    for (size_t i = 0; i < count; i++) {
        x[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
    }
    return x;
}

template <typename Filter>
static double gainAt(Filter& filter, double hz, double rate)
{
    const size_t count = static_cast<size_t>(rate);
    double power = 0.0;
    size_t used = 0;

    // amplitude from the power of the second half: the peak of few samples per cycle depends on
    // the phase
    filter.reset();
    for (size_t i = 0; i < count; i++)
    {
        const double y = filter.process(std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
        if (i > count / 2)
        {
            power += y * y;
            used++;
        }
    }
    return std::sqrt(2.0 * power / static_cast<double>(used));
}

static double db(double gain)
{
    return 20.0 * std::log10(gain);
}

static void testFilters()
{
    const double fs = 48000.0;

    BiquadHighpass hp(300.0, fs, 4);
    CHECK(std::fabs(db(gainAt(hp, 300.0, fs)) + 3.01) < 0.3);
    CHECK(db(gainAt(hp, 3000.0, fs)) > -0.1);
    CHECK(db(gainAt(hp, 30.0, fs)) < -60.0);

    BiquadLowpass lp(3400.0, fs, 8);
    CHECK(std::fabs(db(gainAt(lp, 3400.0, fs)) + 3.01) < 0.3);
    CHECK(db(gainAt(lp, 1000.0, fs)) > -0.1);
    CHECK(db(gainAt(lp, 6000.0, fs)) < -40.0);
}

// Feeds a tone to a detector for a number of seconds, returns the seconds until the first
// detection (or -1) and the final state.
struct DetectorRun
{
    double detectedAt = -1.0;
    double lostAt = -1.0;
    bool detected = false;
};

static DetectorRun runDetector(SupervisoryDetector& detector, const std::vector<float>& x, double rate)
{
    DetectorRun run;
    size_t pos = 0;

    detector.setSink([&](bool tone, double) {
        const double t = static_cast<double>(pos) / rate;
        if (tone && run.detectedAt < 0.0) {
            run.detectedAt = t;
        }
        if (!tone) {
            run.lostAt = t;
        }
    });
    while (pos < x.size())
    {
        const size_t n = std::min<size_t>(480, x.size() - pos);
        pos += n;
        detector.process(x.data() + pos - n, static_cast<int>(n));
    }
    run.detected = detector.detected();
    return run;
}

static void testSupervisoryDetector()
{
    const double rates[] = {16000.0, 44100.0, 48000.0};

    for (double fs : rates)
    {
        for (int number = 1; number <= 4; number++)
        {
            for (int other = 1; other <= 4; other++)
            {
                SupervisoryDetector detector(fs, number);
                const std::vector<float> x = sine(kSupervisoryHz[other - 1], 0.0249, fs, static_cast<size_t>(fs * 2.0));
                const DetectorRun run = runDetector(detector, x, fs);

                if (number == other)
                {
                    // 6 windows of 1/15 s, a window more for the start
                    CHECK(run.detected);
                    CHECK(run.detectedAt > 0.39 && run.detectedAt < 0.48);
                }
                else {
                    CHECK(!run.detected);       // the neighbour tones 30 Hz away are blocked
                }
            }
        }
    }

    const double fs = 48000.0;

    // lost after 4 windows without the tone
    {
        SupervisoryDetector detector(fs, 2);
        std::vector<float> x = sine(kSupervisoryHz[1], 0.0249, fs, static_cast<size_t>(fs * 1.0));
        x.resize(static_cast<size_t>(fs * 2.0), 0.0f);
        const DetectorRun run = runDetector(detector, x, fs);
        CHECK(run.detectedAt > 0.0);
        CHECK(!run.detected);
        CHECK(run.lostAt > 1.26 && run.lostAt < 1.34);       // 4 windows after the end of the tone
    }

    // speech at the level of the tone's neighbourhood does not fake it, noise does not either
    {
        SupervisoryDetector detector(fs, 3);
        std::vector<float> x = sine(1000.0, 0.8, fs, static_cast<size_t>(fs * 2.0));
        const std::vector<float> y = sine(3500.0, 0.3, fs, x.size());
        for (size_t i = 0; i < x.size(); i++) {
            x[i] += y[i];
        }
        CHECK(!runDetector(detector, x, fs).detected);
    }
    {
        SupervisoryDetector detector(fs, 3);
        std::vector<float> x(static_cast<size_t>(fs * 3.0));
        unsigned seed = 12345;
        for (float& v : x)
        {
            seed = seed * 1664525u + 1013904223u;
            v = static_cast<float>((static_cast<double>(seed >> 8) / 8388608.0 - 1.0) * 0.5);
        }
        CHECK(!runDetector(detector, x, fs).detected);
    }

    // a tone with speech on top is still found
    {
        SupervisoryDetector detector(fs, 3);
        std::vector<float> x = sine(kSupervisoryHz[2], 0.0249, fs, static_cast<size_t>(fs * 2.0));
        const std::vector<float> speech = sine(700.0, 0.5, fs, x.size());
        for (size_t i = 0; i < x.size(); i++) {
            x[i] += speech[i];
        }
        CHECK(runDetector(detector, x, fs).detected);
    }

    // switched off
    {
        SupervisoryDetector detector(fs, 0);
        const std::vector<float> x = sine(kSupervisoryHz[0], 0.0249, fs, static_cast<size_t>(fs));
        CHECK(!detector.enabled());
        CHECK(!runDetector(detector, x, fs).detected);
    }
}

static void testToneGenerator()
{
    const double fs = 48000.0;

    for (int number = 1; number <= 4; number++)
    {
        SupervisoryTone tone(fs, number, 1.0);
        std::vector<float> x(static_cast<size_t>(fs), 0.0f);
        tone.add(x.data(), 1000);
        tone.add(x.data() + 1000, static_cast<int>(x.size()) - 1000);       // continuous over calls
        const double f = kSupervisoryHz[number - 1];
        CHECK(std::fabs(toneAmplitude(x, 0, x.size(), f, fs) - kSupervisoryDeviation / kSpeechDeviationHz / Emphasis::preGain(f, fs)) < 0.0005);
        // on air, after the pre-emphasis of the transmitter: 0.3 kHz deviation (NMT Doc 450-4 4.8)
        std::vector<float> air = x;
        Emphasis emphasis(fs);
        emphasis.pre(air.data(), static_cast<int>(air.size()));
        CHECK(std::fabs(toneAmplitude(air, air.size() / 2, air.size(), f, fs) * 3000.0 - 300.0) < 3.0);
        for (int other = 1; other <= 4; other++)
        {
            if (other != number) {
                CHECK(toneAmplitude(x, 0, x.size(), kSupervisoryHz[other - 1], fs) < 0.001);
            }
        }
    }

    // 0.3 kHz at 4015 Hz against 3 kHz at 1 kHz speech
    CHECK(std::fabs(kTxPeakSupervisory - 0.0249066) < 1e-6);
    SupervisoryTone off(fs, 0, 1.0);
    std::vector<float> x(100, 0.5f);
    off.add(x.data(), 100);
    CHECK(x[0] == 0.5f && x[99] == 0.5f);
    SupervisoryTone half(fs, 1, 0.5);
    std::vector<float> y(4800, 0.0f);
    half.add(y.data(), 4800);
    CHECK(std::fabs(toneAmplitude(y, 0, y.size(), kSupervisoryHz[0], fs)
        - kSupervisoryDeviation / kSpeechDeviationHz / Emphasis::preGain(kSupervisoryHz[0], fs) * 0.5) < 0.0005);
}

static void testRxChain()
{
    const double rates[] = {8000.0, 16000.0, 44100.0, 48000.0};

    for (double fs : rates)
    {
        // speech comes through, the supervisory tone does not
        RxAudioChain chain(fs);
        std::vector<float> out;
        int blocks = 0;
        chain.setSink([&](const float* s, int n) {
            CHECK(n == kCallBlock);
            out.insert(out.end(), s, s + n);
            blocks++;
        });
        std::vector<float> x = sine(1000.0, 0.4, fs, static_cast<size_t>(fs * 2.0));
        if (fs >= 12000.0)
        {
            const std::vector<float> sat = sine(kSupervisoryHz[2], 0.0249, fs, x.size());
            for (size_t i = 0; i < x.size(); i++) {
                x[i] += sat[i];
            }
        }
        chain.process(x.data(), static_cast<int>(x.size()), 0);

        CHECK(blocks >= 99 && blocks <= 101);                   // 2 s of 20 ms blocks
        const size_t from = out.size() / 2;
        CHECK(std::fabs(toneAmplitude(out, from, out.size(), 1000.0, kCallRate) - gatedAmplitude(0.4)) < 0.03);
        if (fs >= 12000.0) {
            CHECK(toneAmplitude(out, from, out.size(), 4015.0, kCallRate) < 0.002);
        }
    }

    // muted: the first samples of the buffer are silence
    RxAudioChain chain(48000.0);
    std::vector<float> got;
    chain.setSink([&](const float* s, int n) { got.insert(got.end(), s, s + n); });
    const std::vector<float> x = sine(1000.0, 0.4, 48000.0, 48000);
    chain.process(x.data(), 24000, 24000);
    CHECK(got.size() == 4000);                                  // 0.5 s of 8 kHz = 25 blocks
    for (float v : got) {
        CHECK(std::fabs(v) < 1e-6f);
    }

    // bad samples are silence; the filters stay usable
    std::vector<float> bad = sine(1000.0, 0.4, 48000.0, 48000);
    bad[100] = std::nanf("");
    bad[200] = INFINITY;
    got.clear();
    chain.reset();
    chain.process(bad.data(), 48000, 0);
    CHECK(got.size() == 8000);                                  // 1 s of 8 kHz
    for (float v : got) {
        CHECK(std::isfinite(v));
    }
    CHECK(std::fabs(toneAmplitude(got, 4000, 8000, 1000.0, kCallRate) - gatedAmplitude(0.4)) < 0.02);

    // the noise gate (osmocom-analog's NMT and AMPS): the room below the threshold (0.05, -26 dB)
    // does not reach the call; speech above it does, the threshold taken off and made up
    const std::vector<float> room = sine(1000.0, 0.05, 48000.0, 48000);
    got.clear();
    chain.reset();
    chain.process(room.data(), 48000, 0);
    CHECK(toneAmplitude(got, 4000, 8000, 1000.0, kCallRate) < 1e-4);
    CHECK(gatedAmplitude(0.4) > 0.3 && gatedAmplitude(0.4) < 0.4);

    // 100 ms are 800 samples, 5 blocks
    got.clear();
    chain.reset();
    chain.process(x.data(), 4800, 0);
    CHECK(got.size() == 800);

    // below 300 Hz and above 3.4 kHz is cut
    RxAudioChain wide(48000.0);
    std::vector<float> low, high;
    wide.setSink([&](const float* s, int n) { low.insert(low.end(), s, s + n); });
    const std::vector<float> l = sine(50.0, 0.5, 48000.0, 96000);
    wide.process(l.data(), 96000, true);
    CHECK(toneAmplitude(low, low.size() / 2, low.size(), 50.0, kCallRate) < 0.005);

    // NMT Doc 450-4 5.15: the band 300 Hz to 3.4 kHz passes (flat here, the de-emphasis is
    // elsewhere); the supervisory tones (4 kHz +-55 Hz) at least 30 dB down, hum gone too. A
    // stopped tone is fed 10 times the reference: nothing out of the gate (threshold 0.10) then
    // means more than 40 dB, the filter and not the gate. Each tone alone, the gate is not linear.
    struct Case { double hz; bool passes; };
    const Case cases[] = {{50.0, false}, {300.0, true}, {1000.0, true}, {3000.0, true}, {3400.0, true},
                          {kSupervisoryHz[0], false}, {kSupervisoryHz[1], false},
                          {kSupervisoryHz[2], false}, {kSupervisoryHz[3], false}};
    for (double rate : {44100.0, 48000.0})
    {
        for (const Case& c : cases)
        {
            RxAudioChain band(rate);
            std::vector<float> out;
            band.setSink([&](const float* s, int n) { out.insert(out.end(), s, s + n); });
            const std::vector<float> in = sine(c.hz, c.passes ? 1.0 : 10.0, rate, static_cast<size_t>(rate));
            band.process(in.data(), static_cast<int>(in.size()), 0);
            double peak = 0.0;
            for (size_t i = out.size() / 2; i < out.size(); i++) {
                peak = std::fmax(peak, std::fabs(out[i]));
            }
            if (c.passes) {     // flat within 0.5 dB (the standard allows -3 dB)
                const double gain = toneAmplitude(out, out.size() / 2, out.size(), c.hz, kCallRate) / gatedAmplitude(1.0);
                CHECK(std::fabs(20.0 * std::log10(gain)) < 0.5);
            } else {
                CHECK(peak < 0.001);
            }
        }
    }
}

static void testTxSource()
{
    const double rates[] = {8000.0, 16000.0, 44100.0, 48000.0};

    for (double fs : rates)
    {
        TxAudioSource source(fs);
        double phase = 0.0;
        int pulls = 0;
        source.setPull([&](float* s, int n) {
            CHECK(n == kCallBlock);
            for (int i = 0; i < n; i++)
            {
                s[i] = static_cast<float>(0.6 * std::sin(2.0 * kPi * phase));
                phase += 1000.0 / kCallRate;
            }
            pulls++;
        });

        std::vector<float> a(static_cast<size_t>(fs * 2.0));
        // blocks of odd sizes
        size_t pos = 0;
        size_t step = 1;
        while (pos < a.size())
        {
            const size_t n = std::min(step, a.size() - pos);
            source.generate(a.data() + pos, static_cast<int>(n), false, 1.0);
            pos += n;
            step = step * 3 + 1;
            if (step > 5000) {
                step = 7;
            }
        }
        CHECK(pulls >= 99 && pulls <= 102);                     // 16000 call samples in 160 blocks

        CHECK(std::fabs(toneAmplitude(a, a.size() / 2, a.size(), 1000.0, fs) - 0.6) < 0.03);
        // no images of the interpolation near the old Nyquist frequency: 55 dB below the tone
        if (fs >= 16000.0) {
            CHECK(toneAmplitude(a, a.size() / 2, a.size(), 7000.0, fs) < 0.6 * 1.78e-3);
            CHECK(toneAmplitude(a, a.size() / 2, a.size(), 9000.0, fs) < 0.6 * 1.78e-3);
        }
    }

    // gain and add
    TxAudioSource source(48000.0);
    source.setPull([](float* s, int n) {
        for (int i = 0; i < n; i++) {
            s[i] = 0.5f;
        }
    });
    std::vector<float> a(4800, 0.0f), b(4800, 1.0f);
    source.generate(a.data(), 4800, false, 0.5);
    CHECK(std::fabs(a[4799] - 0.25f) < 0.01f);
    source.generate(b.data(), 4800, true, 2.0);
    CHECK(std::fabs(b[4799] - 2.0f) < 0.02f);

    // without a source: silence
    TxAudioSource none(48000.0);
    std::vector<float> c(480, 3.0f);
    none.generate(c.data(), 480, false, 1.0);
    for (float v : c) {
        CHECK(v == 0.0f);
    }
}

// A call that is connected, with the speech handlers of a test.
struct VoiceCall
{
    modemcellular::SampleClock clock;
    NmtSystem system;
    NmtChannel* tc;
    Calls calls;
    std::vector<float> fromPhone;       // what the phone said, 8 kHz
    double toneHz = 800.0;
    double toneAmp = 0.5;
    double tonePhase = 0.0;
    bool speak = true;
    int pulls = 0;
    Phone phone;

    VoiceCall(int supervisory, double rate = kRate, double factor = 1.0, bool compandor = false) :
        clock(rate), system(clock), tc(nullptr)
    {
        ChannelConfig c = config(System::Nmt450, ChannelType::Tc, 100);
        c.supervisory = supervisory;
        c.compandor = compandor;
        tc = &system.addChannel(c, rate, factor);
        connect(system, calls);
        system.setAudioHandlers(
            [this](const NmtSubscriber&, const float* s, int n) { fromPhone.insert(fromPhone.end(), s, s + n); },
            [this](const NmtSubscriber&, float* s, int n) {
                pulls++;
                for (int i = 0; i < n && speak; i++)
                {
                    s[i] = static_cast<float>(toneAmp * std::sin(2.0 * kPi * tonePhase));
                    tonePhase += toneHz / kCallRate;
                }
            });

        toDialTone(*tc, phone);
        dial(*tc, phone, "12");
        phoneSends(*tc, idleFromPhone(), 1);
        // address complete, then compandor in (idle frames without the compandor)
        for (int i = 0; i < 8; i++)
        {
            const Frame f = pull(*tc);
            if (i < 4) {
                CHECK(isLineSignal(f, 6));
            } else if (compandor) {
                CHECK(isLineSignal(f, 5));
            } else {
                CHECK(f.mt == MessageType::Frame6);
            }
        }
        FrameBits bits;
        CHECK(!tc->nextFrame(bits));
        CHECK(tc->state() == State::Active);
    }

    // Runs the radio for some seconds: the phone's audio goes in, the channel's comes out.
    std::vector<float> air(double seconds, const std::function<void(std::vector<float>&, size_t)>& phoneAudio, double rate = kRate)
    {
        const int block = static_cast<int>(rate / 100.0);
        std::vector<float> sent;
        std::vector<float> in(static_cast<size_t>(block)), out(static_cast<size_t>(block));
        const size_t blocks = static_cast<size_t>(seconds * 100.0 + 0.5);

        for (size_t b = 0; b < blocks; b++)
        {
            std::fill(in.begin(), in.end(), 0.0f);
            if (phoneAudio) {
                phoneAudio(in, b * static_cast<size_t>(block));
            }
            tc->receive(in.data(), block);
            tc->render(out.data(), block, false);
            sent.insert(sent.end(), out.begin(), out.end());
            clock.advance(block);
            system.run();
        }
        return sent;
    }
};

static std::function<void(std::vector<float>&, size_t)> phoneTones(double speechHz, double speechAmp, int satNumber, double rate)
{
    return [=](std::vector<float>& in, size_t first) {
        for (size_t i = 0; i < in.size(); i++)
        {
            const double t = static_cast<double>(first + i) / rate;
            double v = speechAmp * std::sin(2.0 * kPi * speechHz * t);
            if (satNumber) {
                v += kTxPeakSupervisory * std::sin(2.0 * kPi * kSupervisoryHz[satNumber - 1] * t);
            }
            in[i] += static_cast<float>(v);
        }
    };
}

static void testSpeechBothWays()
{
    for (double rate : {16000.0, 48000.0})
    {
        VoiceCall call(3, rate, 0.8);
        std::vector<float> tapped;

        // the tap hears what the handler gets, before it
        call.system.setAudioTap([&tapped](const NmtSubscriber&, const float *samples, int count) {
            tapped.insert(tapped.end(), samples, samples + count);
        });
        const std::vector<float> sent = call.air(2.0, phoneTones(1000.0, 0.4, 3, rate), rate);
        call.system.setAudioTap(nullptr);
        CHECK(!tapped.empty() && tapped == call.fromPhone);

        // towards the phone: the speech at the level of the endpoint (times the deviation
        // factor) and the supervisory tone of the channel
        const size_t from = sent.size() / 2;
        CHECK(std::fabs(toneAmplitude(sent, from, sent.size(), 800.0, rate) - 0.5 * 0.8) < 0.01);
        CHECK(std::fabs(toneAmplitude(sent, from, sent.size(), kSupervisoryHz[2], rate)
            - kSupervisoryDeviation / kSpeechDeviationHz / Emphasis::preGain(kSupervisoryHz[2], rate) * 0.8) < 0.0007);
        CHECK(toneAmplitude(sent, from, sent.size(), kSupervisoryHz[1], rate) < 0.001);       // not the neighbour
        CHECK(call.pulls > 90);

        // from the phone: the speech, without the supervisory tone, in 8 kHz
        CHECK(call.fromPhone.size() >= 15000 && call.fromPhone.size() <= 16100);
        const size_t half = call.fromPhone.size() / 2;
        CHECK(std::fabs(toneAmplitude(call.fromPhone, half, call.fromPhone.size(), 1000.0, kCallRate) - gatedAmplitude(0.4)) < 0.03);
        CHECK(toneAmplitude(call.fromPhone, half, call.fromPhone.size(), 4015.0, kCallRate) < 0.003);

        // the tone of the phone was heard and the call is still up
        CHECK(call.tc->supervisoryDetected());
        CHECK(call.tc->state() == State::Active);
    }
}

// The noise gate's products do not fold into the band (it runs at 48 kHz as in osmocom-analog's
// NMT; at 16 kHz the 5th and 7th products of 1.8 to 3.3 kHz came back 23 dB below the tone).
static void testGateProducts()
{
    for (double hz : {1800.0, 2600.0, 3300.0})
    {
        RxAudioChain chain(kRate);
        std::vector<float> out;
        chain.setSink([&](const float* s, int n) { out.insert(out.end(), s, s + n); });
        const std::vector<float> in = sine(hz, 0.4, kRate, static_cast<size_t>(kRate));
        chain.process(in.data(), static_cast<int>(in.size()), 0);

        const double tone = toneAmplitude(out, out.size() / 2, out.size(), hz, kCallRate);
        double worst = 0.0;
        for (double g = 100.0; g < 3900.0; g += 10.0)
        {
            if (std::fabs(g - hz) >= 30.0) {
                worst = std::max(worst, toneAmplitude(out, out.size() / 2, out.size(), g, kCallRate));
            }
        }
        CHECK(20.0 * std::log10(worst / tone) < -38.0);
    }
}

// The deviation limiter: loud speech is scaled down so that after the pre-emphasis it stays within
// the largest deviation with the supervisory tone; speech at a normal level is not touched.
static void testLimiter()
{
    // the limiter itself: a loud 2.5 kHz tone (pre-emphasis lifts it about 2.5 times)
    // (osmocom-analog's NMT: 4.7 kHz less the supervisory tone, with a margin of 5%, divided by the
    // transmitter's gain after the limiter)
    SpeechLimiter limiter(kRate, true);
    CHECK(std::fabs(limiter.limit() - (4700.0 - 300.0) / 3000.0 * 0.95) < 1e-9);
    CHECK(std::fabs(SpeechLimiter(kRate, false).limit() - 4700.0 / 3000.0 * 0.95) < 1e-9);
    CHECK(std::fabs(SpeechLimiter(kRate, true, 0.8).limit() - (4700.0 - 300.0) / 3000.0 / 0.8 * 0.95) < 1e-9);
    Emphasis check(kRate);
    std::vector<float> block(480);
    double peak = 0.0;
    double phase = 0.0;
    for (int b = 0; b < 100; b++)
    {
        for (float& s : block)
        {
            s = static_cast<float>(1.2 * std::sin(phase));
            phase += 2.0 * 3.14159265358979323846 * 2500.0 / kRate;
        }
        limiter.process(block.data(), 480);
        check.pre(block.data(), 480);
        if (b > 10) {
            for (float s : block) {
                peak = std::max(peak, static_cast<double>(std::fabs(s)));
            }
        }
    }
    std::printf("limiter: loud 2.5 kHz after the pre-emphasis %.3f of speech level (limit %.3f), %llu blocks limited\n",
        peak, limiter.limit(), static_cast<unsigned long long>(limiter.limited()));
    CHECK(peak <= limiter.limit() * 1.01 && peak > limiter.limit() * 0.9);
    CHECK(limiter.limited() >= 90);

    // a quiet block passes as it is, delayed by the lookahead of 2 ms
    SpeechLimiter quiet(kRate, true);
    const size_t delay = static_cast<size_t>(kRate * SpeechLimiter::kLookaheadMs / 1000.0);
    std::vector<float> soft(480), copy;
    for (int i = 0; i < 480; i++) {
        soft[static_cast<size_t>(i)] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / kRate));
    }
    copy = soft;
    quiet.process(soft.data(), 480);
    for (size_t i = 0; i < 480; i++) {
        CHECK(soft[i] == (i < delay ? 0.0f : copy[i - delay]));
    }
    CHECK(quiet.limited() == 0);

    // the gain comes back by 1 dB per 10 ms after a loud burst
    SpeechLimiter release(kRate, true);
    std::vector<float> burst(static_cast<size_t>(kRate * 0.2), 0.0f);
    for (size_t i = 0; i < 4800; i++) {
        burst[i] = static_cast<float>(3.0 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * static_cast<double>(i) / kRate));
    }
    for (size_t i = 4800; i < burst.size(); i++) {
        burst[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * static_cast<double>(i) / kRate));
    }
    release.process(burst.data(), static_cast<int>(burst.size()));
    auto peakAt = [&](double fromS) {
        double p = 0.0;
        for (size_t i = static_cast<size_t>(fromS * kRate); i < static_cast<size_t>((fromS + 0.002) * kRate); i++) {
            p = std::max(p, static_cast<double>(std::fabs(burst[i])));
        }
        return p;
    };
    // The burst needed 1.393 / 3.0 = -6.7 dB (the pre-emphasis is 0 dB at 1 kHz). About 13 ms
    // after it (the lookahead is 2 ms) the gain has come back by only about 1.3 dB: the quiet
    // tone is near 0.27 (an instant release would give 0.5); 90 ms later it is through again.
    CHECK(peakAt(0.115) > 0.22 && peakAt(0.115) < 0.32);
    CHECK(std::fabs(peakAt(0.19) - 0.5) < 0.01);

    // in a call: what goes to the radio stays within 4.7 kHz after the pre-emphasis, tone included
    VoiceCall call(3);
    call.toneHz = 2500.0;
    call.toneAmp = 1.0;
    std::vector<float> sent = call.air(2.0, nullptr);
    Emphasis radio(kRate);
    radio.pre(sent.data(), static_cast<int>(sent.size()));
    double onAir = 0.0;
    for (size_t i = sent.size() / 2; i < sent.size(); i++) {
        onAir = std::max(onAir, static_cast<double>(std::fabs(sent[i])));
    }
    std::printf("limiter: loud call after the pre-emphasis %.0f Hz deviation\n", onAir * 3000.0);
    CHECK(onAir * 3000.0 <= 4700.0 * 1.01);
    CHECK(call.tc->speechLimited() > 50);

    // normal speech in a call is not limited
    VoiceCall normal(3);
    normal.air(2.0, nullptr);
    CHECK(normal.tc->speechLimited() == 0);
}

static void testNoSupervisory()
{
    // without a supervisory tone on the channel nothing is added, and nothing supervises
    VoiceCall call(0);
    const std::vector<float> sent = call.air(1.0, phoneTones(1000.0, 0.4, 0, kRate));

    CHECK(toneAmplitude(sent, sent.size() / 2, sent.size(), 4015.0, kRate) < 0.001);
    CHECK(!call.tc->supervisoryDetected());
    call.air(60.0, nullptr);
    CHECK(call.tc->state() == State::Active);
    CHECK(call.calls.cleared == 0);
}

// Amplitude of a steady tone at 8 kHz after the compressor or the expander.
static double companded(double hz, double amplitude, bool compress)
{
    modemcellular::Compandor compandor(kCallRate);
    std::vector<float> x = sine(hz, amplitude, kCallRate, 16000);

    if (compress) {
        compandor.compress(x.data(), static_cast<int>(x.size()));
    } else {
        compandor.expand(x.data(), static_cast<int>(x.size()));
    }
    return toneAmplitude(x, x.size() / 2, x.size(), hz, kCallRate);
}

static void testCompandor()
{
    const double rate = 48000.0;
    const double toPhone = companded(800.0, 0.5, true);
    // from the phone the noise gate comes before the expander
    const double fromPhone = companded(1000.0, gatedAmplitude(0.4), false);

    // the compressor lifts the quiet tone, the expander lowers it
    CHECK(toPhone > 0.6);
    CHECK(fromPhone < 0.3);

    VoiceCall call(3, rate, 0.8, true);
    const std::vector<float> sent = call.air(2.0, phoneTones(1000.0, 0.4, 3, rate), rate);
    const size_t from = sent.size() / 2;

    // towards the phone compressed, times the deviation factor; the supervisory tone is not
    CHECK(std::fabs(toneAmplitude(sent, from, sent.size(), 800.0, rate) - toPhone * 0.8) < 0.02);
    CHECK(std::fabs(toneAmplitude(sent, from, sent.size(), kSupervisoryHz[2], rate)
        - kSupervisoryDeviation / kSpeechDeviationHz / Emphasis::preGain(kSupervisoryHz[2], rate) * 0.8) < 0.0007);

    // from the phone expanded
    const size_t half = call.fromPhone.size() / 2;
    CHECK(std::fabs(toneAmplitude(call.fromPhone, half, call.fromPhone.size(), 1000.0, kCallRate) - fromPhone) < 0.03);
    CHECK(call.tc->state() == State::Active);

    // without the option the same speech goes out plain
    VoiceCall plain(3, rate, 0.8, false);
    const std::vector<float> plainSent = plain.air(2.0, phoneTones(1000.0, 0.4, 3, rate), rate);
    CHECK(std::fabs(toneAmplitude(plainSent, plainSent.size() / 2, plainSent.size(), 800.0, rate) - 0.5 * 0.8) < 0.01);
}

static void testSupervisoryAlarm()
{
    // the phone never sends its tone: released 3 s after the connection
    {
        VoiceCall call(2);
        call.air(2.8, phoneTones(1000.0, 0.4, 0, kRate));
        CHECK(call.tc->state() == State::Active);
        call.air(0.3, phoneTones(1000.0, 0.4, 0, kRate));
        CHECK(call.tc->state() == State::MtRelease);
        CHECK(call.calls.cleared == 1);
        CHECK(call.tc->dspMode() == DspMode::Frame);
        CHECK(isLineSignal(pull(*call.tc), 15));
    }

    // the tone is there: the call stays, long after the 3 s
    {
        VoiceCall call(2);
        call.air(10.0, phoneTones(1000.0, 0.4, 2, kRate));
        CHECK(call.tc->state() == State::Active);
        CHECK(call.tc->supervisoryDetected());

        // the tone goes: the alarm runs for 20 s
        call.air(19.0, phoneTones(1000.0, 0.4, 0, kRate));
        CHECK(call.tc->state() == State::Active);
        CHECK(!call.tc->supervisoryDetected());
        call.air(1.5, phoneTones(1000.0, 0.4, 0, kRate));
        CHECK(call.tc->state() == State::MtRelease);
        CHECK(call.calls.cleared == 1);
    }

    // the tone comes back in time: the alarm is off
    {
        VoiceCall call(2);
        call.air(1.0, phoneTones(1000.0, 0.4, 2, kRate));
        call.air(15.0, phoneTones(1000.0, 0.4, 0, kRate));
        call.air(1.0, phoneTones(1000.0, 0.4, 2, kRate));
        call.air(30.0, phoneTones(1000.0, 0.4, 2, kRate));
        CHECK(call.tc->state() == State::Active);
    }

    // a frame from the phone restarts a running alarm: 15 s without the tone, a frame, 10 s more
    {
        VoiceCall call(2);
        call.air(1.0, phoneTones(1000.0, 0.4, 2, kRate));
        call.air(15.0, phoneTones(1000.0, 0.4, 0, kRate));
        phoneSends(*call.tc, lineSignalFrame(*call.tc, call.phone, 0x44444), 1);
        call.air(10.0, phoneTones(1000.0, 0.4, 0, kRate));
        CHECK(call.tc->state() == State::Active);               // 25 s since the tone went
        call.air(10.5, phoneTones(1000.0, 0.4, 0, kRate));
        CHECK(call.tc->state() == State::MtRelease);
    }

    // a frame from the phone while its tone is fine starts the 20 s timer as well; only a change
    // of the tone stops it, so the call is released 20 s later
    {
        VoiceCall call(2);
        call.air(1.0, phoneTones(1000.0, 0.4, 2, kRate));
        phoneSends(*call.tc, lineSignalFrame(*call.tc, call.phone, 0x44444), 1);
        call.air(19.5, phoneTones(1000.0, 0.4, 2, kRate));
        CHECK(call.tc->state() == State::Active);
        call.air(1.0, phoneTones(1000.0, 0.4, 2, kRate));
        CHECK(call.tc->state() == State::MtRelease);
    }

    // another tone is not ours
    {
        VoiceCall call(2);
        call.air(3.3, phoneTones(1000.0, 0.4, 3, kRate));
        CHECK(call.tc->state() == State::MtRelease);
    }
}

// Settings the speech path does not run with.
static void testConfigLimits()
{
    auto valid = [](ChannelType type, int supervisory, double rate) {
        ChannelConfig c = config(System::Nmt450, type, 100);
        c.supervisory = supervisory;
        return NmtChannel(c, rate, 1.0).valid();
    };

    CHECK(valid(ChannelType::Tc, 0, 8000.0));
    CHECK(valid(ChannelType::Tc, 4, 16000.0));
    CHECK(valid(ChannelType::Tc, 4, 12000.0));
    CHECK(!valid(ChannelType::Tc, 4, 8000.0));              // 4 kHz tones need more than 8 kHz
    CHECK(!valid(ChannelType::Tc, 3, 11025.0));
    CHECK(!valid(ChannelType::Tc, 5, 48000.0));
    CHECK(!valid(ChannelType::Tc, -1, 48000.0));
    CHECK(!valid(ChannelType::Cc, 1, 48000.0));             // only traffic channels have a tone
    CHECK(!valid(ChannelType::Test, 1, 48000.0));
    CHECK(valid(ChannelType::CcTc, 2, 48000.0));
    CHECK(valid(ChannelType::Cc, 0, 48000.0));

    // at the lowest rate (no room for the supervisory tone) the chain is stable
    RxAudioChain chain(8000.0);
    std::vector<float> got;
    chain.setSink([&](const float* s, int n) { got.insert(got.end(), s, s + n); });
    const std::vector<float> x = sine(1000.0, 0.4, 8000.0, 8000 * 60);
    chain.process(x.data(), static_cast<int>(x.size()), 0);
    float peak = 0.0f;
    for (float v : got) {
        peak = std::fmax(peak, std::fabs(v));
    }
    CHECK(peak < 0.6f);
}

// Every frame received in the conversation, of this phone or another one, starts the 20 s timer
// again: also within the 3 s for the first tone.
static void testAlarmAndFrames()
{
    // a frame in the first 3 s: the call is released 20 s after the frame
    for (int variant = 0; variant < 2; variant++)
    {
        VoiceCall call(2);
        Phone other = call.phone;
        other.number = "654321";

        call.air(1.0, nullptr);
        phoneSends(*call.tc, lineSignalFrame(*call.tc, variant == 0 ? call.phone : other, 0x44444), 1);
        call.air(2.2, nullptr);
        CHECK(call.tc->state() == State::Active);               // 3.2 s
        call.air(17.5, nullptr);
        CHECK(call.tc->state() == State::Active);               // 20.7 s
        call.air(0.6, nullptr);
        CHECK(call.tc->state() == State::MtRelease);            // 21.3 s
    }

    // alarm running: a frame of another phone restarts it too
    {
        VoiceCall call(2);
        Phone other = call.phone;
        other.number = "654321";

        call.air(1.0, phoneTones(1000.0, 0.4, 2, kRate));
        call.air(10.0, nullptr);                                // tone gone at about 1.3 s
        phoneSends(*call.tc, lineSignalFrame(*call.tc, other, 0x44444), 1);
        call.air(19.5, nullptr);
        CHECK(call.tc->state() == State::Active);               // 19.5 s after the frame
        call.air(1.0, nullptr);
        CHECK(call.tc->state() == State::MtRelease);
    }
}

// What a handler may do from inside the audio.
static void testAudioHandlerReentrancy()
{
    // the phone's audio handler releases the call: no more audio after that, even in the same buffer
    {
        VoiceCall call(0);
        int calls = 0;

        call.system.setAudioHandlers(
            [&](const NmtSubscriber& s, const float*, int) {
                calls++;
                CHECK(call.system.releaseCall(s.country, s.number));
            },
            [](const NmtSubscriber&, float*, int) {});
        std::vector<float> in(4800, 0.1f);                      // three blocks of 160 samples
        call.tc->receive(in.data(), 4800);
        CHECK(calls == 1);
        CHECK(call.tc->state() == State::MtRelease);
        CHECK(call.calls.cleared == 0);
        CHECK(isLineSignal(pull(*call.tc), 15));
    }

    // the handler for the audio towards the phone releases the call
    {
        VoiceCall call(0);

        call.system.setAudioHandlers(
            [](const NmtSubscriber&, const float*, int) {},
            [&](const NmtSubscriber& s, float*, int) { call.system.releaseCall(s.country, s.number); });
        std::vector<float> out(4800);
        call.tc->render(out.data(), 4800, false);
        CHECK(call.tc->state() == State::MtRelease);
        CHECK(call.tc->dspMode() == DspMode::Frame);
        std::vector<float> next(480);
        CHECK(call.tc->render(next.data(), 480, false) > 0);    // frames now
        CHECK(isLineSignal(pull(*call.tc), 15));
    }
}

// A bad sample does not poison the speech path of the next call.
static void testBadSamplesAndNextCall()
{
    VoiceCall call(2);

    call.air(1.0, phoneTones(1000.0, 0.4, 2, kRate));
    std::vector<float> in(480, 0.1f);
    in[100] = std::nanf("");
    in[101] = INFINITY;
    call.tc->receive(in.data(), 480);
    call.fromPhone.clear();
    call.air(1.0, phoneTones(1000.0, 0.4, 2, kRate));
    for (float v : call.fromPhone) {
        CHECK(std::isfinite(v));
    }
    CHECK(std::fabs(toneAmplitude(call.fromPhone, call.fromPhone.size() / 2, call.fromPhone.size(), 1000.0, kCallRate) - gatedAmplitude(0.4)) < 0.02);

    // bad audio from the call
    call.system.setAudioHandlers(
        [](const NmtSubscriber&, const float*, int) {},
        [](const NmtSubscriber&, float* s, int n) {
            for (int i = 0; i < n; i++) {
                s[i] = (i == 5) ? std::nanf("") : 0.3f;
            }
        });
    const std::vector<float> sent = call.air(0.5, phoneTones(1000.0, 0.4, 2, kRate));
    for (float v : sent) {
        CHECK(std::isfinite(v));
    }

    // the call ends; a second call on the channel does not inherit the detection
    phoneSends(*call.tc, lineSignalFrame(*call.tc, call.phone, 0x11111), 1);
    CHECK(call.tc->state() == State::MoRelease);
    CHECK(pullClearing(*call.tc) == 4);
    CHECK(call.tc->state() == State::Idle);
    CHECK(!call.tc->supervisoryDetected());
}

// The call meters: the speech from the phone as demodulated (before the expander), the speech
// to the phone after the compressor, on the reference scale; the last call's stay after it ends
// and start again with the next.
static void testSpeechMeters()
{
    VoiceCall call(2);

    call.toneHz = 1500.0;
    call.air(3.0, phoneTones(1000.0, 0.4, 2, kRate));
    NmtChannel::Speech s = call.tc->speech();
    CHECK(s.call == 1 && s.live);
    CHECK(s.country == call.phone.country && s.number == call.phone.number);
    // from the phone: the 1 kHz tone as the receiver passes it, in the 800-1200 Hz band
    const double rxDb = 20.0 * std::log10(gatedAmplitude(0.4) / std::sqrt(2.0));
    CHECK(std::fabs(s.rxCall.levelDb() - rxDb) < 0.5);
    CHECK(s.rxCall.bandsDb()[2] > -1.5);
    CHECK(s.rxRecent.speech > 0 && s.rxRecent.speech <= s.rxCall.speech);
    // to the phone: the 1.5 kHz tone of 0.5 from the call (-9.0 dB rms), in the 1.2-2 kHz band
    CHECK(std::fabs(s.txCall.levelDb() - 20.0 * std::log10(0.5 / std::sqrt(2.0))) < 0.3);
    CHECK(s.txCall.bandsDb()[3] > -1.5);
    CHECK(s.rxCall.atLimit == 0 && s.txCall.atLimit == 0 && s.limited == 0);
    std::printf("call meters: from the phone %.1f dB (expected %.1f), to the phone %.1f dB, pauses %.0f %% / %.0f %%\n",
        s.rxCall.levelDb(), rxDb, s.txCall.levelDb(), 100.0 * s.rxCall.pauseShare(), 100.0 * s.txCall.pauseShare());

    // with the compandor the receive side is measured before the expander (unchanged), the
    // transmit side after the compressor (changed)
    {
        // a quiet tone (-23 dB) comes out of the 2:1 compressor near -11.5 dB
        VoiceCall companded(2, kRate, 1.0, true);
        companded.toneHz = 1000.0;
        companded.toneAmp = 0.1;
        companded.air(3.0, phoneTones(1000.0, 0.4, 2, kRate));
        const NmtChannel::Speech c = companded.tc->speech();
        std::printf("with the compandor: from the phone %.1f dB, to the phone %.1f dB (in -23.0)\n", c.rxCall.levelDb(), c.txCall.levelDb());
        CHECK(std::fabs(c.rxCall.levelDb() - s.rxCall.levelDb()) < 0.3);
        CHECK(c.txCall.levelDb() > -18.0);
    }

    // silence from the phone: pause in the recent window, the call keeps its speech
    call.air(2.5, nullptr);
    s = call.tc->speech();
    CHECK(s.rxRecent.speech == 0 && s.rxRecent.pause > 0);
    CHECK(s.rxCall.speech > 0);

    // the call ends: the stats stay, not live
    phoneSends(*call.tc, lineSignalFrame(*call.tc, call.phone, 0x11111), 1);
    pullClearing(*call.tc);
    CHECK(call.tc->state() == State::Idle);
    s = call.tc->speech();
    CHECK(s.call == 1 && !s.live && s.rxCall.speech > 0);
}

// Frames from the phone mute the audio for a little more than two frame periods.
static void testMuteDuringFrames()
{
    VoiceCall call(0);
    NmtModem phoneModem(kRate, 1.0);
    bool sent = false;

    phoneModem.setFrameSource([&](FrameBits& bits) {
        if (sent) {
            return false;
        }
        sent = true;
        bits = encodeFrame(lineSignalFrame(*call.tc, call.phone, 0x44444));
        return true;
    });

    // 0.6 s of speech, then one frame on top of it, then speech again
    const double speechAmp = 0.4;
    const size_t frameAt = static_cast<size_t>(kRate * 0.6);
    const size_t frameEnd = frameAt + static_cast<size_t>(kRate * 166.0 / 1200.0);
    std::vector<float> frame(static_cast<size_t>(kRate * 0.2), 0.0f);
    phoneModem.render(frame.data(), static_cast<int>(frame.size()), true);       // add: silence outside

    call.air(1.8, [&](std::vector<float>& in, size_t first) {
        for (size_t i = 0; i < in.size(); i++)
        {
            const size_t k = first + i;
            // the phone mutes its own microphone while it sends a frame
            if (k < frameAt || k >= frameEnd) {
                in[i] += static_cast<float>(speechAmp * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(k) / kRate));
            }
            if (k >= frameAt && k - frameAt < frame.size()) {
                in[i] += frame[k - frameAt];
            }
        }
    });

    const std::vector<float>& heard = call.fromPhone;
    auto level = [&](double fromS, double toS) {
        return toneAmplitude(heard, static_cast<size_t>(fromS * kCallRate), static_cast<size_t>(toS * kCallRate), 1000.0, kCallRate);
    };

    CHECK(std::fabs(level(0.2, 0.55) - gatedAmplitude(speechAmp)) < 0.03);          // before the frame
    // Speech is back on the line at 0.74 s; the channel stays muted until 0.28 s after the
    // frame sync (about 0.62 s), that is until 0.90 s.
    CHECK(level(0.78, 0.88) < 0.01);
    CHECK(std::fabs(level(1.1, 1.7) - gatedAmplitude(speechAmp)) < 0.03);           // speech is back
}

int main()
{
    testFilters();
    testSupervisoryDetector();
    testToneGenerator();
    testRxChain();
    testGateProducts();
    testTxSource();
    testConfigLimits();
    testSpeechBothWays();
    testLimiter();
    testNoSupervisory();
    testCompandor();
    testSupervisoryAlarm();
    testAlarmAndFrames();
    testAudioHandlerReentrancy();
    testBadSamplesAndNextCall();
    testMuteDuringFrames();
    testSpeechMeters();

    if (g_failures == 0) {
        std::printf("all voice tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
