/*
 * Copyright (c) 2024 Viasat Inc.
 * Authors:  Amber Cronin, Jae Won Chung, Mike Foxworthy, Vittorio Parrella, Feng Li, Mark Claypool
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "quicly/ss.h"
#include <stdint.h>

/*
 * Slow start Exit At Right CHokepoint (SEARCH) is a slow start algorithm to detect 
 * the right exit point from slow start when reaching the maximum link capacity. It 
 * keeps tracking delivery rate and exits from slow start when delivery rate is not 
 * increasing as expected. SEARCH has been implemented and evaluated with in QUIC and 
 * Linux TCP (as a kernel module). 
 *
 * References: 
 *  [1] Amber Cronin, Maryam Ataei Kachooei, Jae Chung, Feng Li, Benjamin Peters, and Mark Claypool.
 *      Improving QUIC Slow Start Behavior in Wireless Networks with SEARCH, In Proceedings of the IEEE
 *      Local and Metropolitan Area Conference (LANMAN), Boston, MA, USA, July 2024.
 *
 *  [2] Maryam Ataei Kachooei, Jae Chung, Feng Li, Benjamin Peters, Josh Chung, and
 *      Mark Claypool. Improving TCP Slow Start Performance in Wireless Networks with
 *      SEARCH, In Proceedings of the World of Wireless, Mobile and Multimedia Networks
 *      (WoWMoM), Perth, Australia June 2024.
 *
 */

// define this internally, it's a "private" function...
void ss_search_reset(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, int64_t now);

void ss_search_reset(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, int64_t now)
{
    // Handy pointers to the cc struct
    QUICLY_SEARCH_BIN_TYPE* delv = cc->ss_state.search.delv_bins;
    int64_t* bin_end = &cc->ss_state.search.bin_end;
    uint32_t* bin_time = &cc->ss_state.search.bin_time;
    uint16_t* bin_rounds = &cc->ss_state.search.bin_rounds;
    uint8_t* bin_bitshifts = &cc->ss_state.search.bin_bitshift;

    // bin time is the size of each of the sent/delv bins
    uint32_t tmp_bin_time = (loss->rtt.latest * QUICLY_SEARCH_WINDOW_MULTIPLIER) / (QUICLY_SEARCH_DELV_BIN_COUNT);
    *bin_time = tmp_bin_time < 1 ? 1 : tmp_bin_time;
    *bin_end = now + *bin_time;
    delv[0] = bytes;
    *bin_rounds = 0;
    *bin_bitshifts = 0;
}

#define SEARCH_BIN(delv, index) (delv[(index) % (QUICLY_SEARCH_TOTAL_BIN_COUNT)])

double y0(double x0, double x2, double y1, double y2) {
    double m = (y2 - y1) / (x2 - 0);
    return (m * (x0 - 0)) + y1;
}

