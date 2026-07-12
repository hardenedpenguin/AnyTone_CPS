#include "codeplug.h"
#include "util.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Memory map (D868UV / D878UV family) — from qdmr. */
#define OFF_CH_BANKS      0x00800000u
#define OFF_CH_BANK_STRIDE 0x00040000u
#define CH_PER_BANK       128u
#define CH_SIZE           0x40u

#define OFF_CONTACT_BMP   0x02640000u
#define OFF_CONTACT_BANKS 0x02680000u
#define OFF_CONTACT_BSTR  0x00040000u
#define CONTACTS_PER_BANK 1000u
#define CONTACT_SIZE      0x64u
#define CONTACT_BMP_SIZE  0x04F0u /* matches D878UV2 memmap Contact Bitmap Element */

#define OFF_CH_BMP        0x024c1500u
#define CH_BMP_SIZE       0x0200u

#define OFF_RADIO_ID_BMP  0x024c1320u
#define OFF_RADIO_IDS     0x02580000u
#define RADIO_ID_SIZE     0x20u
#define RADIO_ID_BMP_SIZE 0x20u

#define OFF_ZONE_BMP      0x024c1300u
#define OFF_ZONE_NAMES    0x02540000u
#define OFF_ZONE_CHANS    0x01000000u
#define ZONE_NAME_STRIDE  0x20u
#define ZONE_CHAN_STRIDE  0x200u
#define ZONE_BMP_SIZE     0x20u

#define OFF_SCAN_BMP      0x024c1340u
#define OFF_SCAN_BANKS    0x01080000u
#define SCAN_STRIDE       0x200u
#define SCAN_BANK_STRIDE  0x40000u
#define SCAN_PER_BANK     16u
#define SCAN_SIZE         0x90u
#define SCAN_BMP_SIZE     0x20u

#define OFF_RXG_BMP       0x025C0B10u
#define OFF_RX_GROUPS     0x02980000u
#define RXG_STRIDE        0x200u
#define RXG_SIZE          0x120u /* 64×u32 members + name + pad; bank stride 0x200 */
#define RXG_BMP_SIZE      0x20u
#define RXG_NAME_OFF      0x100u

#define OFF_BOOT          0x02500600u
#define BOOT_SIZE         0x30u

static const double CTCSS_TABLE[] = {
    62.5, 67.0, 69.3, 71.9, 74.4, 77.0, 79.7, 82.5, 85.4, 88.5, 91.5, 94.8,
    97.4, 100.0, 103.5, 107.2, 110.9, 114.8, 118.8, 123.0, 127.3, 131.8, 136.5,
    141.3, 146.2, 151.4, 156.7, 159.8, 162.2, 165.5, 167.9, 171.3, 173.8, 177.3,
    179.9, 183.5, 186.2, 189.9, 192.8, 196.6, 199.5, 203.5, 206.5, 210.7, 218.1,
    225.7, 229.1, 233.6, 241.8, 250.3, 254.1
};
#define CTCSS_N (sizeof(CTCSS_TABLE) / sizeof(CTCSS_TABLE[0]))
#define CUSTOM_CTCSS 0x33

void at_codeplug_init(struct at_codeplug *cp)
{
    memset(cp, 0, sizeof(*cp));
}

void at_codeplug_free(struct at_codeplug *cp)
{
    free(cp->channels);
    free(cp->contacts);
    free(cp->radio_ids);
    free(cp->zones);
    free(cp->scan_lists);
    free(cp->rx_groups);
    memset(cp, 0, sizeof(*cp));
}

static uint32_t channel_addr(unsigned idx)
{
    return OFF_CH_BANKS + (idx / CH_PER_BANK) * OFF_CH_BANK_STRIDE +
           (idx % CH_PER_BANK) * CH_SIZE;
}

static uint32_t contact_addr(unsigned idx)
{
    return OFF_CONTACT_BANKS + (idx / CONTACTS_PER_BANK) * OFF_CONTACT_BSTR +
           (idx % CONTACTS_PER_BANK) * CONTACT_SIZE;
}

static uint32_t scan_addr(unsigned idx)
{
    return OFF_SCAN_BANKS + (idx / SCAN_PER_BANK) * SCAN_BANK_STRIDE +
           (idx % SCAN_PER_BANK) * SCAN_STRIDE;
}

static double ctcss_decode(uint8_t idx)
{
    if (idx == 0 || idx == CUSTOM_CTCSS || idx >= CTCSS_N)
        return 0.0;
    return CTCSS_TABLE[idx];
}

static uint8_t ctcss_encode(double hz)
{
    if (hz <= 0.0)
        return 0;
    for (unsigned i = 0; i < CTCSS_N; i++) {
        if (CTCSS_TABLE[i] > hz - 0.05 && CTCSS_TABLE[i] < hz + 0.05)
            return (uint8_t)i;
    }
    return 0;
}

