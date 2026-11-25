///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
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

#include <algorithm>

#include "ampschannel.h"
#include "phone.h"

static const uint32_t kMin1 = ReccAssembler::min1Of("2177526");
static const uint16_t kMin2 = ReccAssembler::min2Of("216");

static std::vector<uint64_t> registrationWords()
{
    return {encodeWord(words::kReccA, {{"F", 1}, {"NAWC", 2}, {"T", 1}, {"S", 1}, {"E", 1}, {"SCM", 0xa}, {"MIN1", kMin1}}),
        encodeWord(words::kReccB, {{"NAWC", 1}, {"ORDQ", 3}, {"ORDER", kOrderRegistration}, {"MIN2", kMin2}}),
        encodeWord(words::kReccSerial, {{"NAWC", 0}, {"SERIAL", 0xe0ec3aac}})};
}

static std::vector<uint64_t> originationWords()
{
    return {encodeWord(words::kReccA, {{"F", 1}, {"NAWC", 3}, {"T", 1}, {"S", 1}, {"E", 1}, {"SCM", 0xa}, {"MIN1", kMin1}}),
        encodeWord(words::kReccB, {{"NAWC", 2}, {"ORDER", kOrderPage}, {"MIN2", kMin2}}),
        encodeWord(words::kReccSerial, {{"NAWC", 1}, {"SERIAL", 0xe0ec3aac}}),
        encodeWord(words::kReccDigits, {{"NAWC", 0}, {"DIGIT 1", 5}, {"DIGIT 2", 5}, {"DIGIT 3", 5}})};
}

struct Bench
{
    modemcellular::SampleClock clock{kRate};
    AmpsSystem system;
    AmpsChannel cc;
    AmpsChannel vc;
    AmpsChannel vc2;                    // 359, SAT colour code 1: the channel of a handoff
    SatTone phoneSatTone2{kRate, 1};    // the phone transponding its SAT there
    bool onVoice2 = false;
    FoccReader focc;
    PhoneTx phone;
    PhoneTx vcPhone;                    // the phone's messages on the RVC
    bool vcData = false;
    SatDetector phoneSat{kRate};        // the phone hears the SAT of the voice channel
    SatTone phoneSatTone{kRate, 0};     // and transponds it
    bool onVoice = false;
    bool tone = false;
    int heardSat = -2;
    std::vector<std::string> calls;
    int registrations = 0;
    int clears = 0;
    double toneAt = 0.0;
    double vc1kHz = 0.0;
    double vcPower = 0.0;
    double vc2Power = 0.0;
    double t = 0.0;

    static AmpsSystemConfig config()
    {
        AmpsSystemConfig c;
        c.si.sid = 54;
        c.channels = {{353, AmpsChannelType::ControlPaging, 0}, {356, AmpsChannelType::Voice, 0},
            {359, AmpsChannelType::Voice, 1}};
        return c;
    }

    Bench() :
        system(clock, config()),
        cc(system, 0, kRate),
        vc(system, 1, kRate),
        vc2(system, 2, kRate)
    {
        system.setRegistrationHandler([this](const AmpsSubscriber&, bool) { registrations++; });
        system.setCallHandler([this](const AmpsSubscriber&, const std::string& d) {
            calls.push_back(d);
            return true;
        });
        system.setClearHandler([this](const AmpsSubscriber&) { clears++; });
        // the network sends a 1 kHz tone at the speech level
        system.setAudioHandlers(nullptr, [this](const AmpsSubscriber&, float* s, int n) {
            for (int i = 0; i < n; i++) {
                s[i] = static_cast<float>(std::sin(2.0 * kPi * 1000.0 * (m_callSamples++) / 8000.0));
            }
        });
        phoneSat.setSatSink([this](int scc, double) { heardSat = scc; });
    }

