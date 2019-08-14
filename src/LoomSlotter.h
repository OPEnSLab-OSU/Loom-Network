#pragma once
#include <cstdint>
#include "LoomNetworkUtility.h"

/**
 * Class containg some useful information and calculations for time slot information
 * meant to be generated once
 */

namespace LoomNet {
	
	class Slotter {
	public:
		enum class State {
			SLOT_SEND,
			SLOT_RECV,
			SLOT_WAIT_REFRESH,
			SLOT_ERROR
		};

		Slotter(	const uint8_t send_slot, 
					const uint8_t total_slots,
					const uint8_t cycles_per_refresh,
					const uint8_t send_count,
					const uint8_t recv_slot, 
					const uint8_t recv_count)
			: m_send_slot(send_slot)
			, m_send_count(send_count)
			, m_recv_slot(recv_slot)
			, m_recv_count(recv_count)
			, m_total_slots(total_slots)
			, m_cycles_per_refresh(cycles_per_refresh)
			, m_state(send_slot != SLOT_ERROR && recv_slot != SLOT_ERROR ? State::SLOT_WAIT_REFRESH : State::SLOT_ERROR)
			, m_cur_cycle(0)
			, m_cur_device(0) {}
		
		Slotter(const uint8_t send_slot, const uint8_t total_slots, const uint8_t cycles_per_refresh)
			: Slotter(send_slot, total_slots, cycles_per_refresh, 1, SLOT_NONE, 0) {}

		bool operator==(const Slotter& rhs) const {
			return (rhs.m_send_slot == m_send_slot)
				&& (rhs.m_send_count == m_send_count)
				&& (rhs.m_recv_slot == m_recv_slot)
				&& (rhs.m_recv_count == m_recv_count)
				&& (rhs.m_total_slots == m_total_slots);
		}

		uint8_t get_send_slot() const { return m_send_slot; }
		uint8_t get_recv_slot() const { return m_recv_slot; }

		State get_state() const { return m_state; }
		State next_state() {
			// if we were waiting for a refresh, we wait for children next
			// if we don't have any children then parent
			if (m_state == State::SLOT_ERROR) return m_state;
			else if (m_state == State::SLOT_WAIT_REFRESH) {
				if (m_recv_slot == SLOT_NONE) m_state = State::SLOT_SEND;
				else m_state = State::SLOT_RECV;
				m_cur_device = 0;
			}
			// else if we were waiting to recieve, start waiting to send if we are out of children
			else if (m_state == State::SLOT_RECV && ++m_cur_device == m_recv_count) {
				if (m_send_slot != SLOT_NONE) m_state = State::SLOT_SEND;
				m_cur_device = 0;
			}				
			// else we were waiting to transmit, so move to the next cycle
			else if (m_state == State::SLOT_SEND && ++m_cur_device == m_send_count) {
				if (++m_cur_cycle == m_cycles_per_refresh) {
					m_cur_cycle = 0;
					m_state = State::SLOT_WAIT_REFRESH;
				}
				else {
					// or reset
					if (m_recv_slot == SLOT_NONE) m_state = State::SLOT_SEND;
					else m_state = State::SLOT_RECV;
				}
				m_cur_device = 0;
			}
			return m_state;
		}

		uint8_t get_slot_wait() const {
			// if we're waiting for the first send slot in a cycle
			if (m_state == State::SLOT_SEND && m_cur_device == 0) {
				// special case: it's not the first cycle (we don't have to trigger the refresh gap) 
				// and the device is an end device (no recv slots).
				// we only trigger this special case if the device is an end device because otherwise 
				// the refresh gap is accounted for during the SLOT_RECV state.
				if (m_cur_cycle != 0 && m_recv_slot == SLOT_NONE)
					return m_total_slots + CYCLE_GAP - 1;
				// if the device is an end device, wait for the send slot plus the cycle slot gap
				// plus the group gap if needed
				else if (m_recv_slot == SLOT_NONE) {
					if (m_cur_cycle == 0) return get_send_slot() + CYCLE_GAP + BATCH_GAP;
					else return get_send_slot() + CYCLE_GAP;
				}
				// else wait for the number of slots between the end of the recv slot and 
				// the begining of the send slot (the refresh gap is accounted for in the
				// SLOT_RECV state).
				else 
					return get_send_slot() - (get_recv_slot() + m_recv_count - 1) - 1;
			}
			// else if we're waiting for the first recv slot in a cycle
			if (m_state == State::SLOT_RECV && m_cur_device == 0) {
				// if it's not the first cycle we don't account for the refresh gap
				if (m_cur_cycle != 0) {
					// if we're a coordinator (no send slots)
					if (m_send_slot != SLOT_NONE)
						return m_total_slots + CYCLE_GAP - (get_send_slot() + m_send_count - get_recv_slot());
					else 
						return m_total_slots + CYCLE_GAP - m_recv_count - 1;
				}
				// special case: if it's the very first cycle, we need to account for the refresh gap
				// (gap between the end of the refresh process and the begining of data transmission).
				// since only a router/coordinator can be in the SLOT_RECV state, we know that
				// it needs the number of slots to the recv slot plus the refresh gap
				else return get_recv_slot() + BATCH_GAP;
			}
			// TODO: "next slot" timing
			// else we're either waiting for a time interval or a consecutive slot
			// if we are waiting for a time interval (SLOT_WAIT_REFRESH) this value
			// should be ignored.
			else return 0;
		}

		void reset() {
			m_state = State::SLOT_WAIT_REFRESH;
			m_cur_cycle = 0;
			m_cur_device = 0;
		}

	private:
		const uint8_t m_send_slot;
		const uint8_t m_send_count;
		const uint8_t m_recv_slot;
		const uint8_t m_recv_count;
		const uint8_t m_total_slots;
		const uint8_t m_cycles_per_refresh;
		State m_state;
		uint8_t m_cur_cycle;
		uint8_t m_cur_device;
	};

	const Slotter SLOTTER_ERROR = Slotter(SLOT_ERROR, 0, 0, 0, SLOT_ERROR, 0);
}