static void decode_channel(struct at_channel *ch, const uint8_t *raw, int index)
{
    memset(ch, 0, sizeof(*ch));
    ch->index = index;
    ch->rx_hz = at_bcd8_be_get(raw + 0x00) * 10u;
    uint32_t off = at_bcd8_be_get(raw + 0x04) * 10u;
    uint8_t b8 = raw[0x08];
    ch->mode = at_get_bits(b8, 0, 2);
    ch->power = at_get_bits(b8, 2, 2);
    ch->bandwidth_wide = at_get_bits(b8, 4, 2) ? 1 : 0;
    uint8_t rmode = at_get_bits(b8, 6, 2);
    if (rmode == 0)
        ch->tx_hz = ch->rx_hz;
    else if (rmode == 1)
        ch->tx_hz = ch->rx_hz + off;
    else
        ch->tx_hz = ch->rx_hz - off;

    uint8_t b9 = raw[0x09];
    uint8_t rxsig = at_get_bits(b9, 0, 2);
    uint8_t txsig = at_get_bits(b9, 2, 2);
    ch->rx_only = at_get_bits(b9, 5, 1);

    if (rxsig == 1)
        ch->rx_ctcss = ctcss_decode(raw[0x0b]);
    else if (rxsig == 2)
        ch->rx_dcs = at_get_u16_le(raw + 0x0e);
    if (txsig == 1)
        ch->tx_ctcss = ctcss_decode(raw[0x0a]);
    else if (txsig == 2)
        ch->tx_dcs = at_get_u16_le(raw + 0x0c);

    ch->contact_index = (int)at_get_u32_le(raw + 0x14);
    if (ch->contact_index == (int)0xffffffffu)
        ch->contact_index = -1;
    ch->radio_id_index = (int)raw[0x18];
    ch->admit = at_get_bits(raw[0x1a], 0, 4);
    ch->scan_list_index = raw[0x1b] == 0xff ? -1 : (int)raw[0x1b];
    ch->rx_group_index = raw[0x1c] == 0xff ? -1 : (int)raw[0x1c];
    ch->color_code = raw[0x20];
    ch->timeslot = at_get_bits(raw[0x21], 0, 1) ? 2 : 1;
    at_read_ascii(raw + 0x23, 16, ch->name, sizeof(ch->name));
}

static void encode_channel(uint8_t *raw, const struct at_channel *ch)
{
    memset(raw, 0, CH_SIZE);
    at_bcd8_be_set(raw + 0x00, ch->rx_hz / 10u);
    uint32_t off = 0;
    uint8_t rmode = 0;
    if (ch->tx_hz > ch->rx_hz) {
        rmode = 1;
        off = ch->tx_hz - ch->rx_hz;
    } else if (ch->tx_hz < ch->rx_hz) {
        rmode = 2;
        off = ch->rx_hz - ch->tx_hz;
    }
    at_bcd8_be_set(raw + 0x04, off / 10u);
    at_set_bits(&raw[0x08], 0, 2, ch->mode);
    at_set_bits(&raw[0x08], 2, 2, ch->power);
    at_set_bits(&raw[0x08], 4, 2, ch->bandwidth_wide ? 1 : 0);
    at_set_bits(&raw[0x08], 6, 2, rmode);

    uint8_t rxsig = 0, txsig = 0;
    if (ch->rx_ctcss > 0.0) {
        rxsig = 1;
        raw[0x0b] = ctcss_encode(ch->rx_ctcss);
    } else if (ch->rx_dcs) {
        rxsig = 2;
        at_put_u16_le(raw + 0x0e, ch->rx_dcs);
    }
    if (ch->tx_ctcss > 0.0) {
        txsig = 1;
        raw[0x0a] = ctcss_encode(ch->tx_ctcss);
    } else if (ch->tx_dcs) {
        txsig = 2;
        at_put_u16_le(raw + 0x0c, ch->tx_dcs);
    }
    at_set_bits(&raw[0x09], 0, 2, rxsig);
    at_set_bits(&raw[0x09], 2, 2, txsig);
    at_set_bits(&raw[0x09], 5, 1, ch->rx_only);

    if (ch->contact_index >= 0)
        at_put_u32_le(raw + 0x14, (uint32_t)ch->contact_index);
    else
        at_put_u32_le(raw + 0x14, 0xffffffffu);
    raw[0x18] = ch->radio_id_index >= 0 ? (uint8_t)ch->radio_id_index : 0;
    at_set_bits(&raw[0x1a], 0, 4, ch->admit);
    raw[0x1b] = ch->scan_list_index >= 0 ? (uint8_t)ch->scan_list_index : 0xff;
    raw[0x1c] = ch->rx_group_index >= 0 ? (uint8_t)ch->rx_group_index : 0xff;
    raw[0x20] = ch->color_code;
    at_set_bits(&raw[0x21], 0, 1, ch->timeslot == 2 ? 1 : 0);
    at_write_ascii(raw + 0x23, 16, ch->name);
}