    void run(double seconds)
    {
        std::vector<float> in(kBlock), out(kBlock);
        for (int b = 0; b < static_cast<int>(seconds * 100.0); b++)
        {
            // the phone towards the base
            phone.render(in.data(), kBlock);
            cc.receive(in.data(), kBlock);
            std::fill(in.begin(), in.end(), 0.0f);
            if (onVoice && (vcPhone.sending() || vcData))
            {
                // a message on the RVC in place of the SAT (blank and burst)
                vcData = vcPhone.render(in.data(), kBlock) > 0;
            }
            else if (onVoice)
            {
                phoneSatTone.add(in.data(), kBlock);
                for (int i = 0; tone && i < kBlock; i++) {
                    in[i] += static_cast<float>(kFskDeviation * std::sin(2.0 * kPi * kSignallingToneHz * (t + i / kRate)));
                }
            }
            vc.receive(in.data(), kBlock);
            std::fill(in.begin(), in.end(), 0.0f);
            if (onVoice2) {
                phoneSatTone2.add(in.data(), kBlock);
            }
            vc2.receive(in.data(), kBlock);
            // the base towards the phone
            cc.render(out.data(), kBlock);
            focc.process(out.data(), kBlock);
            vc2.render(out.data(), kBlock);
            vc2Power = 0.0;
            for (int i = 0; i < kBlock; i++) {
                vc2Power += out[i] * out[i] / kBlock;
            }
            vc.render(out.data(), kBlock);
            phoneSat.process(out.data(), kBlock);
            double re = 0.0, im = 0.0, p = 0.0;
            for (int i = 0; i < kBlock; i++)
            {
                re += out[i] * std::cos(2.0 * kPi * 1000.0 * (t + i / kRate));
                im += out[i] * std::sin(2.0 * kPi * 1000.0 * (t + i / kRate));
                p += out[i] * out[i];
            }
            vc1kHz = 2.0 * std::sqrt(re * re + im * im) / kBlock;
            vcPower = p / kBlock;
            clock.advance(kBlock);
            system.run();
            t += kBlock / kRate;
        }
    }

    uint64_t m_callSamples = 0;
};

// The whole of a phone's call through the channels: the overhead train, a registration and its
// acknowledgement, an origination, the designation, the voice channel with SAT, speech, the
// phone's release by its tone
static void testCall()
{
    Bench b;

    b.run(1.0);
    CHECK(heardWord(b.focc.words, [](uint64_t w) {
        return wordField(words::kSystemParameter1, w, "OHD") == kOhdSystemParameter1 && wordField(words::kSystemParameter1, w, "SID1") == 27;
    }));
    CHECK(heardWord(b.focc.words, [](uint64_t w) { return wordField(words::kControlFiller, w, "OHD") == kOhdControlFiller; }));

    b.phone.send(reccMessage(0, registrationWords()));
    b.run(1.0);
    CHECK(b.registrations == 1);
    CHECK(heardWord(b.focc.words, [](uint64_t w) {
        return wordField(words::kExtendedAddressOrder, w, "T1T2") == kT1T2Next && wordField(words::kExtendedAddressOrder, w, "SCC") == 3
            && wordField(words::kExtendedAddressOrder, w, "ORDER") == kOrderRegistration && wordField(words::kExtendedAddressOrder, w, "MIN2") == kMin2;
    }));

    b.focc.words.clear();
    b.phone.send(reccMessage(0, originationWords()));
    b.run(1.0);
    CHECK(b.calls.size() == 1 && !b.calls.empty() && b.calls[0] == "555");
    CHECK(heardWord(b.focc.words, [](uint64_t w) {
        return wordField(words::kExtendedAddressChannel, w, "T1T2") == kT1T2Next && wordField(words::kExtendedAddressChannel, w, "SCC") == 0
            && wordField(words::kExtendedAddressChannel, w, "CHAN") == 356;
    }));

    // the phone goes to the voice channel: it hears the SAT and transponds it
    b.run(0.5);
    CHECK(b.heardSat == 0);
    b.onVoice = true;
    b.run(1.0);
    CHECK(b.system.mode(1) == AmpsMode::Audio);
    // the network's 1 kHz at the speech level, about 1.27 of 2.9 kHz after gain, compressor and emphasis
    CHECK(b.vc1kHz > 0.9 * kSpeechDeviation && b.vc1kHz < 1.6 * kSpeechDeviation);

    // the meters of the call: the network's tone to the phone, from the phone only its SAT (notched
    // out: pause)
    AmpsChannel::Speech s = b.vc.speech();
    AmpsSubscriber phone;
    phone.min1 = kMin1;
    phone.min2 = kMin2;
    CHECK(s.call == 1 && s.live && s.number == phone.number());
    CHECK(s.txRecent.speech > 0 && s.txRecent.levelDb() > -3.0 && s.txRecent.levelDb() < 3.0);
    CHECK(s.rxRecent.speech == 0 && s.rxRecent.pause > 0);

    // a flash (its tone of 0.4 s): the network is told, a second later it asks for the called
    // address (Send Called-Address on the FVC); the phone sends the digits dialled on the RVC
    std::vector<std::string> flashes;
    std::vector<std::string> sent;
    b.system.setFlashHandler([&flashes](const AmpsSubscriber&, const std::string& d) { flashes.push_back(d); });
    b.vc.setTraceTap([&sent](bool toPhone, const std::string& text) {
        if (toPhone) {
            sent.push_back(text);
        }
    });
    b.tone = true;
    b.run(0.4);
    b.tone = false;
    b.run(2.0);                         // the tone's end is detected some 0.3 s late, then 1 s, then the order
    CHECK(flashes.size() == 1 && !flashes.empty() && flashes[0].empty());
    CHECK(std::find(sent.begin(), sent.end(), "Send Called-address") != sent.end());
    CHECK(b.system.mode(1) == AmpsMode::Audio);
    const uint64_t d1 = encodeWord(words::kRvcDigits, {{"F", 1}, {"NAWC", 1}, {"T", 0}, {"DIGIT 1", 5}, {"DIGIT 2", 5},
        {"DIGIT 3", 5}, {"DIGIT 4", 1}, {"DIGIT 5", 2}, {"DIGIT 6", 3}, {"DIGIT 7", 4}, {"DIGIT 8", 5}});
    const uint64_t d2 = encodeWord(words::kRvcDigits, {{"F", 0}, {"NAWC", 0}, {"T", 0}, {"DIGIT 1", 6}});
    b.vcPhone.send(rvcMessage({d1, d2}));
    b.run(0.5);
    CHECK(flashes.size() == 2 && flashes.size() == 2 && flashes[1] == "555123456");
    CHECK(b.clears == 0 && b.system.mode(1) == AmpsMode::Audio);

    // its tone of 1.7 s releases the call, the voice channel goes off
    b.tone = true;
    b.run(1.7);
    b.tone = false;
    b.run(0.6);
    CHECK(b.clears == 1 && b.system.mode(1) == AmpsMode::Off);
    // the call ended; its meters stay readable
    s = b.vc.speech();
    CHECK(s.call == 1 && !s.live && s.txCall.speech > 0);
    b.onVoice = false;
    b.run(0.1);
    CHECK(b.vcPower == 0.0);
}

