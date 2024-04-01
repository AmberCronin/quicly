#include "quicly/ss.h"
#include <stdint.h>
#include <stdio.h>

// bytes is the number of bytes acked in the last ACK frame
// inflight is sentmap->bytes_in_flight + bytes
void ss_search10(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, uint64_t largest_acked, uint32_t inflight,
                        uint64_t next_pn, int64_t now, uint32_t max_udp_payload_size)
{
	uint64_t* sent = cc->ss_state.search10.sent_bins;
	uint64_t* delv = cc->ss_state.search10.delv_bins;
	uint64_t* held_sent = &cc->ss_state.search10.held_sent;
	int64_t* bin_end = &cc->ss_state.search10.bin_end;
	uint32_t* bin_time = &cc->ss_state.search10.bin_time;
	uint32_t* last_true_inflight = &cc->ss_state.search10.last_true_inflight;
	uint8_t* bin_rounds = &cc->ss_state.search10.bin_rounds;

	// struct initializations, everything else important has already been reset to 0
	if(*bin_time == 0) {
		// bin time is the size of each of the sent/delv bins
		*bin_time = (loss->rtt.latest * SEARCH10_WINDOW_MULTIPLIER) / SEARCH10_DELV_BIN_COUNT;
		*bin_end = now + *bin_time;
		sent[0] = delv[0] = 0;
		*bin_rounds = 0;
		printf("SEARCHLOG: RESET, %li, %u, %li\n", now, *bin_time, *bin_end);
	}

	// inflight is sentmap->bytes_in_flight + bytes
	uint32_t true_bytes_in_flight = inflight - bytes;
	// to get the bytes sent between the last ack and this ack, we subtract the previous inflight (not including acked)
	// T(i) = I(i) - D(i)    <<  True Inflight Bytes = inflight - bytes (delivered)
	// S(i) = I(i) - T(i-1) = T(i) + D(i) - T(i-1)
	// On the first round, *last_true_inflight will be 0 - so the sent bytes are equal to true_inflight + delivered_bytes
	uint32_t sent_bytes = inflight - *last_true_inflight;

	// bin_shift is the number of bins to shift backwards, based on the latest RTT
	uint8_t bin_shift = loss->rtt.latest / *bin_time;
	if(bin_shift == 0) {
		bin_shift = 1;
	}
	else if(loss->rtt.latest % *bin_time > (*bin_time / 2)) {
		// round to the nearest bin (not doing interpolation yet)
		bin_shift++;
	}

	// update bin boundaries when rolled over
	while(now > (*bin_end)) {
	
		// only perform SEARCH if there is enough data in the sent bins with the current RTT
		// bin_rounds tracks how many times we've rolled over, and a single window is the entire
		// delivered bin count (because of the definition of how bin_time is calculated)
		// thus, the number of rounds must be >= than the delv bin count + the bin shift
		if((*bin_rounds) >= (SEARCH10_DELV_BIN_COUNT + bin_shift)) {
			// do SEARCH
			double sent_sum = 0, delv_sum = 0;
			for (int i = *bin_rounds; i > (*bin_rounds - SEARCH10_DELV_BIN_COUNT); i--) {
				// the value of bin_shift will always be at least 1, so the current sent bin is never used
				sent_sum += sent[((i - bin_shift) % SEARCH10_SENT_BIN_COUNT)];
				delv_sum += delv[(i % SEARCH10_DELV_BIN_COUNT)];
			}
			if (sent_sum == 0) {
				continue;
			}
			double normalized_diff = (sent_sum - delv_sum) / sent_sum;
			printf("SEARCHLOG: SEARCH, %li, %u, %u, %f, %f, %f, %u, %u\n", now, *bin_rounds, bin_shift, sent_sum, delv_sum, normalized_diff, loss->rtt.latest, true_bytes_in_flight);
			if (normalized_diff > SEARCH10_THRESH) {
				// exit slow start here
				printf("SEARCHLOG: EXIT, %li\n", now);
				if (search_exit) {
					if (cc->cwnd_maximum < cc->cwnd)
						cc->cwnd_maximum = cc->cwnd;
					cc->ssthresh = cc->cwnd;
					return;
				}
			}
		}

		printf("SEARCHLOG: BINROLL, %li, %u, %lu, %lu\n", now, *bin_time, sent[(*bin_rounds % SEARCH10_SENT_BIN_COUNT)], delv[(*bin_rounds % SEARCH10_DELV_BIN_COUNT)]);
		*bin_end += *bin_time;
		*bin_rounds += 1;
		sent[(*bin_rounds % SEARCH10_SENT_BIN_COUNT)] = delv[(*bin_rounds % SEARCH10_DELV_BIN_COUNT)] = 0;
	}

	// fill bins for the current index
	delv[(*bin_rounds % SEARCH10_DELV_BIN_COUNT)] += bytes;

	*held_sent += sent_bytes;
	uint64_t true_sent = MIN(bytes * 2, *held_sent);
	sent[(*bin_rounds % SEARCH10_SENT_BIN_COUNT)] += true_sent;
	*held_sent -= true_sent;


	// printf("SEARCHLOG: ACK, %li, %u, %li, %u, %u, %u, %u, %u, %u\n", now, *bin_time, *bin_end, bytes, sent_bytes, true_bytes_in_flight, bin_shift, loss->rtt.minimum, loss->rtt.latest);

	// perform standard SS doubling
	cc->cwnd += bytes;
	if (cc->cwnd_maximum < cc->cwnd)
		cc->cwnd_maximum = cc->cwnd;

	*last_true_inflight = true_bytes_in_flight;
	return;
}

quicly_ss_type_t quicly_ss_type_search10 = { "search10", ss_search10 };
