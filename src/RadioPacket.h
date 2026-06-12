#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Shared radio link + packet protocol for the nRF52840 tracker network.
//
// This file is the single source of truth for the over-the-air format and the
// physical-layer parameters. It is COPIED VERBATIM into both the transmitter
// (tracker) and the receiver sketch folders — Arduino compiles each sketch from
// its own folder, so the two copies must be kept byte-for-byte identical or the
// link silently breaks (mismatched address/channel = no packets; mismatched
// layout = garbage decode).
//
// Topology: one receiver sends a short frame beacon, then transmitters reply in
// fixed TDMA slots on the same RF channel. Each transmitter must have a unique
// slot number for the active tracker set.
// ---------------------------------------------------------------------------

namespace RadioLink {

// --- Physical layer (MUST match on both ends) ------------------------------
// RF channel: actual frequency = 2400 + kChannel MHz. 76 -> 2476 MHz, above
// the busiest Wi-Fi band. Valid 0..100.
static const uint8_t  kChannel = 76;

// On-air data rate. Options (RADIO_MODE_MODE_*): Nrf_1Mbit, Nrf_2Mbit.
// 2 Mbit halves air-time per packet (better for many fast trackers) at slightly
// shorter range — fine for on-body distances.
#define RADIOLINK_MODE_NRF_2MBIT 1   // 0 -> 1 Mbit, 1 -> 2 Mbit

// Shared 5-byte logical address (4-byte BASE0 + 1-byte PREFIX0). Pick anything,
// just keep it identical on both ends and distinct from neighbouring setups.
static const uint32_t kBase0   = 0x9D0F7AC3;  // "9DOF tracker"
static const uint8_t  kPrefix0 = 0xE7;

// Largest payload (bytes, excluding the on-air length byte). Worst-case packet
// is header(8) + every field(68) = 76; 96 leaves headroom.
static const uint8_t  kMaxLen  = 96;

// --- TDMA timing -----------------------------------------------------------
// 30 Hz frame. The receiver sends one beacon at the start of each frame, then
// trackers transmit in evenly spaced slots after this guard interval.
static const uint32_t kTdmaFrameUs       = 33333UL;
static const uint8_t  kTdmaSlotCount     = 5;
static const uint32_t kTdmaBeaconGuardUs = 1000UL;

// --- Packet protocol -------------------------------------------------------
// Layout (little-endian), header is fixed, fields follow in flag-bit order and
// only the enabled ones are present:
//
//   off 0 : uint8  magic       (kMagic)
//   off 1 : uint8  flags       (bitmask of Field below)
//   off 2 : uint32 devId       (NRF_FICR->DEVICEID[0], the "MAC")
//   off 6 : uint16 seq         (per-transmitter packet counter, wraps)
//   off 8 : float  fields...   (present iff the matching flag bit is set)
//
// The flags byte is carried in every packet, so the receiver decodes any
// transmitter without sharing its compile-time field selection.
static const uint8_t kMagic = 0xA7;
static const uint8_t kBeaconMagic = 0x5B;

// Beacon layout:
//   off 0 : uint8  magic       (kBeaconMagic)
//   off 1 : uint8  slotCount   (kTdmaSlotCount)
//   off 2 : uint16 frameSeq    (receiver frame counter, wraps)
//   off 4 : uint32 frameUs     (kTdmaFrameUs)
static const uint8_t kBeaconLen = 8;

enum Field : uint8_t {
  kQuat  = 1 << 0,  // 4 floats: w, x, y, z
  kRpy   = 1 << 1,  // 3 floats: roll, pitch, yaw (deg)
  kAccel = 1 << 2,  // 3 floats: x, y, z (g)
  kGyro  = 1 << 3,  // 3 floats: x, y, z (deg/s)
  kTemp  = 1 << 4,  // 1 float : Celsius
  kMag   = 1 << 5,  // 3 floats: x, y, z (Gauss)
};

static const uint8_t kHeaderLen = 8;

// Number of float values carried by a given field flag (0 if not a single bit).
inline uint8_t fieldFloatCount(uint8_t flag) {
  switch (flag) {
    case kQuat:  return 4;
    case kRpy:   return 3;
    case kAccel: return 3;
    case kGyro:  return 3;
    case kTemp:  return 1;
    case kMag:   return 3;
    default:     return 0;
  }
}

}  // namespace RadioLink