int at_codeplug_from_image(struct at_codeplug *cp, const struct at_image *img)
{
    at_codeplug_free(cp);
    at_codeplug_init(cp);
    memcpy(cp->model, img->model, sizeof(cp->model));
    memcpy(cp->fw_version, img->fw_version, sizeof(cp->fw_version));
    cp->bands = img->bands;

    /* Channels */
    const uint8_t *ch_bmp = at_image_find(img, OFF_CH_BMP, CH_BMP_SIZE);
    if (ch_bmp) {
        for (unsigned i = 0; i < AT_MAX_CHANNELS; i++) {
            if (!at_bitmap_get(ch_bmp, i))
                continue;
            const uint8_t *raw = at_image_find(img, channel_addr(i), CH_SIZE);
            if (!raw)
                continue;
            struct at_channel *nch =
                realloc(cp->channels, (cp->nchannels + 1) * sizeof(*nch));
            if (!nch)
                return -1;
            cp->channels = nch;
            decode_channel(&cp->channels[cp->nchannels], raw, (int)i);
            cp->nchannels++;
        }
    }

    /* Contacts (inverted bitmap) */
    const uint8_t *ct_bmp = at_image_find(img, OFF_CONTACT_BMP, CONTACT_BMP_SIZE);
    if (ct_bmp) {
        for (unsigned i = 0; i < AT_MAX_CONTACTS; i++) {
            if (!at_inv_bitmap_get(ct_bmp, i))
                continue;
            const uint8_t *raw = at_image_find(img, contact_addr(i), CONTACT_SIZE);
            if (!raw)
                continue;
            struct at_contact *nc =
                realloc(cp->contacts, (cp->ncontacts + 1) * sizeof(*nc));
            if (!nc)
                return -1;
            cp->contacts = nc;
            struct at_contact *c = &cp->contacts[cp->ncontacts];
            memset(c, 0, sizeof(*c));
            c->index = (int)i;
            c->type = raw[0];
            at_read_ascii(raw + 1, 16, c->name, sizeof(c->name));
            c->number = at_bcd8_be_get(raw + 0x23);
            c->alert = raw[0x27];
            if (c->name[0] == '\0' && c->number == 0)
                continue;
            cp->ncontacts++;
        }
    }

    /* Radio IDs */
    const uint8_t *rid_bmp = at_image_find(img, OFF_RADIO_ID_BMP, RADIO_ID_BMP_SIZE);
    if (rid_bmp) {
        for (unsigned i = 0; i < AT_MAX_RADIO_IDS; i++) {
            if (!at_bitmap_get(rid_bmp, i))
                continue;
            const uint8_t *raw =
                at_image_find(img, OFF_RADIO_IDS + i * RADIO_ID_SIZE, RADIO_ID_SIZE);
            if (!raw)
                continue;
            struct at_radio_id *nr =
                realloc(cp->radio_ids, (cp->nradio_ids + 1) * sizeof(*nr));
            if (!nr)
                return -1;
            cp->radio_ids = nr;
            struct at_radio_id *r = &cp->radio_ids[cp->nradio_ids];
            memset(r, 0, sizeof(*r));
            r->index = (int)i;
            r->number = at_bcd8_be_get(raw + 0);
            at_read_ascii(raw + 5, 16, r->name, sizeof(r->name));
            cp->nradio_ids++;
        }
    }

    /* Zones — D878UV2 often marks every zone bit used even when slots are
     * empty/0xff, so only keep zones with a real name and/or channel members. */
    for (unsigned i = 0; i < AT_MAX_ZONES; i++) {
        const uint8_t *name =
            at_image_find(img, OFF_ZONE_NAMES + i * ZONE_NAME_STRIDE, 16);
        const uint8_t *chans =
            at_image_find(img, OFF_ZONE_CHANS + i * ZONE_CHAN_STRIDE, ZONE_CHAN_STRIDE);
        if (!chans)
            continue;

        char zname[AT_NAME_LEN];
        zname[0] = '\0';
        if (name)
            at_read_ascii(name, 16, zname, sizeof(zname));

        int members[AT_ZONE_CHANS];
        int nmembers = 0;
        for (unsigned j = 0; j < AT_ZONE_CHANS; j++) {
            uint16_t ci = at_get_u16_le(chans + j * 2);
            if (ci == 0xffff)
                break;
            members[nmembers++] = (int)ci;
        }

        if (nmembers == 0 && zname[0] == '\0')
            continue;

        struct at_zone *nz = realloc(cp->zones, (cp->nzones + 1) * sizeof(*nz));
        if (!nz)
            return -1;
        cp->zones = nz;
        struct at_zone *z = &cp->zones[cp->nzones];
        memset(z, 0, sizeof(*z));
        z->index = (int)i;
        memcpy(z->name, zname, sizeof(z->name));
        z->nchannels = nmembers;
        memcpy(z->channels, members, (size_t)nmembers * sizeof(int));
        cp->nzones++;
    }

    /* Scan lists */
    const uint8_t *s_bmp = at_image_find(img, OFF_SCAN_BMP, SCAN_BMP_SIZE);
    if (s_bmp) {
        for (unsigned i = 0; i < AT_MAX_SCANLISTS; i++) {
            if (!at_bitmap_get(s_bmp, i))
                continue;
            const uint8_t *raw = at_image_find(img, scan_addr(i), SCAN_SIZE);
            if (!raw)
                continue;
            struct at_scan_list *ns =
                realloc(cp->scan_lists, (cp->nscan_lists + 1) * sizeof(*ns));
            if (!ns)
                return -1;
            cp->scan_lists = ns;
            struct at_scan_list *s = &cp->scan_lists[cp->nscan_lists];
            memset(s, 0, sizeof(*s));
            s->index = (int)i;
            at_read_ascii(raw + 0x0f, 16, s->name, sizeof(s->name));
            for (unsigned j = 0; j < AT_SCAN_MEMBERS; j++) {
                uint16_t m = at_get_u16_le(raw + 0x20 + j * 2);
                if (m == 0xffff)
                    break;
                s->members[s->nmembers++] = (int)m;
            }
            cp->nscan_lists++;
        }
    }

    /* RX group lists */
    const uint8_t *g_bmp = at_image_find(img, OFF_RXG_BMP, RXG_BMP_SIZE);
    if (g_bmp) {
        for (unsigned i = 0; i < AT_MAX_RXGROUPS; i++) {
            if (!at_bitmap_get(g_bmp, i))
                continue;
            const uint8_t *raw =
                at_image_find(img, OFF_RX_GROUPS + i * RXG_STRIDE, RXG_SIZE);
            if (!raw)
                continue;
            struct at_rx_group *ng =
                realloc(cp->rx_groups, (cp->nrx_groups + 1) * sizeof(*ng));
            if (!ng)
                return -1;
            cp->rx_groups = ng;
            struct at_rx_group *g = &cp->rx_groups[cp->nrx_groups];
            memset(g, 0, sizeof(*g));
            g->index = (int)i;
            at_read_ascii(raw + RXG_NAME_OFF, 16, g->name, sizeof(g->name));
            for (unsigned j = 0; j < AT_RXG_MEMBERS; j++) {
                uint32_t m = at_get_u32_le(raw + j * 4);
                if (m == 0xffffffffu)
                    break;
                g->members[g->nmembers++] = (int)m;
            }
            if (g->name[0] == '\0' && g->nmembers == 0)
                continue;
            cp->nrx_groups++;
        }
    }

    /* Boot intro lines */
    const uint8_t *boot = at_image_find(img, OFF_BOOT, BOOT_SIZE);
    if (boot) {
        at_read_ascii(boot + 0x00, 16, cp->settings.intro_line1, sizeof(cp->settings.intro_line1));
        at_read_ascii(boot + 0x10, 16, cp->settings.intro_line2, sizeof(cp->settings.intro_line2));
    }

    return 0;
}

