#include "quicly/ss.h"
#include <stdint.h>

/*
This slow start algorithm implements the SEARCH algorithm, as derived in
https://web.cs.wpi.edu/~claypool/papers/search-wowmom-24/
*/

void ss_search_reset(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, int64_t now);

void ss_search_reset(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, int64_t now)
{
	// Handy pointers to the cc struct
	uint64_t* delv = cc->ss_state.search.delv_bins;
	int64_t* bin_end = &cc->ss_state.search.bin_end;
	uint32_t* bin_time = &cc->ss_state.search.bin_time;
	uint32_t* bin_rounds = &cc->ss_state.search.bin_rounds;

	// bin time is the size of each of the sent/delv bins
	*bin_time = MAX((loss->rtt.latest * SEARCH20_WINDOW_MULTIPLIER) / (SEARCH20_DELV_BIN_COUNT), 1);
	*bin_end = now + *bin_time;
	delv[0] = 0;
	*bin_rounds = 0;
}

// bytes is the number of bytes acked in the last ACK frame
// inflight is sentmap->bytes_in_flight + bytes
void ss_search(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, uint64_t largest_acked, uint32_t inflight,
                        uint64_t next_pn, int64_t now, uint32_t max_udp_payload_size)
{
	// Handy pointers to the cc struct
	uint64_t* delv = cc->ss_state.search.delv_bins;
	int64_t* bin_end = &cc->ss_state.search.bin_end;
	uint32_t* bin_time = &cc->ss_state.search.bin_time;
	uint32_t* bin_rounds = &cc->ss_state.search.bin_rounds;

	// struct initializations, everything else important has already been reset to 0
	if(*bin_time == 0) {
		ss_search_reset(cc, loss, bytes, now);
	}

	// bin_shift is the number of bins to shift backwards, based on the latest RTT
	uint8_t bin_shift = loss->rtt.latest / *bin_time;
	if(bin_shift == 0) {
		bin_shift = 1;
	}
	else if(loss->rtt.latest % *bin_time > (*bin_time / 2)) {
		// round to the nearest bin (not doing interpolation yet)
		bin_shift++;
	}

	// Possibly add some code here for dirty reset - run when no data has been sent on the connection
	// for a very long time, but application never received a loss (and so is still in slow-start)
	// This is likely handled by the prior binroll while loop, but that might add unnecessary latency
	// dependant on how long ago the last packet was acknowledged.
	if (((now - *bin_end) / *bin_time) > SEARCH20_SENT_BIN_COUNT) {
		ss_search_reset(cc, loss, bytes, now);
	}
	

	// perform prior binrolls before updating the latest bin to run SEARCH on if necessary
	while((now - *bin_time) > (*bin_end)) {
		*bin_end += *bin_time;
		*bin_rounds += 1;
		delv[(*bin_rounds % (SEARCH20_SENT_BIN_COUNT))] = 0;
	}
	// perform current binroll
	if((now > (*bin_end))) {
		// only perform SEARCH if there is enough data in the sent bins with the current RTT
		// bin_rounds tracks how many times we've rolled over, and a single window is the entire
		// delivered bin count (because of the definition of how bin_time is calculated)
		// thus, the number of rounds must be >= than the delv bin count + the bin shift
		if((*bin_rounds) >= ((SEARCH20_DELV_BIN_COUNT) + bin_shift)
			&& bin_shift < (SEARCH20_SENT_BIN_COUNT - SEARCH20_DELV_BIN_COUNT)) {
			// do SEARCH
			double shift_delv_sum = 0, delv_sum = 0;
			for (int i = *bin_rounds; i > (*bin_rounds - (SEARCH20_DELV_BIN_COUNT)); i--) {
				// the value of bin_shift will always be at least 1, so the current sent bin is never used
				shift_delv_sum += delv[((i - bin_shift) % (SEARCH20_SENT_BIN_COUNT))];
				delv_sum += delv[(i % (SEARCH20_SENT_BIN_COUNT))];
			}
			if (shift_delv_sum >= 1) {
				shift_delv_sum *= 2;
				double normalized_diff = (shift_delv_sum - delv_sum) / shift_delv_sum;
				if (normalized_diff > SEARCH20_THRESH) {
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
		else if(bin_shift >= (SEARCH20_SENT_BIN_COUNT - SEARCH20_DELV_BIN_COUNT)) {
			/* TODO: Double bin_time and consolidate for high RTT operation */
		}

		*bin_end += *bin_time;
		*bin_rounds += 1;
		delv[(*bin_rounds % (SEARCH20_SENT_BIN_COUNT))] = 0;
	}

	// fill (updated) bin with latest acknowledged bytes
	// TCP implementation has a method of tracking total delivered bytes to avoid this per-packet
	// computation, but we aren't doing that (yet). loss->total_bytes_sent looks interesting, but
	// does not seem to guarantee a match with conn->egress.max_data.sent (see loss.c)
	delv[(*bin_rounds % (SEARCH20_SENT_BIN_COUNT))] += bytes;

	// perform standard SS doubling
	cc->cwnd += bytes;
	if (cc->cwnd_maximum < cc->cwnd)
		cc->cwnd_maximum = cc->cwnd;

	return;
}

quicly_ss_type_t quicly_ss_type_search = { "search", ss_search };