// A handoff through the channels: the new channel's SAT on the air, the order on the old channel,
// the phone leaves it and transponds the new SAT (colour code 1) on 359: the detector there moves
// the call, the old channel goes off
static void testHandoff()
{
    Bench b;
    std::vector<std::string> sent;

    b.phone.send(reccMessage(0, originationWords()));
    b.run(1.5);
    b.onVoice = true;
    b.run(1.0);
    CHECK(b.system.mode(1) == AmpsMode::Audio && b.vc2Power == 0.0);
    b.vc.setTraceTap([&sent](bool toPhone, const std::string& text) {
        if (toPhone) {
            sent.push_back(text);
        }
    });
    AmpsSubscriber phone;
    phone.min1 = kMin1;
    phone.min2 = kMin2;
    CHECK(b.system.handoff(phone.number(), 359) == AmpsSystem::OrderResult::Sent);
    b.run(0.3);
    CHECK(std::find(sent.begin(), sent.end(), "Handoff: CHAN 359, SCC 1, VMAC 4") != sent.end());
    CHECK(b.vc2Power > 0.0);                            // the new channel's SAT is on the air
    b.onVoice = false;
    b.onVoice2 = true;
    b.run(1.0);
    const std::vector<AmpsSystem::TransactionInfo> t = b.system.transactions();
    CHECK(t.size() == 1 && !t.empty() && t[0].state == AmpsSystem::State::Call && t[0].channel == 2);
    CHECK(b.system.mode(1) == AmpsMode::Off && b.system.mode(2) == AmpsMode::Audio && b.clears == 0);
}