int at_codeplug_to_image(const struct at_codeplug *cp, struct at_image *img)
{
    uint8_t *ch_bmp = at_image_find_mut(img, OFF_CH_BMP, CH_BMP_SIZE);
    if (ch_bmp) {
        memset(ch_bmp, 0, CH_BMP_SIZE);
        for (int i = 0; i < cp->nchannels; i++) {
            const struct at_channel *ch = &cp->channels[i];
            if (ch->index < 0 || ch->index >= AT_MAX_CHANNELS)
                continue;
            at_bitmap_set(ch_bmp, (unsigned)ch->index, 1);
            uint8_t *raw = at_image_find_mut(img, channel_addr((unsigned)ch->index), CH_SIZE);
            if (raw)
                encode_channel(raw, ch);
        }
    }

    uint8_t *ct_bmp = at_image_find_mut(img, OFF_CONTACT_BMP, CONTACT_BMP_SIZE);
    if (ct_bmp) {
        memset(ct_bmp, 0xff, CONTACT_BMP_SIZE); /* inverted: all disabled */
        for (int i = 0; i < cp->ncontacts; i++) {
            const struct at_contact *c = &cp->contacts[i];
            if (c->index < 0 || c->index >= AT_MAX_CONTACTS)
                continue;
            at_inv_bitmap_set(ct_bmp, (unsigned)c->index, 1);
            uint8_t *raw = at_image_find_mut(img, contact_addr((unsigned)c->index), CONTACT_SIZE);
            if (!raw)
                continue;
            memset(raw, 0, CONTACT_SIZE);
            raw[0] = c->type;
            at_write_ascii(raw + 1, 16, c->name);
            at_bcd8_be_set(raw + 0x23, c->number);
            raw[0x27] = c->alert;
        }
    }

    uint8_t *rid_bmp = at_image_find_mut(img, OFF_RADIO_ID_BMP, RADIO_ID_BMP_SIZE);
    if (rid_bmp) {
        memset(rid_bmp, 0, RADIO_ID_BMP_SIZE);
        for (int i = 0; i < cp->nradio_ids; i++) {
            const struct at_radio_id *r = &cp->radio_ids[i];
            if (r->index < 0 || r->index >= AT_MAX_RADIO_IDS)
                continue;
            at_bitmap_set(rid_bmp, (unsigned)r->index, 1);
            uint8_t *raw =
                at_image_find_mut(img, OFF_RADIO_IDS + (unsigned)r->index * RADIO_ID_SIZE,
                                  RADIO_ID_SIZE);
            if (!raw)
                continue;
            memset(raw, 0, RADIO_ID_SIZE);
            at_bcd8_be_set(raw + 0, r->number);
            at_write_ascii(raw + 5, 16, r->name);
        }
    }

    uint8_t *z_bmp = at_image_find_mut(img, OFF_ZONE_BMP, ZONE_BMP_SIZE);
    if (z_bmp) {
        memset(z_bmp, 0, ZONE_BMP_SIZE);
        for (int i = 0; i < cp->nzones; i++) {
            const struct at_zone *z = &cp->zones[i];
            if (z->index < 0 || z->index >= AT_MAX_ZONES)
                continue;
            at_bitmap_set(z_bmp, (unsigned)z->index, 1);
            uint8_t *name =
                at_image_find_mut(img, OFF_ZONE_NAMES + (unsigned)z->index * ZONE_NAME_STRIDE, 16);
            uint8_t *chans = at_image_find_mut(
                img, OFF_ZONE_CHANS + (unsigned)z->index * ZONE_CHAN_STRIDE, ZONE_CHAN_STRIDE);
            if (!name || !chans)
                continue;
            at_write_ascii(name, 16, z->name);
            memset(chans, 0xff, ZONE_CHAN_STRIDE);
            for (int j = 0; j < z->nchannels && j < AT_ZONE_CHANS; j++)
                at_put_u16_le(chans + j * 2, (uint16_t)z->channels[j]);
        }
    }

    uint8_t *s_bmp = at_image_find_mut(img, OFF_SCAN_BMP, SCAN_BMP_SIZE);
    if (s_bmp) {
        memset(s_bmp, 0, SCAN_BMP_SIZE);
        for (int i = 0; i < cp->nscan_lists; i++) {
            const struct at_scan_list *s = &cp->scan_lists[i];
            if (s->index < 0 || s->index >= AT_MAX_SCANLISTS)
                continue;
            at_bitmap_set(s_bmp, (unsigned)s->index, 1);
            uint8_t *raw = at_image_find_mut(img, scan_addr((unsigned)s->index), SCAN_SIZE);
            if (!raw)
                continue;
            memset(raw, 0xff, SCAN_SIZE);
            raw[0] = 0;
            at_write_ascii(raw + 0x0f, 16, s->name);
            for (int j = 0; j < s->nmembers && j < AT_SCAN_MEMBERS; j++)
                at_put_u16_le(raw + 0x20 + j * 2, (uint16_t)s->members[j]);
        }
    }

    uint8_t *g_bmp = at_image_find_mut(img, OFF_RXG_BMP, RXG_BMP_SIZE);
    if (g_bmp) {
        memset(g_bmp, 0, RXG_BMP_SIZE);
        for (int i = 0; i < cp->nrx_groups; i++) {
            const struct at_rx_group *g = &cp->rx_groups[i];
            if (g->index < 0 || g->index >= AT_MAX_RXGROUPS)
                continue;
            at_bitmap_set(g_bmp, (unsigned)g->index, 1);
            uint8_t *raw =
                at_image_find_mut(img, OFF_RX_GROUPS + (unsigned)g->index * RXG_STRIDE,
                                  RXG_SIZE);
            if (!raw)
                continue;
            memset(raw, 0xff, 64 * 4); /* member slots */
            memset(raw + RXG_NAME_OFF, 0, RXG_SIZE - RXG_NAME_OFF);
            at_write_ascii(raw + RXG_NAME_OFF, 16, g->name);
            for (int j = 0; j < g->nmembers && j < AT_RXG_MEMBERS; j++)
                at_put_u32_le(raw + j * 4, (uint32_t)g->members[j]);
        }
    }

    uint8_t *boot = at_image_find_mut(img, OFF_BOOT, BOOT_SIZE);
    if (boot) {
        at_write_ascii(boot + 0x00, 16, cp->settings.intro_line1);
        at_write_ascii(boot + 0x10, 16, cp->settings.intro_line2);
    }

    memcpy(img->model, cp->model, sizeof(img->model));
    memcpy(img->fw_version, cp->fw_version, sizeof(img->fw_version));
    img->bands = cp->bands;
    return 0;
}

