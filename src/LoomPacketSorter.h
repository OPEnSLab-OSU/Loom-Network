#pragma once
#include <stdint.h>
#include "LoomNetworkPacket.h"
#include "CircularBuffer.h"
#include "LoomNetworkUtility.h"

/**
 * Loom network packet sorter
 * Performs a few functions:
 * * Takes a data stream and converts it into packets
 * * Takes a stream of packets and converts it into a data stream
 * * Removes duplicate packets (using fingerprints to determine duplicates)
 */

namespace LoomNet {
	template <size_t StreamSizeMax = 6, size_t StreamCountMax = 2, size_t SendCountMax = 6>
	class PacketSorter {
	public:
		size_t write(const uint8_t* data, const size_t len);
		bool write(const DataPacket& packet);

		size_t read(uint8_t* dest, const size_t len);
		
		bool get_packet(Packet& dest, const uint16_t send_addr);
		
		size_t data_available() const;
		uint16_t data_from_addr() const;
		size_t packets_available() const;

	private:
		// TODO: implement recieveing packet data structure as a memory pool (use TLSF?)
		// maybe we are lazy and simply use an array of arrays for now
		CircularBuffer<CircularBuffer<Packet, StreamSizeMax>, StreamCountMax> m_recv_buffer;
		CircularBuffer<Pair<uint16_t, Packet>, SendCountMax> m_send_buffer;
	};

}