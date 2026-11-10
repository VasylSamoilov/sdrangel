#!/bin/bash
# FLEX HEX/binary and secure messages end to end: sdrangelsrv with a FLEX modulator (one-shot) on a
# file output takes the messages on its WebAPI (tx action) and writes them; a file input plays the file
# to a radio paging demodulator. The message center log has the records of both: the received ones are
# checked against the sent ones (text, data bits, header fields, K and signature).
#
#   radiopagingflex_binary_e2e.sh [BUILD_DIR] [PORT]
#
# Needs the server (ninja sdrangelsrv featuremessagecentersrv modradiopagingsrv demodradiopagingsrv
# outputfileoutputsrv inputfileinputsrv)

BUILD=${1:-/tmp/sdrangel-build}
PORT=${2:-18094}
WORK=$(mktemp -d)
U=http://127.0.0.1:$PORT/sdrangel
OUT="$WORK/out.sdriq"
failed=0

check() {
    if eval "$2"; then echo "PASS: $1"; else echo "FAIL: $1"; failed=$((failed + 1)); fi
}

curl_code() { curl -s -o /dev/null -w "%{http_code}" -X "$1" "$U$2" -H 'Content-Type: application/json' ${3:+-d "$3"}; }
report() { curl -s "$U/featureset/feature/0/report" | tr -d '\n' | sed 's/\\"/"/g'; }
tx() {
    curl_code POST /deviceset/0/channel/0/actions "{\"channelType\":\"RadioPagingMod\",\"direction\":1,\"RadioPagingModActions\":{\"tx\":$1}}"
}
sent() { curl -s "$U/deviceset/0/channel/0/report" | tr -d "\n " | grep -o "\"sent\":[0-9]*" | cut -d: -f2; }
# The received record of a capcode (the log is a JSON list of records, one per line once split)
rxRecord() { report | sed 's/},{/}\n{/g' | grep "\"direction\":\"rx\"" | grep "\"address\":\"0*$1\"" | head -1; }

mkdir -p "$WORK/config"
(cd "$BUILD" && XDG_CONFIG_HOME="$WORK/config" LD_LIBRARY_PATH="$BUILD/lib" ./sdrangelsrv --scratch -a 127.0.0.1 -p "$PORT" > "$WORK/server.log" 2>&1 &)

for i in $(seq 50); do curl -s "$U" > /dev/null && break; sleep 0.2; done

curl_code POST /featureset/feature '{"featureType":"MessageCenter"}' > /dev/null

# Transmitter: FLEX 1600/2, one-shot
curl_code POST "/deviceset?direction=1" > /dev/null
sleep 0.5
curl_code PUT /deviceset/0/device '{"hwType":"FileOutput","direction":1}' > /dev/null
curl_code PATCH /deviceset/0/device/settings "{\"deviceHwType\":\"FileOutput\",\"direction\":1,\"fileOutputSettings\":{\"fileName\":\"$OUT\",\"sampleRate\":48000,\"log2Interp\":0}}" > /dev/null
curl_code POST /deviceset/0/channel '{"channelType":"RadioPagingMod","direction":1}' > /dev/null
curl_code PATCH /deviceset/0/channel/0/settings '{"channelType":"RadioPagingMod","direction":1,"RadioPagingModSettings":{"protocol":1,"mode":0,"flexMode":0,"channelAlias":"t1","deviation":4800}}' > /dev/null
curl_code POST /deviceset/0/device/run > /dev/null
sleep 1

# 64 bits B=8 (fill); 84 bits B=4 ending exactly at a word end with a uniform last character (padding
# word); B=7 right to left with M H; secure alphanumeric; secure binary at an exact word end (termination word)
check "binary B=8 is accepted" '[ "$(tx "{\"protocol\":\"flex\",\"capcode\":1000001,\"type\":\"binary\",\"blocking\":8,\"text\":\"0123456789ABCDEF\"}")" = 202 ]'
check "binary B=4 exact end is accepted" '[ "$(tx "{\"protocol\":\"flex\",\"capcode\":1000002,\"type\":\"binary\",\"blocking\":4,\"text\":\"123456789ABCDEF12345F\"}")" = 202 ]'
check "binary B=7 D M H is accepted" '[ "$(tx "{\"protocol\":\"flex\",\"capcode\":1000003,\"type\":\"binary\",\"blocking\":7,\"rightToLeft\":1,\"mailDrop\":1,\"headerMessage\":1,\"text\":\"1234567\"}")" = 202 ]'
check "secure alphanumeric is accepted" '[ "$(tx "{\"protocol\":\"flex\",\"capcode\":1000004,\"type\":\"secure\",\"secureType\":0,\"text\":\"Secure text\"}")" = 202 ]'
check "secure binary is accepted" '[ "$(tx "{\"protocol\":\"flex\",\"capcode\":1000005,\"type\":\"secure\",\"secureType\":2,\"text\":\"123456789ABCDEF123455\"}")" = 202 ]'
check "binary bits not a multiple of B are refused" '[ "$(tx "{\"protocol\":\"flex\",\"capcode\":1000006,\"type\":\"binary\",\"blocking\":8,\"text\":\"123\"}")" = 400 ]'
check "binary right to left with B=1 is refused" '[ "$(tx "{\"protocol\":\"flex\",\"capcode\":1000006,\"type\":\"binary\",\"rightToLeft\":1,\"text\":\"12\"}")" = 400 ]'
check "binary not hex is refused" '[ "$(tx "{\"protocol\":\"flex\",\"capcode\":1000006,\"type\":\"binary\",\"text\":\"12G4\"}")" = 400 ]'
check "secure type 3 is refused" '[ "$(tx "{\"protocol\":\"flex\",\"capcode\":1000006,\"type\":\"secure\",\"secureType\":3,\"text\":\"12\"}")" = 400 ]'

