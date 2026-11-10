#!/bin/bash
# Message center end to end: sdrangelsrv with the message center feature and a GSC modulator on a
# file output. The modulator announces itself, pages are posted to the message center (WebAPI
# action), claimed and transmitted by the modulator; the states are read back from the report.
#
#   messagecenter_e2e.sh [BUILD_DIR] [PORT]
#
# Needs the server (ninja sdrangelsrv featuremessagecentersrv modradiopagingsrv outputfileoutputsrv)

BUILD=${1:-/tmp/sdrangel-build}
PORT=${2:-18093}
WORK=$(mktemp -d)
U=http://127.0.0.1:$PORT/sdrangel
OUT="$WORK/out.sdriq"
failed=0

check() {
    if eval "$2"; then echo "PASS: $1"; else echo "FAIL: $1"; failed=$((failed + 1)); fi
}

curl_code() { curl -s -o /dev/null -w "%{http_code}" -X "$1" "$U$2" -H 'Content-Type: application/json' ${3:+-d "$3"}; }
report() { curl -s "$U/featureset/feature/0/report" | tr -d '\n' | sed 's/\\"/"/g'; }
post() {
    curl_code POST /featureset/feature/0/actions "{\"featureType\":\"MessageCenter\",\"MessageCenterActions\":{\"post\":$1}}"
}

mkdir -p "$WORK/config"
(cd "$BUILD" && XDG_CONFIG_HOME="$WORK/config" LD_LIBRARY_PATH="$BUILD/lib" ./sdrangelsrv --scratch -a 127.0.0.1 -p "$PORT" > "$WORK/server.log" 2>&1 &)

for i in $(seq 50); do curl -s "$U" > /dev/null && break; sleep 0.2; done

curl_code POST /featureset/feature '{"featureType":"MessageCenter"}' > /dev/null
curl_code POST "/deviceset?direction=1" > /dev/null
sleep 0.5
curl_code PUT /deviceset/0/device '{"hwType":"FileOutput","direction":1}' > /dev/null
curl_code PATCH /deviceset/0/device/settings "{\"deviceHwType\":\"FileOutput\",\"direction\":1,\"fileOutputSettings\":{\"fileName\":\"$OUT\",\"sampleRate\":48000,\"log2Interp\":0}}" > /dev/null
curl_code POST /deviceset/0/channel '{"channelType":"RadioPagingMod","direction":1}' > /dev/null
curl_code PATCH /deviceset/0/channel/0/settings '{"channelType":"RadioPagingMod","direction":1,"RadioPagingModSettings":{"protocol":2,"mode":0,"channelAlias":"t1","deviation":4500}}' > /dev/null
curl_code POST /deviceset/0/device/run > /dev/null

# The modulator announces itself every 5 s
sleep 7
R=$(report)
check "the modulator is registered under its alias, idle" '[[ "$R" == *"\"alias\":\"t1\""*"\"role\":\"tx\""*"\"status\":\"idle\""* ]]'

check "a page is accepted" '[ "$(post "{\"id\":\"p-tone\",\"transmitter\":\"t1\",\"protocol\":\"gsc\",\"address\":\"123456\",\"function\":1,\"type\":\"tone_only\"}")" = 202 ]'
check "a page with text is accepted" '[ "$(post "{\"id\":\"p-alpha\",\"transmitter\":\"t1\",\"protocol\":\"gsc\",\"address\":\"123456\",\"function\":2,\"type\":\"alphanumeric\",\"text\":\"FROM THE CENTER\"}")" = 202 ]'
check "a page for a transmitter that is not there is accepted" '[ "$(post "{\"id\":\"p-nobody\",\"transmitter\":\"nobody\",\"protocol\":\"gsc\",\"address\":\"123456\",\"function\":0,\"type\":\"tone_only\"}")" = 202 ]'
check "a page with a bad address is refused" '[ "$(post "{\"id\":\"p-bad\",\"transmitter\":\"t1\",\"protocol\":\"flex\",\"address\":\"abc\"}")" = 400 ]'
check "a voice page needs a file" '[ "$(post "{\"id\":\"p-voice\",\"transmitter\":\"t1\",\"protocol\":\"gsc\",\"address\":\"123456\",\"type\":\"voice\"}")" = 400 ]'

sleep 12
R=$(report)
check "the page was claimed by the modulator and sent" '[[ "$R" == *"\"claimedBy\":\"t1\""*"\"id\":\"p-tone\""*"\"state\":\"sent\""* ]]'
check "the page with text was sent" '[[ "$R" == *"\"id\":\"p-alpha\""*"\"state\":\"sent\""*"\"text\":\"FROM THE CENTER\""* ]]'
check "the page for nobody waits" '[[ "$R" == *"\"id\":\"p-nobody\""*"\"state\":\"pending\""* ]]'
check "the sent messages are in the log, from the transmitter" '[[ "$R" == *"\"channelAlias\":\"t1\""*"\"direction\":\"tx\""* ]]'
check "the modulator sent two messages" '[ "$(curl -s "$U/deviceset/0/channel/0/report" | tr -d "\n " | grep -o "\"sent\":[0-9]*")" = "\"sent\":2" ]'

check "a page nobody has can be withdrawn" '[ "$(curl_code POST /featureset/feature/0/actions "{\"featureType\":\"MessageCenter\",\"MessageCenterActions\":{\"cancel\":\"p-nobody\"}}")" = 202 ]'
sleep 1.5
R=$(report)
# (the pages have a state, the records of the log do not)
check "the withdrawn page is gone" '! echo "$R" | grep -q "\"id\":\"p-nobody\",\"protocol\"[^}]*\"state\""'

check "the settings are read" '[[ "$(curl -s "$U/featureset/feature/0/settings" | tr -d "\n")" == *"\"logSize\": 10000"* ]]'
check "a route that is not JSON is refused" '[ "$(curl_code PATCH /featureset/feature/0/settings "{\"featureType\":\"MessageCenter\",\"MessageCenterSettings\":{\"routes\":\"not json\"}}")" = 400 ]'
ROUTES='[{\"name\":\"gsc to the file\",\"enabled\":true,\"stop\":false,\"sinks\":[\"log\"],\"filter\":{\"protocols\":[\"gsc\"]}}]'
check "routes and the claim time are set" '[ "$(curl_code PATCH /featureset/feature/0/settings "{\"featureType\":\"MessageCenter\",\"MessageCenterSettings\":{\"claimTimeoutSec\":20,\"routes\":\"$ROUTES\"}}")" = 200 ]'
sleep 1
S=$(curl -s "$U/featureset/feature/0/settings" | tr -d "\n" | sed 's/\\"/"/g')
check "the settings keep them" '[[ "$S" == *"\"claimTimeoutSec\": 20"* && "$S" == *"gsc to the file"* ]]'

curl_code DELETE /deviceset/0/device/run > /dev/null
fuser -k "$PORT"/tcp > /dev/null 2>&1
rm -rf "$WORK"
echo "$failed failed"
exit $failed