static cJSON *channel_json(const struct at_channel *ch)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "index", ch->index);
    cJSON_AddStringToObject(o, "name", ch->name);
    cJSON_AddNumberToObject(o, "rx_mhz", ch->rx_hz / 1e6);
    cJSON_AddNumberToObject(o, "tx_mhz", ch->tx_hz / 1e6);
    cJSON_AddNumberToObject(o, "mode", ch->mode);
    cJSON_AddNumberToObject(o, "power", ch->power);
    cJSON_AddBoolToObject(o, "bandwidth_wide", ch->bandwidth_wide);
    cJSON_AddNumberToObject(o, "color_code", ch->color_code);
    cJSON_AddNumberToObject(o, "timeslot", ch->timeslot);
    cJSON_AddNumberToObject(o, "contact_index", ch->contact_index);
    cJSON_AddNumberToObject(o, "radio_id_index", ch->radio_id_index);
    cJSON_AddNumberToObject(o, "scan_list_index", ch->scan_list_index);
    cJSON_AddNumberToObject(o, "rx_group_index", ch->rx_group_index);
    cJSON_AddBoolToObject(o, "rx_only", ch->rx_only);
    cJSON_AddNumberToObject(o, "admit", ch->admit);
    cJSON_AddNumberToObject(o, "rx_ctcss", ch->rx_ctcss);
    cJSON_AddNumberToObject(o, "tx_ctcss", ch->tx_ctcss);
    cJSON_AddNumberToObject(o, "rx_dcs", ch->rx_dcs);
    cJSON_AddNumberToObject(o, "tx_dcs", ch->tx_dcs);
    return o;
}

