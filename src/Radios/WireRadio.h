#pragma once

#include "Arduino.h"
#include "../LoomRadio.h"

/** A simple testing radio, using a wire as airwaves */

constexpr auto SLOT_LENGTH_MILLIS = 10000;
constexpr auto SEND_DELAY_MILLIS = 500;
constexpr auto WIRE_RECV_TIMEOUT_MILLIS = 500 + SEND_DELAY_MILLIS;
constexpr auto BIT_LENGTH = 400; // MUST BE DIVISIBLE BY 4

namespace LoomNet {
    class WireRadio : public Radio {
    public:

        WireRadio(const uint8_t data_pin,
            const uint8_t clk_pin, 
            const uint8_t send_indicator_pin, 
            const uint8_t recv_indicator_pin, 
            const uint8_t pwr_indicator_pin)
            : m_data_pin(data_pin)
            , m_clk_pin(clk_pin)
            , m_send_ind(send_indicator_pin)
            , m_recv_ind(recv_indicator_pin)
            , m_pwr_ind(pwr_indicator_pin) 
            , m_state(State::DISABLED)
            , m_buffer{} {}

        TimeInterval get_time() const override { 
            // get time using the internal RTC counter!
            RTC->MODE0.READREQ.reg = RTC_READREQ_RREQ;
            while (RTC->MODE0.STATUS.bit.SYNCBUSY);
            return TimeInterval(TimeInterval::Unit::MILLISECOND, RTC->MODE0.COUNT.bit.COUNT);
        }
        State get_state() const override { return m_state; }
        void enable() override {
            if (m_state != State::DISABLED) 
                Serial.println("Invalid radio state movement in enable()");
            m_state = State::SLEEP;
            // setup all pins to output and power low, except for data which needs to be input pullup
            pinMode(m_data_pin,                   INPUT);
            pinMode(m_clk_pin,                    INPUT);
            pinMode(m_send_ind,         OUTPUT);
            pinMode(m_recv_ind,         OUTPUT);
            pinMode(m_pwr_ind,          OUTPUT);
            digitalWrite(m_send_ind,    LOW);
            digitalWrite(m_recv_ind,    LOW);
            digitalWrite(m_pwr_ind,     LOW);
            // configure the internal RTC to act as our timer
            RTC->MODE2.CTRL.reg &= ~RTC_MODE0_CTRL_ENABLE; // disable RTC
            // while (RTC->MODE0.STATUS.bit.SYNCBUSY);
            RTC->MODE2.CTRL.reg |= RTC_MODE0_CTRL_SWRST; // software reset
            // while (RTC->MODE0.STATUS.bit.SYNCBUSY);
            PM->APBAMASK.reg |= PM_APBAMASK_RTC; // turn on digital interface clock
            GCLK->GENDIV.reg = GCLK_GENDIV_ID(2)|GCLK_GENDIV_DIV(5);
            while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY);
            GCLK->GENCTRL.reg = (GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_OSC8M | GCLK_GENCTRL_ID(2) | GCLK_GENCTRL_DIVSEL );
            while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY);
            GCLK->CLKCTRL.reg = (uint32_t)((GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK2 | (RTC_GCLK_ID << GCLK_CLKCTRL_ID_Pos)));
            while (GCLK->STATUS.bit.SYNCBUSY);
            RTC->MODE0.READREQ.reg &= ~RTC_READREQ_RCONT; // disable continuously mode
            // tell the RTC to operate as a 32 bit counter
            RTC->MODE0.CTRL.reg = RTC_MODE0_CTRL_MODE_COUNT32 | RTC_MODE0_CTRL_PRESCALER_DIV128;
            while (RTC->MODE0.STATUS.bit.SYNCBUSY);
            RTC->MODE0.CTRL.reg |= RTC_MODE0_CTRL_ENABLE; // enable RTC
            while (RTC->MODE0.STATUS.bit.SYNCBUSY);
            RTC->MODE0.CTRL.reg &= ~RTC_MODE0_CTRL_SWRST; // software reset remove
            while (RTC->MODE0.STATUS.bit.SYNCBUSY);
        }
        void disable() override {
            if (m_state != State::SLEEP) 
                Serial.println("Invalid radio state movement in disable()");
            m_state = State::DISABLED;
        }
        void sleep() override {
            if (m_state != State::IDLE) 
                Serial.println("Invalid radio state movement in sleep()");
            m_state = State::SLEEP;
            // turn power indicator off
            digitalWrite(m_pwr_ind, LOW);
        }
        void wake() override {
            if (m_state != State::SLEEP) 
                Serial.println("Invalid radio state movement in wake()");
            m_state = State::IDLE;
            // turn power indicator on
            digitalWrite(m_pwr_ind, HIGH);
        }
        LoomNet::Packet recv(TimeInterval& recv_stamp) override {
            if (m_state != State::IDLE) 
                Serial.println("Invalid radio state to recv");
            // check start for synchronization measurement
            const auto recv_start = get_time();
            // turn recv indicator on
            digitalWrite(m_recv_ind, HIGH);
            // start reading the data pin
            // get the buffer ready
            for (uint8_t i = 0; i < sizeof(m_buffer); i++) m_buffer[i] = 0;
            // check the slot for one second, since this is pretty high tolarance
            const uint32_t start = millis();
            bool found = false;
            while (millis() - start < WIRE_RECV_TIMEOUT_MILLIS && !found) found = found || (digitalRead(m_clk_pin) == LOW);
            const auto sync_off = get_time() - recv_start;
            // if we found something, start recieving it
            if (found) {
                // set the recv_stamp to when we first heard the signal
                recv_stamp = get_time() - TimeInterval(TimeInterval::MILLISECOND, SEND_DELAY_MILLIS);
                // read the "airwaves" 254 times!
                bool timed_out = false;
                bool last_state;
                bool cur_state = false;
                for (uint8_t i = 0; i < sizeof(m_buffer) && !timed_out; i++) {
                    for (uint8_t b = 0; b < 8 && !timed_out; b++) {
                        const unsigned long start = micros();
                        do {
                            // time out after eight bits worth of time
                            if (micros() - start > BIT_LENGTH * 7) {
                                timed_out = true;
                                break;
                            }
                            // wait for the rising edge of the clock
                            last_state = cur_state;
                            cur_state = digitalRead(m_clk_pin) == HIGH ? true : false; 
                        } while(cur_state == last_state || cur_state == false);
                        // read the line
                        m_buffer[i] |= (digitalRead(m_data_pin) == LOW ? 1 : 0) << b;
                    }
                }
                Serial.print("Off by: ");
                Serial.println(sync_off.get_time());
                /*Serial.print("Got: ");
                for (uint8_t i = 0; i < sizeof(m_buffer); i++) {
                    Serial.print("0x");
                    if (m_buffer[i] <= 0x0F) Serial.print('0');
                    Serial.print(m_buffer[i], HEX);
                    Serial.print(", ");
                }
                Serial.println();*/
            }
            // reset the indicator
            digitalWrite(m_recv_ind, LOW);
            // return data!
            return LoomNet::Packet{ m_buffer, static_cast<uint8_t>(sizeof(m_buffer)) };
        }
        void send(const LoomNet::Packet& send) override {
            if (m_state != State::IDLE) 
                Serial.println("Invalid radio state to recv");
            /*Serial.println("Transmitting: ");
            for (uint8_t i = 0; i < 64; i++) {
                Serial.print("0x");
                if (send.get_raw()[i] < 16) Serial.print('0');
                Serial.print(send.get_raw()[i], HEX);
                Serial.print(", ");
            }
            Serial.println();*/
            // wait a bit for the recieving device to initialize
            const uint32_t start = millis();
            while(millis() - start < SEND_DELAY_MILLIS);
            // turn on the indicator!
            digitalWrite(m_send_ind, HIGH);
            // initialize our data pin to output low, to send a leading one
            pinMode(m_data_pin, OUTPUT);
            pinMode(m_clk_pin, OUTPUT);
            // start writing to the "network"!
            for (uint8_t i = 0; i < send.get_packet_length(); i++) {
                for (uint8_t b = 1;;) {
                    // set the clock low, and the data pin approprietly
                    digitalWrite(m_clk_pin, LOW);
                    digitalWrite(m_data_pin, (send.get_raw()[i] & b) ? LOW : HIGH);
                    // wait to stabilize
                    delayMicroseconds(BIT_LENGTH / 2);
                    // have the recieving device read the bus
                    digitalWrite(m_clk_pin, HIGH);
                    delayMicroseconds(BIT_LENGTH / 2);
                    if (b == 0x80) break;
                    b <<= 1;
                }
            }
            // finish the transaction by holding the clock for 8 bits
            delayMicroseconds(BIT_LENGTH * 8);
            // reset the pin
            pinMode(m_clk_pin, INPUT);
            pinMode(m_data_pin, INPUT);
            // we're all done!
            digitalWrite(m_send_ind, LOW);
        }

    private:

        const uint8_t m_data_pin;
        const uint8_t m_clk_pin;
        const uint8_t m_send_ind;
        const uint8_t m_recv_ind;
        const uint8_t m_pwr_ind;
        State m_state;
        uint8_t m_buffer[LoomNet::PACKET_MAX];
        uint32_t m_cur_time;
    };
}