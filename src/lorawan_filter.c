#include "lorawan_filter.h"

static lorawan_filter_t g_filter;

lorawan_filter_t *lorawan_filter(void)
{
    return &g_filter;
}

bool hex_characters(const char ch)
{
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')) {
        return true;
    }
    return false;
}

static int dev_addr_str2hex(short *pbDest, const char *pbSrc, int nLen)
{
    if (nLen != MAX_LORA_MAC) {
        printf("ERROR: n len is less than 8 bytes.\n");
        return -1;
    }
    char h1, h2;
    int  i;
    char hex[3] = { 0 };
    for (i = 0; i < (nLen / 2); i++) {
        memset(hex, 0, 3);
        h1 = pbSrc[2 * i];
        h2 = pbSrc[2 * i + 1];
        if (hex_characters(h1) && hex_characters(h2)) {
            snprintf(hex, 3, "%c%c", h1, h2);
            pbDest[i] = (int)strtol(hex, NULL, 16);
        } else {
            printf("ERROR: format.");
            return -1;
        }
    }
    return 0;
}

static int dev_addr_hex2int(short *dev_array, int *addr_val)
{
    if (dev_array == NULL || addr_val == NULL) {
        MSG("ERROR, null poiter.\n");
        return -1;
    }
    *addr_val = dev_array[3];
    *addr_val |= (dev_array[2]) << 8;
    *addr_val |= (dev_array[1]) << 16;
    *addr_val |= (dev_array[0]) << 24;
    return 0;
}

int match(struct cds_lfht_node *ht_node, const void *_key)
{
    dev_addr_htn_t *match_node = caa_container_of(ht_node, dev_addr_htn_t, node);
    const int      *key        = _key;
    return (*key == match_node->value);
}

int parse_filter_configuration(void)
{
    JSON_Value     *root_val   = NULL;
    JSON_Object    *conf_obj   = NULL;
    JSON_Array     *conf_array = NULL;
    dev_addr_htn_t *dev_node   = NULL;
    JSON_Value     *val        = NULL; /* needed to detect the absence of some fields */
    const char     *str;
    unsigned long   hash           = 0;
    short           value_array[4] = { 0 };
    uint32_t        addr_value     = 0;
    uint32_t        seqnum         = 0;
    /* try to parse JSON */
    root_val = json_parse_file_with_comments(FILTER_CONF_PATH_DEFAULT);
    if (root_val == NULL) {
        printf("ERROR: %s is not a valid JSON file\n", FILTER_CONF_PATH_DEFAULT);
        return -1;
    }

    /* point to the gateway configuration object */
    conf_obj = json_value_get_object(root_val);
    if (conf_obj == NULL) {
        json_value_free(root_val);
        return -1;
    } else {
        printf("INFO: %s does contain a JSON object , parsing debug parameters\n",
               FILTER_CONF_PATH_DEFAULT);
    }

    val = json_object_get_value(conf_obj, "filter_enable");
    if (json_value_get_type(val) == JSONBoolean) {
        lorawan_filter()->filter_enable = (bool)json_value_get_boolean(val);
        printf("INFO: lorawan filter enable :%d \n", lorawan_filter()->filter_enable);
    }
    if (lorawan_filter()->filter_enable == false) {
        printf("INFO: LoRaWAN filter is not enable.\n");
        return -1;
    }

    val = json_object_get_value(conf_obj, "RSSI");
    if (val != NULL) {
        lorawan_filter()->filter_rssi = json_value_get_number(val);
        printf("INFO: filter rssi is %lf \n", lorawan_filter()->filter_rssi);
    }

    val = json_object_get_value(conf_obj, "SNR");
    if (val != NULL) {
        lorawan_filter()->filter_snr = json_value_get_number(val);
        printf("INFO: filter rssi is %lf \n", lorawan_filter()->filter_snr);
    }

    conf_array = json_object_get_array(conf_obj, "white_list");
    if (conf_array == NULL) {
        printf("INFO: dev White list is empty.\n");
        return -1;
    }

    /* Use time as seed for hash table hashing. */
    lorawan_filter()->seed = time(NULL);
    /*
	 * Allocate hash table.
	 */
    lorawan_filter()->dev_ht = cds_lfht_new_flavor(
        1, 1, 0, CDS_LFHT_AUTO_RESIZE | CDS_LFHT_ACCOUNTING, &urcu_memb_flavor, NULL);
    if (!lorawan_filter()->dev_ht) {
        printf("ERROR:  allocating dev_ht\n");
        return -1;
    }

    for (int i = 0; i < (int)json_array_get_count(conf_array); ++i) {
        str = json_array_get_string(conf_array, i);
        // MSG("DEBUG: while list dev: %s \n", str);
        memset(value_array, 0, sizeof(value_array));
        dev_node = (dev_addr_htn_t *)malloc(sizeof(dev_addr_htn_t));
        if (!dev_node) {
            json_value_free(root_val);
            MSG("ERROR: dev_node is null.\n");
            return -1;
        }
        cds_lfht_node_init(&dev_node->node);
        if (dev_addr_str2hex(value_array, str, strlen(str)) == -1) {
            json_value_free(root_val);
            MSG("ERROR: dev_addr_str2hex is failed.\n");
            return -1;
        }
        if (dev_addr_hex2int(value_array, &addr_value) == -1) {
            MSG("ERROR: dev_addr_hex2int is failed.\n");
            json_value_free(root_val);
            return -1;
        }
        dev_node->value  = addr_value;
        dev_node->seqnum = seqnum++;
        hash             = jhash(&addr_value, sizeof(int), lorawan_filter()->seed);
        urcu_memb_read_lock();
        cds_lfht_add(lorawan_filter()->dev_ht, hash, &dev_node->node);
        urcu_memb_read_unlock();
    }
    MSG("INFO: [%d] devices to filter.\n", dev_node->seqnum);
    /* free JSON parsing data structure */
    json_value_free(root_val);
    return 0;
}

/*主动释放ht的节点内存*/
void delete_dev_ht_node(void)
{
    int                   ret;
    struct cds_lfht_node *ht_node;
    dev_addr_htn_t       *dev_node;
    printf("removing keys (single key, not duplicates):\n");
    urcu_memb_read_lock();

    cds_lfht_for_each_entry(lorawan_filter()->dev_ht, &(lorawan_filter()->iter), dev_node, node)
    {
        ht_node = cds_lfht_iter_get_node(&(lorawan_filter()->iter));
        ret     = cds_lfht_del(lorawan_filter()->dev_ht, ht_node);
        if (ret) {
            // printf(" (concurrently deleted)");
        } else {
            free(dev_node);
        }
    }
    urcu_memb_read_unlock();
}