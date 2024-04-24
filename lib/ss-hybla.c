#include "quicly/ss.h"
#include <stdint.h>
#include <math.h>

static void recalc_rho(quicly_cc_t *cc, const quicly_loss_t *loss) {
	// RTT in msec
	// Linux here uses us instead of ms for their rho calculations, which gives them more accurate information about
	// the bandwidth of the connection. Here, we only have ms level measurements from loss, and so all of our
	// measurements are only up to that level of precision. It cleans the code up a little bit though from the kernel. 
	// we have changed this to use doubles for more accuracy and ease of writing code. 
	double rho = (double) loss->rtt.minimum / QUICLY_HYBLA_RTT0;
	// don't allow the ratio to be less than one for faster connections than reference. 
	cc->ss_state.hybla.rho = MAX(rho, 1);
}

void ss_hybla(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, uint64_t largest_acked, uint32_t inflight,
                        uint64_t next_pn, int64_t now, uint32_t max_udp_payload_size)
{
	recalc_rho(cc, loss);

	cc->cwnd += (uint32_t)round(pow(2, MIN(cc->ss_state.hybla.rho, QUICLY_HYBLA_RHO_LIM)));

	if (cc->cwnd_maximum < cc->cwnd)
		cc->cwnd_maximum = cc->cwnd;
}

quicly_ss_type_t quicly_ss_type_hybla = { "hybla", ss_hybla };