char *at_codeplug_to_json(const struct at_codeplug *cp)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", cp->model);
    cJSON_AddStringToObject(root, "fw_version", cp->fw_version);
    cJSON_AddNumberToObject(root, "bands", cp->bands);

    cJSON *settings = cJSON_CreateObject();
    cJSON_AddStringToObject(settings, "intro_line1", cp->settings.intro_line1);
    cJSON_AddStringToObject(settings, "intro_line2", cp->settings.intro_line2);
    cJSON_AddItemToObject(root, "settings", settings);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < cp->nradio_ids; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", cp->radio_ids[i].index);
        cJSON_AddStringToObject(o, "name", cp->radio_ids[i].name);
        cJSON_AddNumberToObject(o, "number", cp->radio_ids[i].number);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "radio_ids", arr);

    arr = cJSON_CreateArray();
    for (int i = 0; i < cp->ncontacts; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", cp->contacts[i].index);
        cJSON_AddStringToObject(o, "name", cp->contacts[i].name);
        cJSON_AddNumberToObject(o, "number", cp->contacts[i].number);
        cJSON_AddNumberToObject(o, "type", cp->contacts[i].type);
        cJSON_AddNumberToObject(o, "alert", cp->contacts[i].alert);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "contacts", arr);

    arr = cJSON_CreateArray();
    for (int i = 0; i < cp->nchannels; i++)
        cJSON_AddItemToArray(arr, channel_json(&cp->channels[i]));
    cJSON_AddItemToObject(root, "channels", arr);

    arr = cJSON_CreateArray();
    for (int i = 0; i < cp->nzones; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", cp->zones[i].index);
        cJSON_AddStringToObject(o, "name", cp->zones[i].name);
        cJSON *m = cJSON_CreateArray();
        for (int j = 0; j < cp->zones[i].nchannels; j++)
            cJSON_AddItemToArray(m, cJSON_CreateNumber(cp->zones[i].channels[j]));
        cJSON_AddItemToObject(o, "channels", m);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "zones", arr);

    arr = cJSON_CreateArray();
    for (int i = 0; i < cp->nscan_lists; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", cp->scan_lists[i].index);
        cJSON_AddStringToObject(o, "name", cp->scan_lists[i].name);
        cJSON *m = cJSON_CreateArray();
        for (int j = 0; j < cp->scan_lists[i].nmembers; j++)
            cJSON_AddItemToArray(m, cJSON_CreateNumber(cp->scan_lists[i].members[j]));
        cJSON_AddItemToObject(o, "members", m);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "scan_lists", arr);

    arr = cJSON_CreateArray();
    for (int i = 0; i < cp->nrx_groups; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", cp->rx_groups[i].index);
        cJSON_AddStringToObject(o, "name", cp->rx_groups[i].name);
        cJSON *m = cJSON_CreateArray();
        for (int j = 0; j < cp->rx_groups[i].nmembers; j++)
            cJSON_AddItemToArray(m, cJSON_CreateNumber(cp->rx_groups[i].members[j]));
        cJSON_AddItemToObject(o, "members", m);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "rx_groups", arr);

    char *s = cJSON_Print(root);
    cJSON_Delete(root);
    return s;
}

