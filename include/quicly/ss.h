#ifndef quicly_ss_h
#define quicly_ss_h

#ifdef __cplusplus
extern "C" {
#endif

#include "quicly/cc.h"
#include "quicly/constants.h"
#include <stdint.h>
#include <stdbool.h>

extern bool search_exit;

#define MIN(x,y) ((x > y) ? (y) : (x))
#define MAX(x,y) ((x < y) ? (y) : (x))

/**
 * Holds pointers to concrete congestion control implementation functions.
 */
typedef const struct st_quicly_ss_type_t quicly_ss_type_t;

struct st_quicly_ss_type_t {
    const char* name;
    void (*ss)(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, uint64_t largest_acked, uint32_t inflight,
               uint64_t next_pn, int64_t now, uint32_t max_udp_payload_size);
};

extern quicly_ss_type_t quicly_ss_type_rfc2001, quicly_ss_type_hybla, quicly_ss_type_hystart, quicly_ss_type_search20_delv;

extern quicly_ss_type_t* quicly_ss_all_types[];

void ss_rfc2001(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, uint64_t largest_acked, uint32_t inflight,
                        uint64_t next_pn, int64_t now, uint32_t max_udp_payload_size);

struct st_quicly_variable_ss {
    quicly_ss_type_t* slowstart;
};

extern struct st_quicly_variable_ss active_slowstart;

#ifdef __cplusplus
}
#endif

#endif