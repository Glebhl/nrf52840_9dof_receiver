#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

#include "NrfRadio.h"
#include "RadioPacket.h"

// ---------------------------------------------------------------------------
// nRF52840 tracker receiver / serial bridge.
//
// Listens on the shared link (see RadioPacket.h) for packets from any number of
// transmitters and dumps each one to USB serial. The radio already verifies a
// 16-bit CRC, so anything delivered here is intact. Optimised for a fast stream:
// the loop does nothing but poll the radio and push bytes out.
//
// Two output modes (RX_OUTPUT_TEXT):
//   0 (default) — binary frames, fast and host-parseable:
//        0xAA 0x55 | len | payload[len] | crc8(payload)
//      The payload is the raw radio packet (magic, flags, devId, seq, fields).
//   1 — human-readable text lines, for debugging in a serial monitor.
// ---------------------------------------------------------------------------
#define RX_OUTPUT_TEXT 0

#define RX_SERIAL_BAUD 921600
// Frame sync bytes for binary output.
#define FRAME_SYNC0 0xAA
#define FRAME_SYNC1 0x55

static NrfRadio radio;
static uint16_t g_beaconSeq = 0;
static uint32_t g_nextBeaconAtUs = 0;

// CRC-8 (poly 0x07, init 0x00) over the forwarded payload, so the host can
// reject any byte the USB link mangled.
static uint8_t crc8(const uint8_t* data, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

#if RX_OUTPUT_TEXT
// Decode and pretty-print one packet. Tolerates any transmitter's flag set.
static void printPacketText(const uint8_t* p, uint8_t len) {
  if (len < RadioLink::kHeaderLen || p[0] != RadioLink::kMagic) {
    Serial.println("(bad packet)");
    return;
  }
  const uint8_t  flags = p[1];
  uint32_t devId; memcpy(&devId, &p[2], 4);
  uint16_t seq;   memcpy(&seq,   &p[6], 2);

  Serial.print("id=0x"); Serial.print(devId, HEX);
  Serial.print(" seq="); Serial.print(seq);

  uint8_t pos = RadioLink::kHeaderLen;
  static const struct { uint8_t flag; const char* name; } kOrder[] = {
    { RadioLink::kQuat,  " Q="  }, { RadioLink::kRpy,   " RPY=" },
    { RadioLink::kAccel, " A="  }, { RadioLink::kGyro,  " G="   },
    { RadioLink::kTemp,  " T="  }, { RadioLink::kMag,   " M="   },
  };
  for (auto& f : kOrder) {
    if (!(flags & f.flag)) continue;
    const uint8_t n = RadioLink::fieldFloatCount(f.flag);
    Serial.print(f.name);
    for (uint8_t i = 0; i < n; ++i) {
      if (pos + 4 > len) { Serial.print("?"); break; }
      float v; memcpy(&v, &p[pos], 4); pos += 4;
      Serial.print(v, 3);
      if (i + 1 < n) Serial.print(',');
    }
  }
  Serial.println();
}
#else
// Forward one packet as a binary frame.
static void forwardPacketBinary(const uint8_t* p, uint8_t len) {
  uint8_t hdr[3] = { FRAME_SYNC0, FRAME_SYNC1, len };
  Serial.write(hdr, sizeof(hdr));
  Serial.write(p, len);
  const uint8_t c = crc8(p, len);
  Serial.write(&c, 1);
}
#endif

static void sendBeacon() {
  uint8_t pkt[RadioLink::kBeaconLen];
  uint8_t pos = 0;

  pkt[pos++] = RadioLink::kBeaconMagic;
  pkt[pos++] = RadioLink::kTdmaSlotCount;
  memcpy(&pkt[pos], &g_beaconSeq, sizeof(g_beaconSeq)); pos += sizeof(g_beaconSeq);
  const uint32_t frameUs = RadioLink::kTdmaFrameUs;
  memcpy(&pkt[pos], &frameUs, sizeof(frameUs));

  g_beaconSeq++;
  radio.send(pkt, sizeof(pkt));
}

void setup() {
  Serial.begin(RX_SERIAL_BAUD);
  const unsigned long start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }

  radio.beginRx();
  g_nextBeaconAtUs = micros();

#if RX_OUTPUT_TEXT
  Serial.println();
  Serial.println("RX ready (text mode)");
#endif
}

void loop() {
  const uint32_t nowUs = micros();
  if ((int32_t)(nowUs - g_nextBeaconAtUs) >= 0) {
    sendBeacon();
    g_nextBeaconAtUs += RadioLink::kTdmaFrameUs;
    if ((int32_t)(nowUs - g_nextBeaconAtUs) >= 0) {
      g_nextBeaconAtUs = nowUs + RadioLink::kTdmaFrameUs;
    }
  }

  uint8_t buf[RadioLink::kMaxLen];
  uint8_t len = 0;

  // Drain everything available this iteration to keep up with bursts.
  while (radio.poll(buf, sizeof(buf), len)) {
    if (len < RadioLink::kHeaderLen || buf[0] != RadioLink::kMagic) {
      continue;  // not one of ours (or truncated) — skip
    }
#if RX_OUTPUT_TEXT
    printPacketText(buf, len);
#else
    forwardPacketBinary(buf, len);
#endif
  }
}
