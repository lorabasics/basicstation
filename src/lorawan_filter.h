#ifndef _LORAWAN_FILTER_H_
#define _LORAWAN_FILTER_H_
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <urcu/urcu-memb.h> /* RCU flavor */
#include <urcu/rculfhash.h> /* RCU Lock-free hash table */
#include <urcu/compiler.h>  /* For CAA_ARRAY_SIZE */
#include "jhash.h"          /* Example hash function */
#include "parson.h"
#define MAX_LORA_MAC 8
#define FILTER_CONF_PATH_DEFAULT "/etc/lorawan_filter/lorawan_filter.conf"
#define MSG(args...) printf(args) /* message that is destined to the user */

typedef struct lorawan_filter {
    uint32_t mote_addr;
    uint32_t mote_fcnt;
    /* For iteration on hash table */
    bool filter_enable;
    /*dev_ht for filter whilte list */
    struct cds_lfht     *dev_ht;
    int                  seqnum;
    time_t               seed;
    double               filter_rssi;
    double               filter_snr;
    struct cds_lfht_iter iter;
} lorawan_filter_t;

typedef struct dev_addr_htn {
    uint32_t             value;
    uint32_t             seqnum; /* Our node sequence number */
    struct cds_lfht_node node;
} dev_addr_htn_t;

lorawan_filter_t *lorawan_filter(void);
int match(struct cds_lfht_node *ht_node, const void *_key);
int parse_filter_configuration(void);
void delete_dev_ht_node(void);

#endif