// bytes is the number of bytes acked in the last ACK frame
// inflight is sentmap->bytes_in_flight + bytes
void ss_search(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, uint64_t largest_acked, uint32_t inflight,
                        uint64_t next_pn, int64_t now, uint32_t max_udp_payload_size)
{
    // Handy pointers to the cc struct
    QUICLY_SEARCH_BIN_TYPE* delv = cc->ss_state.search.delv_bins;
    int64_t* bin_end = &cc->ss_state.search.bin_end;
    uint32_t* bin_time = &cc->ss_state.search.bin_time;
    uint16_t* bin_rounds = &cc->ss_state.search.bin_rounds;
    uint8_t* bin_bitshifts = &cc->ss_state.search.bin_bitshift;

    // struct initializations, everything else important has already been reset to 0
    if(*bin_time == 0) {
        ss_search_reset(cc, loss, (bytes >> *bin_bitshifts), now);
    }

    // perform a reset if a certain number of bins were missed (application limited case).
    // non-application-limited delay will trigger a protocol reset if in-flight packets
    // are not acked (typical case, not handled by us).
    if (((now - *bin_end) / *bin_time) >= QUICLY_SEARCH_RESET_LIMIT) {
        ss_search_reset(cc, loss, (bytes >> *bin_bitshifts) + SEARCH_BIN(delv, *bin_rounds), now);
        
    }

    // perform prior binrolls before updating the latest bin to run SEARCH on if necessary
    while((now - *bin_time) > (*bin_end)) {
        // initialize the next bin with the final value of the previous
        SEARCH_BIN(delv, *bin_rounds + 1) = SEARCH_BIN(delv, *bin_rounds);
        *bin_end += *bin_time;
        *bin_rounds += 1;
    }
    // perform current binroll
    if((now > (*bin_end))) {
        // only perform SEARCH if there is enough data in the sent bins with the current RTT
        // bin_rounds tracks how many times we've rolled over, and a single window is the entire
        // delivered bin count (because of the definition of how bin_time is calculated)
        // thus, the number of rounds must be >= than the delv bin count + the bin shift

        // bin_shift is the number of bins to shift backwards, based on the latest RTT
        uint8_t bin_shift = loss->rtt.latest / *bin_time;
        uint32_t bin_shift_frac = loss->rtt.latest % *bin_time;
        
        // we actually need (QUICLY_SEARCH_DELV_BIN_COUNT + 1) bins because of the fractional usage
        if((*bin_rounds) >= ((QUICLY_SEARCH_DELV_BIN_COUNT) + bin_shift)
            && bin_shift < (QUICLY_SEARCH_TOTAL_BIN_COUNT - QUICLY_SEARCH_DELV_BIN_COUNT - 1)) {
            // do SEARCH
            double shift_delv_sum, delv_sum;
            if (bin_shift_frac) {
                double shift_delv_front = y0(bin_shift_frac, *bin_time,                             \
                    SEARCH_BIN(delv, *bin_rounds - bin_shift - 1),                                  \
                    SEARCH_BIN(delv, *bin_rounds - bin_shift));
                double shift_delv_back = y0(bin_shift_frac, *bin_time,                              \
                    SEARCH_BIN(delv, *bin_rounds - bin_shift - QUICLY_SEARCH_DELV_BIN_COUNT - 1),   \
                    SEARCH_BIN(delv, *bin_rounds - QUICLY_SEARCH_DELV_BIN_COUNT - bin_shift));
                
                double delv_front = y0(bin_shift_frac, *bin_time,                                   \
                    SEARCH_BIN(delv, *bin_rounds - 1),                                              \
                    SEARCH_BIN(delv, *bin_rounds));
                double delv_back = y0(bin_shift_frac, *bin_time,                                    \
                    SEARCH_BIN(delv, *bin_rounds - QUICLY_SEARCH_DELV_BIN_COUNT - 1),               \
                    SEARCH_BIN(delv, *bin_rounds - QUICLY_SEARCH_DELV_BIN_COUNT));
                shift_delv_sum = shift_delv_front - shift_delv_back;
                delv_sum = delv_front - delv_back;
            }
            else {
                shift_delv_sum = SEARCH_BIN(delv, *bin_rounds - bin_shift) -                        \
                    SEARCH_BIN(delv, *bin_rounds - QUICLY_SEARCH_DELV_BIN_COUNT - bin_shift);
                delv_sum = SEARCH_BIN(delv, *bin_rounds) -                                          \
                    SEARCH_BIN(delv, *bin_rounds - QUICLY_SEARCH_DELV_BIN_COUNT);
            }

            if (shift_delv_sum >= 1) {
                shift_delv_sum *= 2;
                double normalized_diff = (shift_delv_sum - delv_sum) / shift_delv_sum;
                if (normalized_diff > QUICLY_SEARCH_THRESH) {
                    // exit slow start
                    // TODO: Proposal to lower cwnd by tracked previously sent bytes
                    if (cc->cwnd_maximum < cc->cwnd)
                        cc->cwnd_maximum = cc->cwnd;
                    cc->ssthresh = cc->cwnd;
                    cc->cwnd_exiting_slow_start = cc->cwnd;
                    cc->exit_slow_start_at = now;
                    return;
                }
            }
        }
        else if(bin_shift >= (QUICLY_SEARCH_TOTAL_BIN_COUNT - QUICLY_SEARCH_DELV_BIN_COUNT)) {
            /* TODO: Double bin_time and consolidate for high RTT operation */
        }

        SEARCH_BIN(delv, *bin_rounds + 1) = SEARCH_BIN(delv, *bin_rounds);
        *bin_end += *bin_time;
        *bin_rounds += 1;
    }

    // check if the cummulation of acked bytes will overflow the bin
    while((SEARCH_BIN(delv, *bin_rounds) + (bytes >> *bin_bitshifts)) < SEARCH_BIN(delv, *bin_rounds)) {
        // calculate the maximum number of rounds that should be performed to shift all relevant bins
        int cycles = (*bin_rounds > QUICLY_SEARCH_TOTAL_BIN_COUNT) ? QUICLY_SEARCH_TOTAL_BIN_COUNT : *bin_rounds;
        for(int i = *bin_rounds; i > *bin_rounds - cycles; i--) {
            // shift each bin
            SEARCH_BIN(delv, i) = SEARCH_BIN(delv, i) >> 1;
        }
        *bin_bitshifts += 1;
    }

    // fill latest bin with latest acknowledged bytes
    // TCP implementation has a method of tracking total delivered bytes to avoid this per-packet
    // computation, but we aren't doing that (yet). loss->total_bytes_sent looks interesting, but
    // does not seem to guarantee a match with conn->egress.max_data.sent (see loss.c)
    //
    // we include a right-bitshift to reduce the amount of data being stored in the bin
    SEARCH_BIN(delv, *bin_rounds) += (bytes >> *bin_bitshifts);

    // perform standard SS doubling
    cc->cwnd += bytes;
    if (cc->cwnd_maximum < cc->cwnd)
        cc->cwnd_maximum = cc->cwnd;
}

quicly_ss_type_t quicly_ss_type_search = { "search", ss_search };