static int jnum(cJSON *o, const char *k, double *out)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsNumber(v))
        return -1;
    *out = v->valuedouble;
    return 0;
}

static const char *jstr(cJSON *o, const char *k)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}

int at_codeplug_from_json(struct at_codeplug *cp, const char *json)
{
    at_codeplug_free(cp);
    at_codeplug_init(cp);

    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;

    strncpy(cp->model, jstr(root, "model"), sizeof(cp->model) - 1);
    strncpy(cp->fw_version, jstr(root, "fw_version"), sizeof(cp->fw_version) - 1);
    double bands = 0;
    if (jnum(root, "bands", &bands) == 0)
        cp->bands = (uint8_t)bands;

    cJSON *settings = cJSON_GetObjectItemCaseSensitive(root, "settings");
    if (cJSON_IsObject(settings)) {
        strncpy(cp->settings.intro_line1, jstr(settings, "intro_line1"),
                sizeof(cp->settings.intro_line1) - 1);
        strncpy(cp->settings.intro_line2, jstr(settings, "intro_line2"),
                sizeof(cp->settings.intro_line2) - 1);
    }

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "radio_ids");
    if (cJSON_IsArray(arr)) {
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            struct at_radio_id *nr =
                realloc(cp->radio_ids, (cp->nradio_ids + 1) * sizeof(*nr));
            if (!nr)
                goto fail;
            cp->radio_ids = nr;
            struct at_radio_id *r = &cp->radio_ids[cp->nradio_ids++];
            memset(r, 0, sizeof(*r));
            double v;
            if (jnum(it, "index", &v) == 0)
                r->index = (int)v;
            strncpy(r->name, jstr(it, "name"), sizeof(r->name) - 1);
            if (jnum(it, "number", &v) == 0)
                r->number = (uint32_t)v;
        }
    }

    arr = cJSON_GetObjectItemCaseSensitive(root, "contacts");
    if (cJSON_IsArray(arr)) {
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            struct at_contact *nc =
                realloc(cp->contacts, (cp->ncontacts + 1) * sizeof(*nc));
            if (!nc)
                goto fail;
            cp->contacts = nc;
            struct at_contact *c = &cp->contacts[cp->ncontacts++];
            memset(c, 0, sizeof(*c));
            double v;
            if (jnum(it, "index", &v) == 0)
                c->index = (int)v;
            strncpy(c->name, jstr(it, "name"), sizeof(c->name) - 1);
            if (jnum(it, "number", &v) == 0)
                c->number = (uint32_t)v;
            if (jnum(it, "type", &v) == 0)
                c->type = (uint8_t)v;
            if (jnum(it, "alert", &v) == 0)
                c->alert = (uint8_t)v;
        }
    }

    arr = cJSON_GetObjectItemCaseSensitive(root, "channels");
    if (cJSON_IsArray(arr)) {
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            struct at_channel *nch =
                realloc(cp->channels, (cp->nchannels + 1) * sizeof(*nch));
            if (!nch)
                goto fail;
            cp->channels = nch;
            struct at_channel *ch = &cp->channels[cp->nchannels++];
            memset(ch, 0, sizeof(*ch));
            double v;
            if (jnum(it, "index", &v) == 0)
                ch->index = (int)v;
            strncpy(ch->name, jstr(it, "name"), sizeof(ch->name) - 1);
            if (jnum(it, "rx_mhz", &v) == 0)
                ch->rx_hz = (uint32_t)(v * 1e6 + 0.5);
            if (jnum(it, "tx_mhz", &v) == 0)
                ch->tx_hz = (uint32_t)(v * 1e6 + 0.5);
            if (jnum(it, "mode", &v) == 0)
                ch->mode = (uint8_t)v;
            if (jnum(it, "power", &v) == 0)
                ch->power = (uint8_t)v;
            cJSON *bw = cJSON_GetObjectItemCaseSensitive(it, "bandwidth_wide");
            ch->bandwidth_wide = cJSON_IsTrue(bw);
            if (jnum(it, "color_code", &v) == 0)
                ch->color_code = (uint8_t)v;
            if (jnum(it, "timeslot", &v) == 0)
                ch->timeslot = (uint8_t)v;
            ch->contact_index = -1;
            ch->radio_id_index = -1;
            ch->scan_list_index = -1;
            ch->rx_group_index = -1;
            if (jnum(it, "contact_index", &v) == 0)
                ch->contact_index = (int)v;
            if (jnum(it, "radio_id_index", &v) == 0)
                ch->radio_id_index = (int)v;
            if (jnum(it, "scan_list_index", &v) == 0)
                ch->scan_list_index = (int)v;
            if (jnum(it, "rx_group_index", &v) == 0)
                ch->rx_group_index = (int)v;
            cJSON *rxo = cJSON_GetObjectItemCaseSensitive(it, "rx_only");
            ch->rx_only = cJSON_IsTrue(rxo);
            if (jnum(it, "admit", &v) == 0)
                ch->admit = (uint8_t)v;
            if (jnum(it, "rx_ctcss", &v) == 0)
                ch->rx_ctcss = v;
            if (jnum(it, "tx_ctcss", &v) == 0)
                ch->tx_ctcss = v;
            if (jnum(it, "rx_dcs", &v) == 0)
                ch->rx_dcs = (uint16_t)v;
            if (jnum(it, "tx_dcs", &v) == 0)
                ch->tx_dcs = (uint16_t)v;
        }
    }

    arr = cJSON_GetObjectItemCaseSensitive(root, "zones");
    if (cJSON_IsArray(arr)) {
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            struct at_zone *nz = realloc(cp->zones, (cp->nzones + 1) * sizeof(*nz));
            if (!nz)
                goto fail;
            cp->zones = nz;
            struct at_zone *z = &cp->zones[cp->nzones++];
            memset(z, 0, sizeof(*z));
            double v;
            if (jnum(it, "index", &v) == 0)
                z->index = (int)v;
            strncpy(z->name, jstr(it, "name"), sizeof(z->name) - 1);
            cJSON *m = cJSON_GetObjectItemCaseSensitive(it, "channels");
            if (cJSON_IsArray(m)) {
                cJSON *mi;
                cJSON_ArrayForEach(mi, m) {
                    if (cJSON_IsNumber(mi) && z->nchannels < AT_ZONE_CHANS)
                        z->channels[z->nchannels++] = (int)mi->valuedouble;
                }
            }
        }
    }

    arr = cJSON_GetObjectItemCaseSensitive(root, "scan_lists");
    if (cJSON_IsArray(arr)) {
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            struct at_scan_list *ns =
                realloc(cp->scan_lists, (cp->nscan_lists + 1) * sizeof(*ns));
            if (!ns)
                goto fail;
            cp->scan_lists = ns;
            struct at_scan_list *s = &cp->scan_lists[cp->nscan_lists++];
            memset(s, 0, sizeof(*s));
            double v;
            if (jnum(it, "index", &v) == 0)
                s->index = (int)v;
            strncpy(s->name, jstr(it, "name"), sizeof(s->name) - 1);
            cJSON *m = cJSON_GetObjectItemCaseSensitive(it, "members");
            if (cJSON_IsArray(m)) {
                cJSON *mi;
                cJSON_ArrayForEach(mi, m) {
                    if (cJSON_IsNumber(mi) && s->nmembers < AT_SCAN_MEMBERS)
                        s->members[s->nmembers++] = (int)mi->valuedouble;
                }
            }
        }
    }

    arr = cJSON_GetObjectItemCaseSensitive(root, "rx_groups");
    if (cJSON_IsArray(arr)) {
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            struct at_rx_group *ng =
                realloc(cp->rx_groups, (cp->nrx_groups + 1) * sizeof(*ng));
            if (!ng)
                goto fail;
            cp->rx_groups = ng;
            struct at_rx_group *g = &cp->rx_groups[cp->nrx_groups++];
            memset(g, 0, sizeof(*g));
            double v;
            if (jnum(it, "index", &v) == 0)
                g->index = (int)v;
            strncpy(g->name, jstr(it, "name"), sizeof(g->name) - 1);
            cJSON *m = cJSON_GetObjectItemCaseSensitive(it, "members");
            if (cJSON_IsArray(m)) {
                cJSON *mi;
                cJSON_ArrayForEach(mi, m) {
                    if (cJSON_IsNumber(mi) && g->nmembers < AT_RXG_MEMBERS)
                        g->members[g->nmembers++] = (int)mi->valuedouble;
                }
            }
        }
    }

    cJSON_Delete(root);
    return 0;
fail:
    cJSON_Delete(root);
    at_codeplug_free(cp);
    return -1;
}
