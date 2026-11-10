// SPDX-License-Identifier: MIT
// Variant facts MeshCore expects to find as <target.h> (Wio Tracker L2 Pro).
#pragma once

#define P_LORA_NSS     21
#define P_LORA_DIO_1    9
#define P_LORA_RESET    7
#define P_LORA_BUSY     8
#define P_LORA_SCLK     4
#define P_LORA_MISO     5
#define P_LORA_MOSI     6

#define LORA_FREQ      869.618f
#define LORA_BW        62.5f
#define LORA_SF        8
#define LORA_CR        8
#define LORA_TX_POWER  22

#define SX126X_DIO2_AS_RF_SWITCH true
#define SX126X_DIO3_TCXO_VOLTAGE 1.8f
