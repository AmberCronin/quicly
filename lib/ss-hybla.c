#include "quicly/ss.h"
#include <stdio.h>
#include <stdint.h>

#define QUICLY_HYBLA_RTT0 (25)
#define QUICLY_HYBLA_RHO_LIM (16) // rho limited to prevent integer overflow from affecting performance

// Based very heavily on the Linux kernel's implementation

static void recalc_rho(quicly_cc_t *cc, const quicly_loss_t *loss) {
	// RTT in msec
	// Linux here uses us instead of ms for their rho calculations, which gives them more accurate information about
	// the bandwidth of the connection. Here, we only have ms level measurements from loss, and so all of our
	// measurements are only up to that level of precision. It cleans the code up a little bit though from the kernel. 
	uint32_t rho_3ls = (loss->rtt.minimum << 3) / QUICLY_HYBLA_RTT0;
	// don't allow the ratio to be less than one for faster connections than reference
	cc->ss_state.hybla.rho_3ls = MAX(rho_3ls, (1 << 3));
	cc->ss_state.hybla.rho = (cc->ss_state.hybla.rho_3ls >> 3);
}

static uint32_t fraction(uint32_t frac) {
	const uint32_t fractions[] = {
		128, 139, 152, 165, 181, 197, 215, 234,
	};
	const uint32_t nfractions = 8;
	// the second case should never run, but just in case
	return (frac < nfractions ? fractions[frac] : fractions[0]);	
}

void ss_hybla(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, uint64_t largest_acked, uint32_t inflight,
                        uint64_t next_pn, int64_t now, uint32_t max_udp_payload_size)
{
	recalc_rho(cc, loss);

	uint32_t rho_fractions = cc->ss_state.hybla.rho_3ls - (cc->ss_state.hybla.rho << 3);
	uint32_t increment = (((1 << MIN(cc->ss_state.hybla.rho, QUICLY_HYBLA_RHO_LIM)) * fraction(rho_fractions)) - (1 << 7));

	cc->cwnd += (increment >> 7);
	cc->ss_state.hybla.cents += (increment % (1 << 7));

	while (cc->ss_state.hybla.cents >= (1 << 7)) {
		cc->cwnd += 1;
		cc->ss_state.hybla.cents -= (1 << 7);
	}

	if (cc->cwnd_maximum < cc->cwnd)
		cc->cwnd_maximum = cc->cwnd;
}

quicly_ss_type_t quicly_ss_type_hybla = { "hybla", ss_hybla };