# A page of the message center, claimed by the modulator once it has announced itself (every 5 s)
check "a binary page is accepted by the message center" '[ "$(curl_code POST /featureset/feature/0/actions "{\"featureType\":\"MessageCenter\",\"MessageCenterActions\":{\"post\":{\"id\":\"p-bin\",\"transmitter\":\"t1\",\"protocol\":\"flex\",\"address\":\"1000007\",\"type\":\"binary\",\"blocking\":8,\"mailDrop\":1,\"text\":\"A5A5\"}}}")" = 202 ]'
check "a secure page of type 3 is refused by the message center" '[ "$(curl_code POST /featureset/feature/0/actions "{\"featureType\":\"MessageCenter\",\"MessageCenterActions\":{\"post\":{\"id\":\"p-sec\",\"transmitter\":\"t1\",\"protocol\":\"flex\",\"address\":\"1000008\",\"type\":\"secure\",\"secureType\":3,\"text\":\"12\"}}}")" = 400 ]'

for i in $(seq 60); do [ "$(sent)" = "6" ] && break; sleep 1; done
check "the modulator sent the six messages" '[ "$(sent)" = "6" ]'
R=$(report)
check "the sent binary record has its header fields" '[[ "$R" == *"\"blocking\":7"*"\"display_direction\":\"rtl\""* || "$R" == *"\"display_direction\":\"rtl\""*"\"blocking\":7"* ]]'
sleep 2
curl_code DELETE /deviceset/0/device/run > /dev/null
sleep 1

# Receiver: the file played to the demodulator (FLEX only)
curl_code POST "/deviceset?direction=0" > /dev/null
sleep 0.5
curl_code PUT /deviceset/1/device '{"hwType":"FileInput","direction":0}' > /dev/null
curl_code PATCH /deviceset/1/device/settings "{\"deviceHwType\":\"FileInput\",\"direction\":0,\"fileInputSettings\":{\"fileName\":\"$OUT\",\"accelerationFactor\":1,\"loop\":0}}" > /dev/null
curl_code POST /deviceset/1/channel '{"channelType":"RadioPagingDemod","direction":0}' > /dev/null
curl_code PATCH /deviceset/1/channel/0/settings '{"channelType":"RadioPagingDemod","direction":0,"RadioPagingDemodSettings":{"pocsagEnabled":0,"flexEnabled":1,"gscEnabled":0,"flexDeviation":4800,"channelAlias":"r1"}}' > /dev/null
curl_code POST /deviceset/1/device/run > /dev/null

SECS=$(( $(stat -c %s "$OUT") / (48000 * 4) + 5 ))
for i in $(seq "$SECS"); do [ -n "$(rxRecord 1000005)" ] && [ -n "$(rxRecord 1000007)" ] && break; sleep 1; done

A=$(rxRecord 1000001)
check "binary B=8: text, 64 bits, K and signature" '[[ "$A" == *"\"text\":\"0123456789ABCDEF\""* && "$A" == *"\"hex_bits\":64"* && "$A" == *"\"blocking\":8"* && "$A" == *"\"checksum\":\"ok\""* && "$A" == *"\"signature\":\"ok\""* ]]'
A=$(rxRecord 1000002)
check "binary B=4 exact end: padding word removed, 84 bits" '[[ "$A" == *"\"text\":\"123456789ABCDEF12345F\""* && "$A" == *"\"hex_bits\":84"* && "$A" == *"\"signature\":\"ok\""* ]]'
A=$(rxRecord 1000003)
check "binary B=7: right to left, mail drop, header message" '[[ "$A" == *"\"text\":\"1234567\""* && "$A" == *"\"blocking\":7"* && "$A" == *"\"display_direction\":\"rtl\""* && "$A" == *"\"maildrop\":true"* && "$A" == *"\"header_message\":true"* && "$A" == *"\"signature\":\"ok\""* ]]'
A=$(rxRecord 1000004)
check "secure alphanumeric: text, type 0" '[[ "$A" == *"\"text\":\"Secure text\""* && "$A" == *"\"secure_type\":0"* && "$A" == *"\"checksum\":\"ok\""* ]]'
A=$(rxRecord 1000005)
check "secure binary exact end: termination word removed, 84 bits" '[[ "$A" == *"\"text\":\"123456789ABCDEF123455\""* && "$A" == *"\"secure_type\":2"* && "$A" == *"\"hex_bits\":84"* && "$A" == *"\"checksum\":\"ok\""* ]]'
A=$(rxRecord 1000007)
check "message center binary page: B=8 and mail drop received" '[[ "$A" == *"\"text\":\"A5A5\""* && "$A" == *"\"blocking\":8"* && "$A" == *"\"maildrop\":true"* && "$A" == *"\"signature\":\"ok\""* ]]'

if [ "$failed" -ne 0 ]; then
    report | sed 's/},{/}\n{/g' | grep "\"direction\":\"rx\""
fi

curl_code DELETE /deviceset/1/device/run > /dev/null
fuser -k "$PORT"/tcp > /dev/null 2>&1
rm -rf "$WORK"
echo "$failed failed"
exit $failed
