#ifndef ANYTONE_CODEPLUG_H
#define ANYTONE_CODEPLUG_H

#include "image.h"

#include <stddef.h>
#include <stdint.h>

#define AT_MAX_CHANNELS   4000
#define AT_MAX_CONTACTS   10000
#define AT_MAX_ZONES      250
#define AT_MAX_SCANLISTS 250
#define AT_MAX_RADIO_IDS  250
#define AT_MAX_RXGROUPS   250
#define AT_NAME_LEN      17
#define AT_ZONE_CHANS    250
#define AT_SCAN_MEMBERS  50
#define AT_RXG_MEMBERS   64

enum at_ch_mode {
    AT_CH_ANALOG = 0,
    AT_CH_DIGITAL = 1,
    AT_CH_MIXED_ANALOG = 2,
    AT_CH_MIXED_DIGITAL = 3
};

enum at_power {
    AT_PWR_LOW = 0,
    AT_PWR_MID = 1,
    AT_PWR_HIGH = 2,
    AT_PWR_TURBO = 3
};

enum at_contact_type {
    AT_CALL_PRIVATE = 0,
    AT_CALL_GROUP = 1,
    AT_CALL_ALL = 2
};

struct at_channel {
    int index; /* 0-based slot */
    char name[AT_NAME_LEN];
    uint32_t rx_hz;
    uint32_t tx_hz;
    uint8_t mode;
    uint8_t power;
    uint8_t bandwidth_wide; /* 1 = 25kHz, 0 = 12.5kHz */
    uint8_t color_code;
    uint8_t timeslot; /* 1 or 2 */
    int contact_index;   /* -1 none */
    int radio_id_index;  /* -1 none */
    int scan_list_index; /* -1 none */
    int rx_group_index;  /* -1 none */
    uint8_t rx_only;
    uint8_t admit; /* raw admit criterion */
    double rx_ctcss; /* 0 = none */
    double tx_ctcss;
    uint16_t rx_dcs; /* 0 = none; >=512 inverted */
    uint16_t tx_dcs;
};

struct at_contact {
    int index;
    char name[AT_NAME_LEN];
    uint32_t number;
    uint8_t type;
    uint8_t alert; /* 0 none, 1 ring, 2 online */
};

struct at_radio_id {
    int index;
    char name[AT_NAME_LEN];
    uint32_t number;
};

struct at_zone {
    int index;
    char name[AT_NAME_LEN];
    int nchannels;
    int channels[AT_ZONE_CHANS]; /* 0-based channel indices */
};

struct at_scan_list {
    int index;
    char name[AT_NAME_LEN];
    int nmembers;
    int members[AT_SCAN_MEMBERS];
};

struct at_rx_group {
    int index;
    char name[AT_NAME_LEN];
    int nmembers;
    int members[AT_RXG_MEMBERS]; /* contact indices */
};

struct at_settings {
    char intro_line1[AT_NAME_LEN];
    char intro_line2[AT_NAME_LEN];
};

struct at_codeplug {
    char model[8];
    char fw_version[8];
    uint8_t bands;

    int nchannels;
    struct at_channel *channels;

    int ncontacts;
    struct at_contact *contacts;

    int nradio_ids;
    struct at_radio_id *radio_ids;

    int nzones;
    struct at_zone *zones;

    int nscan_lists;
    struct at_scan_list *scan_lists;

    int nrx_groups;
    struct at_rx_group *rx_groups;

    struct at_settings settings;
};

void at_codeplug_init(struct at_codeplug *cp);
void at_codeplug_free(struct at_codeplug *cp);

/* Decode structured objects from a sparse ATCP image. */
int at_codeplug_from_image(struct at_codeplug *cp, const struct at_image *img);

/* Patch structured objects back into an existing ATCP image (must contain
 * the target regions). */
int at_codeplug_to_image(const struct at_codeplug *cp, struct at_image *img);

/* JSON (malloc'd string). Caller frees. */
char *at_codeplug_to_json(const struct at_codeplug *cp);

/* Parse our JSON dialect into a codeplug. */
int at_codeplug_from_json(struct at_codeplug *cp, const char *json);

#endif /* ANYTONE_CODEPLUG_H */
