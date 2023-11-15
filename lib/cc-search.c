/*
 * TODO: Copyright
 */
#include "quicly/cc.h"
#include "quicly.h"

static float search_calc_threshold(quicly_cc_t *cc)
{
    uint32_t sum_sent = 0U;
    uint32_t sum_delv = 0U;

    uint32_t index = cc->state.search.bin_index;

    for( uint32_t i=0; i<=index; i++ )
    {
        if( i != index )
            sum_delv += cc->state.search.delv[i];

        sum_sent += cc->state.search.sent[i];
    }
    printf("cc-search.c@%d (%d , %d)\n", __LINE__, sum_sent, sum_delv );

    if( sum_sent <= 0U )
        return 0U;

    return ( (float)sum_sent - (float)sum_delv ) / ((float)sum_sent);
}

static void search_on_acked(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, uint64_t largest_acked, uint32_t inflight,
                          uint64_t next_pn, int64_t now, uint32_t max_udp_payload_size)
{
    assert(inflight >= bytes);
    if( cc->state.search.bin_end == 0 )
        cc->state.search.bin_end = now; // after reset

    // update time bin
    uint32_t index = cc->state.search.bin_index;
    if( now > cc->state.search.bin_end )
    {
        cc->state.search.bin_end += CCSEARCH_BINTIME;
        index = (cc->state.search.bin_index+1) % CCSEARCH_NUMBINS;
        cc->state.search.bin_index = index;
        cc->state.search.sent[index] = 0U;
        cc->state.search.delv[index] = 0U;
        printf("cc-search.c@%d\n", __LINE__ );
    }

    cc->state.search.delv[index] += bytes;

    float threshold = search_calc_threshold(cc); // calc current threshold
    printf("cc-search.c@%d (%f / %f)\n", __LINE__, threshold, CCSEARCH_THRESH );
    if( threshold > CCSEARCH_THRESH )
    {
        printf("cc-search.c@%d\n", __LINE__ );
        cc->cwnd = cc->ssthresh; // exit slow start
        printf("cc-search.c@%d\n", __LINE__ );
    }
}

void quicly_cc_search_on_lost(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, uint64_t lost_pn, uint64_t next_pn,
                            int64_t now, uint32_t max_udp_payload_size)
{
    /* Unused */
    return;
}

void quicly_cc_search_on_persistent_congestion(quicly_cc_t *cc, const quicly_loss_t *loss, int64_t now)
{
    /* Unused */
    return;
}

void quicly_cc_search_on_sent(quicly_cc_t *cc, const quicly_loss_t *loss, uint32_t bytes, int64_t now)
{
    uint32_t index = cc->state.search.bin_index;
    cc->state.search.sent[index] += bytes;
    printf("cc-search.c@%d\n", __LINE__ );
    return;
}

static int search_on_switch(quicly_cc_t *cc)
{
    printf("cc-search.c@%d\n", __LINE__ );
    if (cc->type != &quicly_cc_type_search) {
        printf("cc-search.c@%d\n", __LINE__ );
        cc->type = &quicly_cc_type_search;
        memset(cc->state.search.sent, 0U, sizeof(uint32_t) * CCSEARCH_NUMBINS );
        memset(cc->state.search.delv, 0U, sizeof(uint32_t) * CCSEARCH_NUMBINS );
        cc->state.search.bin_index = 0U;
        cc->state.search.bin_end = 0;
        printf("cc-search.c@%d\n", __LINE__ );
    }
    return 1;
}

static void search_init(quicly_init_cc_t *self, quicly_cc_t *cc, uint32_t initcwnd, int64_t now)
{
    printf("cc-search.c@%d\n", __LINE__ );
    memset(cc, 0, sizeof(quicly_cc_t));
    cc->type = &quicly_cc_type_search;
    cc->cwnd = cc->cwnd_initial = cc->cwnd_maximum = initcwnd;
    cc->ssthresh = cc->cwnd_minimum = UINT32_MAX;

    memset(cc->state.search.sent, 0U, sizeof(uint32_t) * CCSEARCH_NUMBINS );
    memset(cc->state.search.delv, 0U, sizeof(uint32_t) * CCSEARCH_NUMBINS );
    cc->state.search.bin_index = 0;
    cc->state.search.bin_end = now + CCSEARCH_BINTIME;
}

quicly_cc_type_t quicly_cc_type_search = {"search",
                                        &quicly_cc_search_init,
                                        search_on_acked,
                                        quicly_cc_search_on_lost,
                                        quicly_cc_search_on_persistent_congestion,
                                        quicly_cc_search_on_sent,
                                        search_on_switch};
quicly_init_cc_t quicly_cc_search_init = {search_init};

