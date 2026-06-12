#include "NrfRadio.h"

#include <string.h>

#include "nrf.h"

#if RADIOLINK_MODE_NRF_2MBIT
  #define RADIOLINK_RADIO_MODE RADIO_MODE_MODE_Nrf_2Mbit
#else
  #define RADIOLINK_RADIO_MODE RADIO_MODE_MODE_Nrf_1Mbit
#endif

void NrfRadio::startHfclk() {
  // The RADIO needs the external high-frequency crystal (HFXO). USB may already
  // have started it; starting again when running is a no-op.
  if ((NRF_CLOCK->HFCLKSTAT &
       (CLOCK_HFCLKSTAT_SRC_Msk | CLOCK_HFCLKSTAT_STATE_Msk)) ==
      ((CLOCK_HFCLKSTAT_SRC_Xtal << CLOCK_HFCLKSTAT_SRC_Pos) |
       (CLOCK_HFCLKSTAT_STATE_Running << CLOCK_HFCLKSTAT_STATE_Pos))) {
    return;
  }
  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  NRF_CLOCK->TASKS_HFCLKSTART = 1;
  while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0) {
  }
}

void NrfRadio::disableRadio() {
  if ((NRF_RADIO->STATE & RADIO_STATE_STATE_Msk) ==
      (RADIO_STATE_STATE_Disabled << RADIO_STATE_STATE_Pos)) {
    rxActive_ = false;
    return;
  }

  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  while (NRF_RADIO->EVENTS_DISABLED == 0) {
  }
  NRF_RADIO->EVENTS_DISABLED = 0;
  rxActive_ = false;
}

void NrfRadio::configureCommon() {
  startHfclk();

  disableRadio();

  NRF_RADIO->POWER = 1;

  NRF_RADIO->MODE = (RADIOLINK_RADIO_MODE << RADIO_MODE_MODE_Pos);
  NRF_RADIO->FREQUENCY = RadioLink::kChannel;
  NRF_RADIO->TXPOWER = (RADIO_TXPOWER_TXPOWER_0dBm << RADIO_TXPOWER_TXPOWER_Pos);

  // Shared 5-byte logical address 0 (4-byte base + 1-byte prefix).
  NRF_RADIO->BASE0 = RadioLink::kBase0;
  NRF_RADIO->PREFIX0 =
      (NRF_RADIO->PREFIX0 & ~RADIO_PREFIX0_AP0_Msk) |
      ((uint32_t)RadioLink::kPrefix0 << RADIO_PREFIX0_AP0_Pos);
  NRF_RADIO->TXADDRESS = 0;
  NRF_RADIO->RXADDRESSES = (1UL << 0);

  // 1-byte dynamic length field, no S0/S1.
  NRF_RADIO->PCNF0 = (8UL << RADIO_PCNF0_LFLEN_Pos) |
                     (0UL << RADIO_PCNF0_S0LEN_Pos) |
                     (0UL << RADIO_PCNF0_S1LEN_Pos);

  NRF_RADIO->PCNF1 =
      ((uint32_t)RadioLink::kMaxLen << RADIO_PCNF1_MAXLEN_Pos) |
      (0UL << RADIO_PCNF1_STATLEN_Pos) |
      (4UL << RADIO_PCNF1_BALEN_Pos) |  // 4 base bytes + 1 prefix = 5-byte addr
      (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) |
      (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos);

  // 16-bit CRC (CCITT), computed over the payload (address skipped).
  NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) |
                      (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
  NRF_RADIO->CRCPOLY = 0x11021UL;
  NRF_RADIO->CRCINIT = 0xFFFFUL;

  // Whitening initial value — any non-zero constant works as long as both ends
  // agree; the channel index is the conventional choice.
  NRF_RADIO->DATAWHITEIV = RadioLink::kChannel;

  NRF_RADIO->PACKETPTR = (uint32_t)buf_;
}

bool NrfRadio::beginTx() {
  configureCommon();
  rxActive_ = false;
  return true;
}

bool NrfRadio::beginRx() {
  configureCommon();
  startRx();
  return true;
}

void NrfRadio::send(const uint8_t* data, uint8_t len) {
  disableRadio();

  if (len > RadioLink::kMaxLen) len = RadioLink::kMaxLen;
  buf_[0] = len;
  memcpy(&buf_[1], data, len);

  NRF_RADIO->PACKETPTR = (uint32_t)buf_;
  NRF_RADIO->TXADDRESS = 0;

  // READY->START ramps straight into transmit; END->DISABLE powers the PA down
  // when the packet is out.
  NRF_RADIO->SHORTS =
      (RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos) |
      (RADIO_SHORTS_END_DISABLE_Enabled << RADIO_SHORTS_END_DISABLE_Pos);

  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->EVENTS_END = 0;
  NRF_RADIO->TASKS_TXEN = 1;

  while (NRF_RADIO->EVENTS_DISABLED == 0) {
  }
  NRF_RADIO->EVENTS_DISABLED = 0;
  rxActive_ = false;
}

void NrfRadio::startRx() {
  disableRadio();

  NRF_RADIO->PACKETPTR = (uint32_t)buf_;
  // Only READY->START: after END the radio idles in RXIDLE, ready to be
  // re-armed with a single TASKS_START (no ramp-up) from poll().
  NRF_RADIO->SHORTS =
      (RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos);
  NRF_RADIO->EVENTS_END = 0;
  NRF_RADIO->TASKS_RXEN = 1;
  rxActive_ = true;
}

bool NrfRadio::poll(uint8_t* out, uint8_t maxLen, uint8_t& outLen) {
  if (!rxActive_) startRx();

  if (NRF_RADIO->EVENTS_END == 0) {
    return false;
  }
  NRF_RADIO->EVENTS_END = 0;

  const bool crcOk =
      (NRF_RADIO->CRCSTATUS == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  uint8_t len = buf_[0];
  if (len > RadioLink::kMaxLen) len = RadioLink::kMaxLen;

  // Copy out of the DMA buffer before re-arming, so an immediately following
  // packet can't overwrite it mid-copy.
  bool delivered = false;
  if (crcOk) {
    if (len > maxLen) len = maxLen;
    memcpy(out, &buf_[1], len);
    outLen = len;
    delivered = true;
  }

  // Re-arm: RXIDLE -> RX.
  NRF_RADIO->TASKS_START = 1;
  return delivered;
}
