#include "quicly/ss.h"
#include <stdint.h>
#include <stdio.h>

#define QUICLY_HYSTART_MIN_SAMPLES (8)
#define QUICLY_HYSTART_MIN_SSTHRESH (16)
#define QUICLY_HYSTART_ACK_DELTA_MS (2)
#define QUICLY_HYSTART_DELAY_MIN (4)
#define QUICLY_HYSTART_DELAY_MAX (16)
#define QUICLY_HYSTART_LOW_WINDOW (16)


static void ss_hystart_reset(quicly_cc_t *cc, int64_t now, uint64_t next_pn) {
	printf("HYSTART: RESET, %ld, %lu\n", now, next_pn);
	cc->ss_state.hystart.round_start = cc->ss_state.hystart.last_ack = now; // we only have ms resolution
	cc->ss_state.hystart.end_seq = next_pn; // next packet we will send
	cc->ss_state.hystart.min_round_rtt = ~0U; // max uint32_t
	cc->ss_state.hystart.samples = 0;
}

// This code is, again, heavily based on Linux's CUBIC/HyStart implementation with several functional differences to make it
// work with the constraints we've been given by Quicly. 
void ss_hystart(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, uint64_t largest_acked, uint32_t inflight,
                        uint64_t next_pn, int64_t now, uint32_t max_udp_payload_size)
{
	printf("HYSTART: ACK, %ld, %lu, %lu, %ld\n", now, largest_acked, next_pn, cc->ss_state.hystart.end_seq);
	// RTT available at loss->rtt.latest
	// dMin (min observed RTT) available at loss->rtt.minimum
	// this isn't exactly sequence number comparison, but it also ain't exactly NOT sequence number comparison...
	if (largest_acked > cc->ss_state.hystart.end_seq) {
		ss_hystart_reset(cc, now, next_pn);
	}


	// check for ACK train
	// Linux does this in usec, but we aren't so lucky
	if (now - cc->ss_state.hystart.last_ack <= QUICLY_HYSTART_ACK_DELTA_MS) {
		cc->ss_state.hystart.last_ack = now;

		// Kernel defines threshold as `ca->delay_min + hystart_ack_delay(sk);`
		// hystart_ack_delay(sk) maxes at 1ms if there is packet spacing happening
		// but we don't have usec so i dont know how much it matters
		uint32_t threshold = loss->rtt.minimum >> 1;
		printf("HYSTART: TRAIN, %ld, %u\n", now, threshold);

		if (now - cc->ss_state.hystart.round_start > threshold) {
			cc->ss_state.hystart.found = 1;
			cc->ssthresh = cc->cwnd;
			printf("HYSTART: TRAINEXIT, %ld, %u\n", now, cc->ssthresh);
		}
	}

	// HyStart Delay exit point
	if (cc->ss_state.hystart.min_round_rtt > loss->rtt.latest) {
		cc->ss_state.hystart.min_round_rtt = loss->rtt.latest;
	}
	if (cc->ss_state.hystart.samples < QUICLY_HYSTART_MIN_SAMPLES) {
		cc->ss_state.hystart.samples += 1;
	}
	else {
		uint32_t threshold = loss->rtt.minimum + MIN(QUICLY_HYSTART_DELAY_MAX, MAX(QUICLY_HYSTART_DELAY_MIN, loss->rtt.minimum >> 3));
		printf("HYSTART: DELAY, %ld, %u\n", now, threshold);

		if (cc->ss_state.hystart.min_round_rtt > threshold) {
			cc->ss_state.hystart.found = 1;
			cc->ssthresh = cc->cwnd;
			printf("HYSTART: DELAYEXIT, %ld, %u\n", now, cc->ssthresh);
		}
	}

	if (cc->ss_state.hystart.found == 0) {
		cc->cwnd += bytes;
		if (cc->cwnd_maximum < cc->cwnd)
			cc->cwnd_maximum = cc->cwnd;
	}
}

quicly_ss_type_t quicly_ss_type_hystart = { "hystart", ss_hystart };

