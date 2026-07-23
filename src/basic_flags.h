/*
 *  Packet_Device Library
 *  ---------------------
 *  Description:
 *    Packet_Device is a lightweight, event-driven communication protocol
 *    built on top of Arduino's Stream interface. It provides a secure,
 *    packet-based layer for transferring raw bytes between two devices
 *    over any underlying transport supported by the Stream API
 *    (e.g., UART, SPI, I²C/Wire, etc.).
 *
 *    Conceptually, it follows a layered design: the Stream interface
 *    operates as the transport layer, while Packet_Device functions
 *    as the application protocol layer, handling packet framing,
 *    data integrity, and event-driven dispatch.
 *
 *  Features:
 *    - Works over any Stream-compatible transport (UART, SPI, I²C, etc.)
 *    - Secure, packetized byte-level communication
 *    - Event-based architecture for handling incoming data efficiently
 *    - Direct mapping of incoming data to any memory location or pointer
 *    - Easy transmission of arrays, pointers, or raw structures
 *    - Flexible enough for both command/control and bulk data transfer
 *
 *  Use Case:
 *    Ideal for connecting two devices over various physical interfaces
 *    with a unified packet protocol, without rewriting the communication
 *    logic for each transport.
 *
 *  Author: Pallob K. Gain <pallobkgain@gmail.com>
 *  License: MIT
 *  Repository: https://github.com/Pallob-Gain/packet_device
 *  Created: 2 Feb, 2025
 *  Modified: 21 July, 2026
 */

#pragma once

#define MAX_COMMAND_QUEUE_LEN 5 // maximum 5 commands at once (default)

// Packet Details header: <[]-[]*[]-[]>
#define PACKET_SIGNETURE_DATA_LEN 4
#define PACKET_SIGNETURE_LEN 9

#define TRANSFER_DATA_TEXT_HEADER_LEN 4 //buff_signeture(1 byte)+data_signeture(1 byte) +data_len(2 bytes)

#define TRANSFER_DATA_PARAMS_HEADER_LEN 6 //buff_signeture(1 byte)+data_signeture(1 byte)+data_type(1 byte)+pram_len(1 bytes)+data_len(2 bytes)
#define TRANSFER_DATA_ARRAY_HEADER_LEN 7 //buff_signeture(1 byte)+data_signeture(1 byte)+type(1 bytes)+type_size(1 bytes)+pram_len(1 bytes)+data_size(2 bytes)