// The combined channel (CC/PC/VC) through one AmpsChannel: the overhead train, the origination on
// the RECC, its own number designated, then the voice channel of the call (the phone's SAT on the
// same channel), the release tone, and the control channel again
static void testCombined()
{
    modemcellular::SampleClock clock{kRate};
    AmpsSystemConfig c;
    c.si.sid = 54;
    c.channels = {{353, AmpsChannelType::ControlPagingVoice, 0}};
    AmpsSystem system(clock, c);
    AmpsChannel ch(system, 0, kRate);
    FoccReader focc;
    PhoneTx phone;
    SatTone phoneSat{kRate, 0};
    bool onVoice = false;
    bool tone = false;
    int clears = 0;
    double t = 0.0;
    std::vector<std::string> sent;
    std::vector<float> in(kBlock), out(kBlock);
    const auto run = [&](double seconds) {
        for (int b = 0; b < static_cast<int>(seconds * 100.0); b++)
        {
            phone.render(in.data(), kBlock);
            if (onVoice) {
                phoneSat.add(in.data(), kBlock);
            }
            for (int i = 0; tone && i < kBlock; i++) {
                in[i] += static_cast<float>(kFskDeviation * std::sin(2.0 * kPi * kSignallingToneHz * (t + i / kRate)));
            }
            ch.receive(in.data(), kBlock);
            ch.render(out.data(), kBlock);
            focc.process(out.data(), kBlock);
            clock.advance(kBlock);
            system.run();
            t += kBlock / kRate;
        }
    };

    system.setCallHandler([](const AmpsSubscriber&, const std::string&) { return true; });
    system.setClearHandler([&clears](const AmpsSubscriber&) { clears++; });
    ch.setTraceTap([&sent](bool toPhone, const std::string& text) {
        if (toPhone) {
            sent.push_back(text);
        }
    });
    run(1.0);
    CHECK(heardWord(focc.words, [](uint64_t w) { return wordField(words::kControlFiller, w, "OHD") == kOhdControlFiller; }));
    phone.send(reccMessage(0, originationWords()));
    run(1.0);
    CHECK(heardWord(focc.words, [](uint64_t w) {
        return wordField(words::kExtendedAddressChannel, w, "T1T2") == kT1T2Next && wordField(words::kExtendedAddressChannel, w, "SCC") == 0
            && wordField(words::kExtendedAddressChannel, w, "CHAN") == 353;
    }));
    CHECK(system.mode(0) == AmpsMode::Silence);
    CHECK(std::find(sent.begin(), sent.end(), "the voice channel of a call, SAT 5970 Hz") != sent.end());
    onVoice = true;
    run(1.0);
    CHECK(system.mode(0) == AmpsMode::Audio && system.transactions().size() == 1);
    // an audit, the phone's confirmation on the RVC of this channel (no SAT while it sends it)
    AmpsSubscriber s;
    s.min1 = kMin1;
    s.min2 = kMin2;
    std::vector<std::string> logs;
    system.setLogHandler([&logs](const std::string& text) { logs.push_back(text); });
    CHECK(system.audit(s.number()) == AmpsSystem::OrderResult::Sent);
    run(0.3);
    onVoice = false;
    phone.send(rvcMessage({encodeWord(words::kRvcOrderConfirmation, {{"F", 1}, {"NAWC", 0}, {"T", 1}, {"ORDER", kOrderAudit}})}));
    run(0.3);
    onVoice = true;
    run(0.2);
    CHECK(std::find(logs.begin(), logs.end(), s.number() + ": Audit confirmed") != logs.end());
    // the network releases: the release order on the FVC of this channel, the phone confirms with its tone
    CHECK(system.releaseCall(s.number()));
    run(0.5);
    CHECK(std::find(sent.begin(), sent.end(), "Release") != sent.end());      // on the FVC of this channel
    tone = true;
    run(1.8);
    tone = false;
    run(0.6);
    CHECK(system.mode(0) == AmpsMode::Control && system.transactions().empty());
    CHECK(std::find(sent.begin(), sent.end(), "the control channel again") != sent.end());
    onVoice = false;
    focc.words.clear();
    run(1.0);
    CHECK(heardWord(focc.words, [](uint64_t w) { return wordField(words::kControlFiller, w, "OHD") == kOhdControlFiller; }));
}

int main()
{
    testCall();
    testHandoff();
    testCombined();

    if (g_failures == 0) {
        std::printf("all channel tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
