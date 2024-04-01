#include "quicly/ss.h"
#include <stdio.h>


void ss_rfc2001(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, uint64_t largest_acked, uint32_t inflight,
                        uint64_t next_pn, int64_t now, uint32_t max_udp_payload_size)
{
	cc->cwnd += bytes;
	if (cc->cwnd_maximum < cc->cwnd)
		cc->cwnd_maximum = cc->cwnd;
}

quicly_ss_type_t quicly_ss_type_rfc2001 = { "rfc2001", ss_rfc2001 };

quicly_ss_type_t* quicly_ss_all_types[] = { &quicly_ss_type_rfc2001, &quicly_ss_type_hybla, &quicly_ss_type_hystart, &quicly_ss_type_search10, &quicly_ss_type_search10_interp, &quicly_ss_type_search10_delv, NULL };
