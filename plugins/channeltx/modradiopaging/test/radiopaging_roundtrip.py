#!/usr/bin/env python3
"""Radio paging round trip over a radio: every message type and its edge cases, sent by a Radio Paging modulator
and received by a Radio Paging demodulator, the two joined by a radio (a USRP with its TX port cabled to its RX
port, an attenuator in between).

sdrangelsrv is started with a TX device set (the modulator) and an RX device set (the demodulator) on the same
device, and a Message Center whose file sink writes every record (JSON lines). The cases are sent in groups, one
for each setting of the modulator (protocol, speed or mode, polarity); after each group the records received are
matched to the messages sent.

Each case checks, for the message received:
  - its text, exactly (nothing cut at the end, no fill or padding taken for characters, no character lost);
  - its type (POCSAG has no type field: the type the decoder chose, or its alternative);
  - no codeword that could not be corrected, not truncated, not partial, FLEX fragments reassembled (complete);
  - the copies of a message sent with retransmissions (received again, marked as duplicates).
It also reports the characters received from corrected codewords (c), and those at the end of a message: a bit
error in the last codeword is what a cut transmission (the carrier off too early) gives. A received record that
matches no message sent is reported (a false decode, or a message split in two).

  radiopaging_roundtrip.py --build BUILD_DIR [--work DIR] [--port PORT] [--freq HZ] [--tx-gain DB] [--rx-gain DB]
                           [--only pocsag|flex|gsc] [--case TEXT] [--keep]

Needs the server and the plugins: sdrangelsrv featuremessagecentersrv modradiopagingsrv demodradiopagingsrv
outputusrpsrv inputusrpsrv. UHD_IMAGES_DIR is passed on. The radio transmits: keep the TX gain low, with a cable
and an attenuator between the ports, on a frequency you may use.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

# ----------------------------------------------------------------------------------------------------------------
# Payloads

ALNUM = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
PRINTABLE = "".join(chr(c) for c in range(32, 127))


def marked(length):
    """A text of exactly length characters whose end says where it is: dots with the position every 10 characters
    and =N> at the end. A cut text shows where it was cut, padding taken for characters shows after the >"""
    end = "=%d>" % length
    body = ""
    while len(body) < length:
        n = len(body) + 10
        body += "." * (10 - len(str(n))) + str(n)
    return body[: length - len(end)] + end


def filled(length, pattern=ALNUM):
    return (pattern * (length // len(pattern) + 1))[:length]


def digits(length, pattern="1234567890"):
    return filled(length, pattern)


def hexDigits(length, pattern="0123456789ABCDEF"):
    return filled(length, pattern)


# ----------------------------------------------------------------------------------------------------------------
# Cases: one message each. expect: "ok" (sent and received as sent), "refused" (400 from the tx action), "cut"
# (received as the first n characters: cut=n)


class Case:
    def __init__(self, label, msg, expect="ok", rxText=None, rxType=None, copies=1, group=None):
        self.label = label
        self.msg = msg
        self.expect = expect
        self.rxText = rxText          # The text expected when it is not the one sent
        self.rxType = rxType          # The type expected when it is not the one sent
        self.copies = copies          # Records received: 1 + retransmissions
        self.group = group            # FLEX group members: the message goes to a temporary address
        self.id = None
        self.result = None
        self.detail = ""


def pocsagCases(speed):
    p = lambda label, ric, fn, typ, text, **kw: Case("POCSAG %d %s" % (speed, label),
        {"protocol": "pocsag", "address": str(ric), "function": fn, "type": typ, "text": text, "speed": speed}, **kw)
    cases = [
        p("tone A", 500001, 0, "tone", ""),
        p("tone D", 500002, 3, "tone", ""),
        p("RIC 0", 0, 0, "alphanumeric", "RIC 0"),
        p("RIC 8 (frame 0)", 8, 1, "numeric", "12345"),
        p("RIC 2097151 (max)", 2097151, 2, "alphanumeric", "Max RIC"),
        p("same frame 1", 700008, 0, "alphanumeric", "Frame 0 msg A"),
        p("same frame 2", 700016, 0, "alphanumeric", "Frame 0 msg B"),
    ]
    # Numeric: around the 5 digits of a codeword
    for n, text in [(1, "7"), (5, "12345"), (6, "123456"), (10, "1234567890"), (15, "123456789012345"),
                    (20, "12345678901234567890"), (21, "123456789012345678901"), (40, digits(40))]:
        cases.append(p("numeric %d" % n, 600000 + n, 0, "numeric", text))
    for i, text in enumerate(["555 123 4567", "555-123-4567", "[555]1234567", "0123456789U -][", "U5551234567", "0", "9"]):
        cases.append(p("numeric chars %d" % i, 601000 + i, 1, "numeric", text))
    # Alphanumeric: around the 20 bits of a codeword (7 bits a character), long ones with an end mark
    for n in [1, 2, 3, 4, 5, 20, 39, 80, 120]:
        cases.append(p("alpha %d" % n, 602000 + n, 3, "alphanumeric", filled(n)))
    for n in [200, 500]:
        cases.append(p("alpha %d marked" % n, 603000 + n, 3, "alphanumeric", marked(n)))
    cases.append(p("printable ASCII", 604001, 3, "alphanumeric", PRINTABLE))
    for i, text in enumerate(["if(x>0){y=1;}", "user@host.com", "Price: $19.99 @store #42!", "hello world", "Wait..."]):
        cases.append(p("alpha punctuation %d" % i, 604100 + i, 3, "alphanumeric", text))
    # Alphanumeric that looks numeric: the decoder may choose numeric; the text must be in the record either way
    for i, text in enumerate(["911", "5551234", "12345"]):
        cases.append(p("alpha digits %d" % i, 604200 + i, 3, "alphanumeric", text, rxType="any"))
    cases.append(p("out of range RIC", 2097152, 0, "alphanumeric", "x", expect="refused"))
    cases.append(p("not numeric", 604300, 0, "numeric", "ABC", expect="refused"))
    return cases


def flexCases(mode, full):
    f = lambda label, cap, typ, text, **kw: Case("FLEX %s %s" % (MODES[mode], label),
        dict({"protocol": "flex", "address": str(cap), "type": typ, "text": text}, **kw.pop("extra", {})), **kw)
    base = 1000000 + 10000 * mode
    cases = [
        f("tone", base + 1, "tone", ""),
        f("alpha 1", base + 2, "alphanumeric", "A"),
        f("alpha 3", base + 3, "alphanumeric", "Hi!"),
        f("alpha 63", base + 4, "alphanumeric", filled(63)),
        f("numeric 1", base + 5, "numeric", "7"),
        f("numeric 20", base + 6, "numeric", digits(20)),
        f("numeric chars", base + 7, "numeric", "0123456789U -]["),
        f("binary F", base + 8, "binary", "F"),
        f("binary ends in 0", base + 9, "binary", "ABCD0"),
        f("binary ends in F", base + 10, "binary", "ABCDF"),
        f("binary B=8 Hello", base + 11, "binary", "48656C6C6F", extra={"blocking": 8}),
        f("secure alpha", base + 12, "secure", "Secure text", extra={"secureType": 0}),
        f("secure binary", base + 13, "secure", "DEADBEEFCAFE0123", extra={"secureType": 2}),
        f("short message", base + 14, "short_message", "123", extra={"shortType": 0}),
        f("long address alpha", 2101249 + mode, "alphanumeric", "Long address"),
        f("priority", base + 15, "alphanumeric", "Priority", extra={"priority": 1}),
    ]
    if full:
        cases += [
            f("capcode 1", 1 + mode, "alphanumeric", "short-cap-min"),
            f("capcode 1933312", 1933312 - mode, "alphanumeric", "short-cap-max"),
            f("capcode 4297068542", 4297068542 - mode, "alphanumeric", "long-cap-max"),
            f("letter form", "3E00523759%d" % (3 + mode), "alphanumeric", "Letter form"),
            f("alpha 248", base + 20, "alphanumeric", marked(248)),
            f("alpha 249", base + 21, "alphanumeric", marked(249)),
            f("alpha 500 (fragments)", base + 22, "alphanumeric", marked(500)),
            f("numeric 41", base + 23, "numeric", digits(41)),
            f("special numeric", base + 24, "special_numeric", "0123456789U -]["),
            f("numbered numeric", base + 25, "numbered_numeric", "5551230001"),
            f("numbered special numeric", base + 26, "numbered_special_numeric", "9876543210"),
            f("binary 420", base + 27, "binary", hexDigits(420)),
            f("binary 500 (fragments)", base + 28, "binary", hexDigits(500)),
            f("secure binary 500 (fragments)", base + 29, "secure", hexDigits(500, "AABBCCDDEEFF0011"), extra={"secureType": 2}),
            f("short message long address", 2101300 + mode, "short_message", "12345678", extra={"shortType": 0}),
            f("printable ASCII", base + 30, "alphanumeric", PRINTABLE),
            f("mail drop", base + 31, "alphanumeric", "Mail drop", extra={"mailDrop": 1}),
            f("retransmit 1", base + 32, "alphanumeric", "Copy me", extra={"retransmit": 1, "retransmitInterval": 2}, copies=2),
            f("not hex", base + 33, "binary", "GHIJ", expect="refused"),
            f("capcode 0", 0, "alphanumeric", "x", expect="refused"),
        ]
    return cases


def flexGroupCases():
    members = "2001,2002,2101249"
    return [Case("FLEX 1600/2 group of 3", {"protocol": "flex", "address": "2001", "groupMembers": members,
        "type": "alphanumeric", "text": "GROUP of three " + marked(60)}, group=[2001, 2002, 2101249])]


def gscCases():
    g = lambda label, addr, fn, typ, text, **kw: Case("GSC %s" % label,
        {"protocol": "gsc", "address": addr, "function": fn, "type": typ, "text": text}, **kw)
    return [
        g("tone", "012355", 0, "tone", ""),
        g("tone function D", "012356", 3, "tone", ""),
        g("address digits not allowed", "012340", 0, "tone", "", expect="refused"),
        g("numeric 1", "012341", 0, "numeric", "5"),
        g("numeric 12 (1 block)", "012342", 0, "numeric", "123456789012"),
        g("numeric 13 (2 blocks)", "012343", 0, "numeric", "1234567890123"),
        g("numeric 24 (max)", "012344", 0, "numeric", digits(24)),
        g("numeric chars", "012345", 1, "numeric", "0123456789U -"),
        g("alpha 1", "012346", 2, "alphanumeric", "A"),
        g("alpha 8 (1 block)", "012347", 2, "alphanumeric", "ABCDEFGH"),
        g("alpha 9 (2 blocks)", "012348", 2, "alphanumeric", "ABCDEFGHI"),
        g("alpha 80 (max)", "012349", 3, "alphanumeric", marked(80).upper()),
        g("alpha chars", "012350", 3, "alphanumeric", "HELLO WORLD! @#$%&()*+,-./:;<=>?"),
        g("5 digits (index 0)", "12351", 1, "alphanumeric", "FIVE DIGITS"),
        g("7 digits (function digit)", "0123527", 0, "alphanumeric", "SUFFIX 7"),
        # Longer than many pagers show (24 digits, 80 characters): the protocol allows 32 blocks
        g("numeric 25", "012353", 0, "numeric", digits(25)),
        g("alpha 81", "012354", 0, "alphanumeric", marked(81).upper()),
        g("alpha 200 (25 blocks)", "012357", 1, "alphanumeric", marked(200).upper()),
    ]


MODES = {0: "1600/2", 1: "3200/2", 2: "3200/4", 3: "6400/4"}

# ----------------------------------------------------------------------------------------------------------------
# REST


class Api:
    def __init__(self, port):
        self.base = "http://127.0.0.1:%d/sdrangel" % port

    def call(self, method, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.base + path, data=data, method=method, headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                text = r.read().decode()
                return r.status, (json.loads(text) if text.strip() else {})
        except urllib.error.HTTPError as e:
            text = e.read().decode()
            try:
                return e.code, json.loads(text)
            except ValueError:
                return e.code, {"message": text}

    def ok(self, method, path, body=None):
        code, reply = self.call(method, path, body)
        if code // 100 != 2:
            raise RuntimeError("%s %s: %d %s" % (method, path, code, reply))
        return reply


# ----------------------------------------------------------------------------------------------------------------
# Matching


def key(protocol, data, address, sent=False):
    """The pager a record or a message is for: POCSAG ric and function, FLEX capcode, GSC address and number"""
    if protocol == "pocsag":
        if sent:
            return ("pocsag", int(address), int(data.get("function", 0)))
        return ("pocsag", int(data.get("ric", -1)), int(data.get("function", -1)))
    if protocol == "flex":
        if sent:
            return ("flex", address)
        return ("flex", int(data.get("capcode", -1)))
    return ("gsc", address)


def runGroup(api, cases, sinkPath, seen, settings, wait):
    """Sends the cases with the modulator set as settings, waits until it has sent them, matches what was received"""
    api.ok("PATCH", "/deviceset/1/channel/0/settings", {"channelType": "RadioPagingMod", "direction": 1, "RadioPagingModSettings": settings})
    time.sleep(1.0)
    start = time.time()

    for c in cases:
        c.id = "rt-%d-%d" % (int(start), id(c) % 100000)
        msg = dict(c.msg, id=c.id)
        code, reply = api.call("POST", "/deviceset/1/channel/0/actions",
            {"channelType": "RadioPagingMod", "direction": 1, "RadioPagingModActions": {"tx": msg}})
        if c.expect == "refused":
            c.result = "PASS" if code == 400 else "FAIL"
            c.detail = "refused: %s" % reply.get("message", "") if code == 400 else "accepted (%d), should be refused" % code
        elif code != 202:
            c.result = "FAIL"
            c.detail = "tx refused: %d %s" % (code, reply.get("message", ""))

    # Sent: the modulator's queue empty and idle, then the time of a frame or two for the receiver. A group that does
    # not end in time is reported: its last messages would be dropped by the settings of the next group
    deadline = time.time() + wait
    finished = False
    while time.time() < deadline:
        code, rep = api.call("GET", "/deviceset/1/channel/0/report")
        r = rep.get("RadioPagingModReport", {})
        if code == 200 and r.get("queued", 1) == 0 and r.get("inFlight", 1) == 0 and r.get("state", 1) == 0:
            finished = True
            break
        time.sleep(1.0)
    if not finished:
        print("TIMEOUT: the modulator did not send the group in %d s" % wait, flush=True)
    time.sleep(6.0)

    # The records of the sink written since the start of the group
    rx, tx = [], {}
    with open(sinkPath, encoding="utf-8") as f:
        for line in f:
            try:
                rec = json.loads(line)
            except ValueError:
                continue
            if rec.get("id") in seen:
                continue
            seen.add(rec.get("id"))
            if rec.get("direction") == "rx":
                rx.append(rec)
            else:
                tx[rec.get("id")] = rec

    used = set()

    for c in cases:
        if c.result is not None:
            continue
        sentRecord = tx.get(c.id)
        if sentRecord is None:
            c.result = "FAIL"
            c.detail = "no record of the message sent"
            continue
        protocol = c.msg["protocol"]
        text = c.rxText if c.rxText is not None else c.msg["text"]

        if c.group:
            # The group message goes to a temporary address with its members
            want = set(str(m).zfill(10) for m in c.group)
            got = [i for i, r in enumerate(rx) if r.get("protocol") == "flex" and (r.get("protocolData") or {}).get("group_capcodes")
                   and want <= set((r.get("protocolData") or {}).get("group_capcodes"))]
        else:
            k = key(protocol, sentRecord.get("protocolData") or {}, sentRecord.get("address"), sent=False) \
                if protocol != "gsc" else ("gsc", sentRecord.get("address"))
            got = [i for i, r in enumerate(rx) if i not in used and
                   (key(r.get("protocol"), r.get("protocolData") or {}, r.get("address")) if r.get("protocol") != "gsc"
                    else ("gsc", r.get("address"))) == k]

        # Fragments reported on their own are not messages
        got = [i for i in got if not (rx[i].get("protocolData") or {}).get("is_fragment")]
        used.update(got)

        if not got:
            c.result = "FAIL"
            c.detail = "not received"
            continue

        r = rx[got[0]]
        d = r.get("protocolData") or {}
        rtext = r.get("text") or ""
        problems = []
        if rtext != text and not (c.rxType == "any" and d.get("alt_text") == text):
            problems.append("text %r, sent %r" % (rtext[:80], text[:80]))
        expectedType = c.rxType or c.msg["type"]
        if expectedType != "any" and r.get("msgType") != expectedType and not (expectedType == "tone" and r.get("msgType") in ("tone", "short_message")):
            problems.append("type %s, sent %s" % (r.get("msgType"), expectedType))
        if (r.get("errorsUncorrectable") or 0) > 0:
            problems.append("%d codewords not corrected" % r.get("errorsUncorrectable"))
        if d.get("truncated"):
            problems.append("truncated")
        if r.get("partial") or d.get("complete") is False:
            problems.append("incomplete (fragments)")
        if len(got) != c.copies:
            problems.append("%d records received, %d expected" % (len(got), c.copies))

        status = d.get("char_status") or ""
        corrected = status.count("c")
        notes = []
        if corrected:
            notes.append("%d characters corrected" % corrected)
        if status and status[-1] != ".":
            notes.append("last character %s" % {"c": "corrected", "X": "lost", "u": "uncertain"}.get(status[-1], status[-1]))
        if (r.get("errorsCorrected") or 0) > 0:
            notes.append("%d bits corrected" % r.get("errorsCorrected"))

        c.result = "FAIL" if problems else "PASS"
        c.detail = "; ".join(problems + notes)

    unexpected = [r for i, r in enumerate(rx) if i not in used and not (r.get("protocolData") or {}).get("is_fragment")
                  and r.get("kind", "message") == "message"]
    return unexpected


# ----------------------------------------------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build", required=True)
    ap.add_argument("--work")
    ap.add_argument("--port", type=int, default=18321)
    ap.add_argument("--freq", type=int, default=439987500, help="Channel frequency (Hz)")
    ap.add_argument("--tx-gain", type=float, default=0)
    ap.add_argument("--rx-gain", type=float, default=10)
    ap.add_argument("--only", choices=["pocsag", "flex", "gsc"])
    ap.add_argument("--case", help="Only the cases whose label contains this text")
    ap.add_argument("--quick", action="store_true", help="FLEX: the edge cases only at 1600/2")
    ap.add_argument("--keep", action="store_true", help="Leave the server running")
    args = ap.parse_args()

    work = args.work or tempfile.mkdtemp(prefix="radiopaging-roundtrip-")
    os.makedirs(work, exist_ok=True)
    sinkPath = os.path.join(work, "records.jsonl")
    if os.path.exists(sinkPath):
        os.remove(sinkPath)
    env = dict(os.environ, XDG_CONFIG_HOME=os.path.join(work, "config"), XDG_DATA_HOME=os.path.join(work, "data"),
               LD_LIBRARY_PATH=os.path.join(args.build, "lib"))
    log = open(os.path.join(work, "server.log"), "w")
    server = subprocess.Popen(["./sdrangelsrv", "--scratch", "-a", "127.0.0.1", "-p", str(args.port)], cwd=args.build,
                              env=env, stdout=log, stderr=subprocess.STDOUT)
    api = Api(args.port)

    try:
        for _ in range(100):
            try:
                api.call("GET", "")
                break
            except (urllib.error.URLError, ConnectionError):
                time.sleep(0.2)

        # Message Center: every record to the log and to the file
        api.ok("POST", "/featureset/feature", {"featureType": "MessageCenter"})
        time.sleep(1)
        features = api.ok("GET", "/featureset")
        mc = [i for i, f in enumerate(features.get("features", [])) if f.get("id") == "MessageCenter"]
        mc = mc[0] if mc else 0
        api.ok("PATCH", "/featureset/feature/%d/settings" % mc, {"featureType": "MessageCenter", "MessageCenterSettings": {
            "logSize": 5000,
            "sinks": json.dumps([{"name": "file", "type": "file", "path": sinkPath, "format": 0}]),
            "routes": json.dumps([{"name": "all", "enabled": True, "sinks": ["log", "file"]}])}})

        # RX (device set 0) and TX (device set 1) on the radio, the channel 12.5 kHz below the centre
        centre = args.freq + 12500
        api.ok("POST", "/deviceset?direction=0")
        time.sleep(2)
        api.ok("POST", "/deviceset?direction=1")
        time.sleep(2)
        api.ok("PUT", "/deviceset/0/device", {"hwType": "USRP", "direction": 0, "deviceStreamIndex": 0})
        time.sleep(5)
        api.ok("PUT", "/deviceset/1/device", {"hwType": "USRP", "direction": 1, "deviceStreamIndex": 0})
        time.sleep(5)
        api.ok("PATCH", "/deviceset/1/device/settings", {"deviceHwType": "USRP", "direction": 1, "usrpOutputSettings": {
            "gain": args.tx_gain, "centerFrequency": centre, "devSampleRate": 1000000, "antennaPath": "TX/RX"}})
        api.ok("PATCH", "/deviceset/0/device/settings", {"deviceHwType": "USRP", "direction": 0, "usrpInputSettings": {
            "gain": args.rx_gain, "centerFrequency": centre, "devSampleRate": 1000000, "antennaPath": "RX2"}})
        api.ok("POST", "/deviceset/0/channel", {"channelType": "RadioPagingDemod", "direction": 0})
        api.ok("POST", "/deviceset/1/channel", {"channelType": "RadioPagingMod", "direction": 1})
        time.sleep(2)
        api.ok("PATCH", "/deviceset/0/channel/0/settings", {"channelType": "RadioPagingDemod", "direction": 0,
            "RadioPagingDemodSettings": {"inputFrequencyOffset": -12500, "title": "rx"}})
        api.ok("PATCH", "/deviceset/1/channel/0/settings", {"channelType": "RadioPagingMod", "direction": 1,
            "RadioPagingModSettings": {"inputFrequencyOffset": -12500, "title": "tx", "mode": 0}})
        api.ok("POST", "/deviceset/0/device/run")
        time.sleep(2)
        api.ok("POST", "/deviceset/1/device/run")
        time.sleep(3)

        # Groups: (name, modulator settings, cases, seconds to wait at most)
        groups = []
        if args.only in (None, "pocsag"):
            for speed in (512, 1200, 2400):
                groups.append(("POCSAG %d" % speed, {"protocol": 0, "pocsagSpeed": speed, "pocsagInverted": 0}, pocsagCases(speed),
                               {512: 600, 1200: 300, 2400: 200}[speed]))
            inv = [c for c in pocsagCases(1200) if c.expect == "ok"][:8]
            for c in inv:
                c.label = c.label.replace("POCSAG 1200", "POCSAG 1200 inverted")
                c.msg["polarity"] = "inverted"
            groups.append(("POCSAG 1200 inverted", {"protocol": 0, "pocsagSpeed": 1200, "pocsagInverted": 1}, inv, 120))
        if args.only in (None, "flex"):
            for mode in MODES:
                full = (mode == 0) or not args.quick
                groups.append(("FLEX %s" % MODES[mode], {"protocol": 1, "flexMode": mode, "flexInverted": 0}, flexCases(mode, full), 600))
            groups.append(("FLEX group", {"protocol": 1, "flexMode": 0, "flexInverted": 0}, flexGroupCases(), 120))
        if args.only in (None, "gsc"):
            groups.append(("GSC", {"protocol": 2, "gscInverted": 0}, gscCases(), 300))

        seen = set()
        allCases = []
        unexpectedAll = []

        for name, settings, cases, wait in groups:
            if args.case:
                cases = [c for c in cases if args.case in c.label]
            if not cases:
                continue
            print("== %s: %d messages" % (name, len(cases)), flush=True)
            unexpected = runGroup(api, cases, sinkPath, seen, settings, wait)
            for c in cases:
                print("%s: %s%s" % (c.result, c.label, (" (" + c.detail + ")") if c.detail else ""), flush=True)
            for r in unexpected:
                print("UNEXPECTED: %s %s %s %r" % (r.get("protocol"), r.get("address"), r.get("msgType"), (r.get("text") or "")[:60]), flush=True)
            allCases += cases
            unexpectedAll += unexpected

        failed = [c for c in allCases if c.result != "PASS"]
        endErrors = [c for c in allCases if "last character" in c.detail]
        print("\n%d cases, %d passed, %d failed, %d with an error in the last character, %d unexpected records"
              % (len(allCases), len(allCases) - len(failed), len(failed), len(endErrors), len(unexpectedAll)))
        with open(os.path.join(work, "results.json"), "w") as f:
            json.dump({"cases": [{"label": c.label, "result": c.result, "detail": c.detail, "message": c.msg} for c in allCases],
                       "unexpected": unexpectedAll}, f, indent=1)
        print("Records: %s, results: %s" % (sinkPath, os.path.join(work, "results.json")))
        return 1 if failed or unexpectedAll else 0
    finally:
        if not args.keep:
            server.terminate()
            try:
                server.wait(10)
            except subprocess.TimeoutExpired:
                server.kill()


if __name__ == "__main__":
    sys.exit(main())
