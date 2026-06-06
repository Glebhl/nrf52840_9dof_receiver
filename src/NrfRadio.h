#pragma once

#include <stdint.h>

#include "RadioPacket.h"

// Minimal bare-metal driver for the nRF52840 2.4 GHz RADIO peripheral, used as a
// simple ShockBurst-style packet link (dynamic length, 16-bit CRC, whitening,
// no auto-ack). One shared logical address + channel for the whole network, so
// a transmitter just sends and a receiver just listens — see RadioPacket.h.
//
// Requires that NO SoftDevice / BLE stack is active (this project is bare-metal,
// so the radio is ours to drive directly). The high-frequency crystal is started
// on begin*().
//
// A single instance is used in one role per sketch: call beginTx() XOR beginRx().
class NrfRadio {
public:
  // Configure the radio for transmitting. Returns true (kept boolean for
  // symmetry / future probing).
  bool beginTx();

  // Configure the radio and start continuous receive.
  bool beginRx();

  // Blocking send of `len` payload bytes (<= RadioLink::kMaxLen). Returns once
  // the packet has left the antenna and the radio is disabled.
  void send(const uint8_t* data, uint8_t len);

  // Non-blocking receive. If a packet arrived since the last call AND its CRC is
  // valid, copies up to `maxLen` payload bytes into `out`, writes the length to
  // `outLen`, and returns true. Returns false otherwise (no packet, or CRC fail).
  bool poll(uint8_t* out, uint8_t maxLen, uint8_t& outLen);

private:
  void startHfclk();
  void configureCommon();
  void startRx();

  // On-air buffer: byte 0 is the length field, payload follows.
  uint8_t buf_[1 + RadioLink::kMaxLen];
  bool    rxActive_ = false;
};
