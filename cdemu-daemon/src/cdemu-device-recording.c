/*
 *  CDEmu daemon: Device object - recording implementation
 *  Copyright (C) 2013-2014 Rok Mandeljc
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "cdemu.h"
#include "cdemu-device-private.h"


/**********************************************************************\
 *                         Generic recording                          *
\**********************************************************************/
#define __debug__ "Recording"

static gboolean cdemu_device_recording_write_sector (CdemuDevice *self, MirageSector *sector)
{
    const guint8 *data;

    /* Dump sector data for now */
    CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: dumping sector data to be written:\n", __debug__);

    /* First 32 bytes of main channel and 16-byte Q subchannel */
    mirage_sector_get_data(sector, &data, NULL, NULL);
    cdemu_device_dump_buffer(self, DAEMON_DEBUG_RECORDING, __debug__, 16, data, 32);
    CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: ...\n", __debug__);
    mirage_sector_get_subchannel(sector, MIRAGE_SUBCHANNEL_Q, &data, NULL, NULL);
    cdemu_device_dump_buffer(self, DAEMON_DEBUG_RECORDING, __debug__, 16, data, 16);

    return TRUE;
}

static gboolean cdemu_device_recording_close_track (CdemuDevice *self)
{
    if (self->priv->open_track) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: closing track (adding to layout)\n", __debug__);

        /* Add track to our open session */
        mirage_session_add_track_by_index(self->priv->open_session, -1, self->priv->open_track);

        /* Release the reference we hold */
        g_object_unref(self->priv->open_track);
        self->priv->open_track = NULL;
    }

    return TRUE;
}

static gboolean cdemu_device_recording_close_session (CdemuDevice *self)
{
    if (self->priv->open_session) {
        const struct ModePage_0x05 *p_0x05 = cdemu_device_get_mode_page(self, 0x05, MODE_PAGE_CURRENT);

        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: closing session (adding to layout)\n", __debug__);

        /* If we have an open track, close it */
        if (self->priv->open_track) {
            cdemu_device_recording_close_track(self);
        }

        /* Add session to our disc */
        mirage_disc_add_session_by_index(self->priv->disc, -1, self->priv->open_session);

        /* Release the reference we hold */
        g_object_unref(self->priv->open_session);
        self->priv->open_session = NULL;

        /* Should we finalize the disc, as well? */
        if (!p_0x05->multisession) {
            self->priv->disc_closed = TRUE;
        }

        self->priv->num_written_sectors = 0; /* Reset */
    }

    return TRUE;
}

static gboolean cdemu_device_recording_open_session (CdemuDevice *self)
{
    /* Create new session */
    self->priv->open_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);

    /* Determine session number and start sector from the disc; but do
       not add the session to the layout yet (do this when session is
       closed) */
    gint session_number = mirage_disc_layout_get_first_session(self->priv->disc) + mirage_disc_get_number_of_sessions(self->priv->disc);
    gint start_sector = mirage_disc_layout_get_start_sector(self->priv->disc) + mirage_disc_layout_get_length(self->priv->disc);
    gint first_track = mirage_disc_layout_get_first_track(self->priv->disc) + mirage_disc_get_number_of_tracks(self->priv->disc);

    mirage_session_layout_set_session_number(self->priv->open_session, session_number);
    mirage_session_layout_set_start_sector(self->priv->open_session, start_sector);
    mirage_session_layout_set_first_track(self->priv->open_session, first_track);

    CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: opened session #%d; start sector: %d, first track: %d!\n", __debug__, mirage_session_layout_get_session_number(self->priv->open_session), mirage_session_layout_get_start_sector(self->priv->open_session), mirage_session_layout_get_first_track(self->priv->open_session));

    return TRUE;
}

static gboolean cdemu_device_recording_open_track (CdemuDevice *self, MirageSectorType sector_type)
{
    /* Close old track, if it is opened */
    if (self->priv->open_track) {
        cdemu_device_recording_close_track(self);
    }

    /* Create new track */
    self->priv->open_track = g_object_new(MIRAGE_TYPE_TRACK, NULL);

    /* Set parent, because libMirage sector code expects to be able to
       chain up all the way to disc */
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->open_track), self->priv->open_session);

    /* Determine track number and start sector from open session; but do
       not add track to the layout yet (do this when track is closed) */
    gint track_number = mirage_session_layout_get_first_track(self->priv->open_session) + mirage_session_get_number_of_tracks(self->priv->open_session);
    gint start_sector = mirage_session_layout_get_start_sector(self->priv->open_session) + mirage_session_layout_get_length(self->priv->open_session);
    mirage_track_layout_set_track_number(self->priv->open_track, track_number);
    mirage_track_layout_set_start_sector(self->priv->open_track, start_sector);

    mirage_track_set_sector_type(self->priv->open_track, sector_type);

    CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: opened track #%d; sector type: %d; start sector: %d!\n", __debug__, mirage_track_layout_get_track_number(self->priv->open_track), sector_type, mirage_track_layout_get_start_sector(self->priv->open_track));

    return TRUE;
}


/**********************************************************************\
 *                    Track-at-once (TAO) recording                   *
\**********************************************************************/
#undef __debug__
#define __debug__ "TAO recording"

struct RECORDING_DataFormat
{
    gint main_size;
    gint subchannel_size;
    gint subchannel_format;
    gint sector_type;
};

static const struct RECORDING_DataFormat recording_data_formats[] = {
    /* 0: 2352 bytes - raw data */
    { 2352, 0, MIRAGE_SUBCHANNEL_NONE, MIRAGE_SECTOR_RAW },
    /* 1: 2368 bytes - raw data with P-Q subchannel */
    { 2352, 16, MIRAGE_SUBCHANNEL_Q, MIRAGE_SECTOR_RAW },
    /* 2: 2448 bytes - raw data with cooked R-W subchannel */
    { 2352, 96, MIRAGE_SUBCHANNEL_RW, MIRAGE_SECTOR_RAW },
    /* 3: 2448 bytes - raw data with raw P-W subchannel */
    { 2352, 96, MIRAGE_SUBCHANNEL_PW, MIRAGE_SECTOR_RAW },
    /* 4-7: reserved */
    { 0, 0, MIRAGE_SUBCHANNEL_NONE, MIRAGE_SECTOR_RAW },
    { 0, 0, MIRAGE_SUBCHANNEL_NONE, MIRAGE_SECTOR_RAW },
    { 0, 0, MIRAGE_SUBCHANNEL_NONE, MIRAGE_SECTOR_RAW },
    { 0, 0, MIRAGE_SUBCHANNEL_NONE, MIRAGE_SECTOR_RAW },
    /* 8: 2048 bytes - Mode 1 user data */
    { 2048, 0, MIRAGE_SUBCHANNEL_NONE, MIRAGE_SECTOR_MODE1 },
    /* 9: 2336 bytes - Mode 2 user data */
    { 2336, 0, MIRAGE_SUBCHANNEL_NONE, MIRAGE_SECTOR_MODE2 },
    /* 10: 2048 bytes - Mode 2 Form 1 user data */
    { 2048, 0, MIRAGE_SUBCHANNEL_NONE, MIRAGE_SECTOR_MODE2_FORM1 },
    /* 11: 2056 bytes - Mode 2 Form 1 with subheader */
    { 2056, 0, MIRAGE_SUBCHANNEL_NONE, MIRAGE_SECTOR_MODE2_FORM1 },
    /* 2324 bytes - Mode 2 Form 2 user data */
    { 2324, 0, MIRAGE_SUBCHANNEL_NONE, MIRAGE_SECTOR_MODE2_FORM2 },
    /* 2332 bytes - Mode 2 (Form 1, Form 2 or mixed) with subheader */
    { 2332, 0, MIRAGE_SUBCHANNEL_NONE, MIRAGE_SECTOR_MODE2_MIXED },
};


static gboolean cdemu_device_tao_recording_open_session (CdemuDevice *self)
{
    /* Use generic function first */
    if (!cdemu_device_recording_open_session(self)) {
        return FALSE;
    }

    /* Set MCN from write parameters mode page */
    const struct ModePage_0x05 *p_0x05 = cdemu_device_get_mode_page(self, 0x05, MODE_PAGE_CURRENT);
    if (p_0x05->mcn[0]) {
        /* We can get away with this because MCN data fields are followed by a ZERO field */
        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: setting MCN from write parameters mode page: %s\n", __debug__, &p_0x05->mcn[1]);
        mirage_session_set_mcn(self->priv->open_session, (gchar *)&p_0x05->mcn[1]);
    }

    return TRUE;
}

static gboolean cdemu_device_tao_recording_open_track (CdemuDevice *self, MirageSectorType sector_type)
{
    /* Use generic function first */
    if (!cdemu_device_recording_open_track(self, sector_type)) {
        return FALSE;
    }

    /* Set ISRC from write parameters mode page */
    const struct ModePage_0x05 *p_0x05 = cdemu_device_get_mode_page(self, 0x05, MODE_PAGE_CURRENT);
    if (p_0x05->isrc[0]) {
        /* We can get away with this because ISRC data fields are followed by a ZERO field*/
        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: setting ISRC from write parameters mode page: %s\n", __debug__, &p_0x05->isrc[1]);
        mirage_track_set_isrc(self->priv->open_track, (gchar *)&p_0x05->isrc[1]);
    }

    /* The track needs a pregap */
    MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL); /* FIXME: need image writer to allocate this properly! */
    mirage_fragment_set_length(fragment, 150);
    mirage_track_add_fragment(self->priv->open_track, -1, fragment);
    g_object_unref(fragment);

    mirage_track_set_track_start(self->priv->open_track, 150);

    self->priv->num_written_sectors += 150;

    /* Data fragment */
    fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL); /* FIXME: need image writer to allocate this properly! */
    mirage_track_add_fragment(self->priv->open_track, -1, fragment);
    g_object_unref(fragment);

    return TRUE;
}

static gboolean cdemu_device_tao_recording_write_sector (CdemuDevice *self, MirageSector *sector)
{
    /* If there is no opened session, open one */
    if (!self->priv->open_session) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: no session opened; opening one!\n", __debug__);

        if (!cdemu_device_tao_recording_open_session(self)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: failed to open new session!\n", __debug__);
            return FALSE;
        }
    }

    /* If there is no opened track, open one */
    if (!self->priv->open_track) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: no track opened; opening one!\n", __debug__);

        if (!cdemu_device_tao_recording_open_track(self, mirage_sector_get_sector_type(sector))) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: failed to open new track!\n", __debug__);
            return FALSE;
        }
    }

    /* FIXME: write sector on libMirage's side */
    mirage_object_set_parent(MIRAGE_OBJECT(sector), self->priv->open_track);

    /* Increase the size of the last track's fragment */
    MirageFragment *fragment = mirage_track_get_fragment_by_index(self->priv->open_track, -1, NULL);
    mirage_fragment_set_length(fragment, mirage_fragment_get_length(fragment) + 1);
    g_object_unref(fragment);

    cdemu_device_recording_write_sector(self, sector);

    return TRUE;
}

static gboolean cdemu_device_tao_recording_write_sectors (CdemuDevice *self, gint start_address, gint num_sectors)
{
    const struct ModePage_0x05 *p_0x05 = cdemu_device_get_mode_page(self, 0x05, MODE_PAGE_CURRENT);
    const struct RECORDING_DataFormat *format = &recording_data_formats[p_0x05->data_block_type]; /* FIXME: we should force validity of this with mode page validation! */
    gint sector_type = format->sector_type;

    MirageSector *sector = g_object_new(MIRAGE_TYPE_SECTOR, NULL);

    GError *local_error = NULL;
    gboolean succeeded = TRUE;

    /* Write all sectors */
    for (gint address = start_address; address < start_address + num_sectors; address++) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: sector %d\n", __debug__, address);

        /* Read data from host */
        cdemu_device_read_buffer(self, format->main_size + format->subchannel_size);

        /* If we have a track open, we have already determined sector
           type and do not have to do it again */
        if (sector_type == MIRAGE_SECTOR_RAW && self->priv->open_track) {
            sector_type = mirage_track_get_sector_type(self->priv->open_track);
        }

        /* Feed sector data */
        if (!mirage_sector_feed_data(sector, address, sector_type, self->priv->buffer, format->main_size, format->subchannel_format, self->priv->buffer + format->main_size, format->subchannel_size, 0, &local_error)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to feed sector for writing: %s!\n", __debug__, local_error->message);
            g_error_free(local_error);
            local_error = NULL;
            break;
        }

        /* If data type is 10 or 12, we need to copy subheader from
           write parameters page */
        if (p_0x05->data_block_type == 10 || p_0x05->data_block_type == 12) {
            mirage_sector_set_subheader(sector, p_0x05->subheader, sizeof(p_0x05->subheader), NULL);
        }

        /* Write sector */
        succeeded = cdemu_device_tao_recording_write_sector(self, sector);
        if (!succeeded) {
            break;
        }

        self->priv->num_written_sectors++;
    }

    g_object_unref(sector);

    return succeeded;
}

static gint cdemu_device_tao_recording_get_next_writable_address (CdemuDevice *self)
{
    /* NWA base is at the beginning of first track */
    gint nwa_base = 0; /* FIXME: multisession! */
    return nwa_base + self->priv->num_written_sectors;
}

/* Commands structure */
static const CdemuRecording recording_commands_tao = {
    .get_next_writable_address = cdemu_device_tao_recording_get_next_writable_address,
    .close_track = cdemu_device_recording_close_track, /* Use generic function */
    .close_session = cdemu_device_recording_close_session, /* Use generic function */
    .write_sectors = cdemu_device_tao_recording_write_sectors,
};


/**********************************************************************\
 *                           Raw recording                            *
\**********************************************************************/
#undef __debug__
#define __debug__ "RAW recording"

static gboolean cdemu_device_raw_recording_write_sector (CdemuDevice *self, gint address, MirageSector *sector)
{
    const guint8 *subchannel;

    /* Analyze subchannel to determine when tracks begin/end */
    mirage_sector_get_subchannel(sector, MIRAGE_SUBCHANNEL_Q, &subchannel, NULL, NULL);

    guint8 ctl = subchannel[0] & 0x0F;
    guint8 tno = subchannel[1];
    guint8 idx = subchannel[2];
    gint track_relative_address = mirage_helper_msf2lba(mirage_helper_bcd2hex(subchannel[3]), mirage_helper_bcd2hex(subchannel[4]), mirage_helper_bcd2hex(subchannel[5]), FALSE);
    gint absolute_address = mirage_helper_msf2lba(mirage_helper_bcd2hex(subchannel[7]), mirage_helper_bcd2hex(subchannel[8]), mirage_helper_bcd2hex(subchannel[9]), TRUE);

    /* Lead-in; contains TOC in Q and CD-TEXT in RW subchannel.
       Unfortunately, the TOC is not detailed enough for our needs,
       therefore we will infer session layout from sectors' data. */
    if (tno == 0x00) {
        if (!self->priv->open_session) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: first lead-in sector; opening session\n", __debug__);
            cdemu_device_recording_open_session(self);

            self->priv->last_recorded_tno = 0;
            self->priv->last_recorded_idx = 0;
        }

        /* FIXME: parse CD-TEXT from RW subchannel */

        return TRUE;
    }

    /* Lead-out; contains no useful data. It tells us when to officially
       close the session, though */
    if (tno == 0xAA) {
        if (self->priv->open_session) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: first lead-out sector; closing session\n", __debug__);
            cdemu_device_recording_close_session(self);
        }

        return TRUE;
    }


    /* At this point, make sure we have an open session; if not, we are in
       leadout, and probably trying to write sector containing MCN... */
    if (!self->priv->open_session) {
        return TRUE;
    }


    /* Track data */
    if (ctl == 1) {
        /* Validate address */
        if (absolute_address != address) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: command LBA %d does not match LBA encoded in sector %d\n", __debug__, address, absolute_address);
        }

        if (tno != self->priv->last_recorded_tno) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: TNO changed; open new track (mode: %d)\n", __debug__, mirage_sector_get_sector_type(sector));

            if (self->priv->open_track) {
                cdemu_device_recording_close_track(self);
            }

            cdemu_device_recording_open_track(self, mirage_sector_get_sector_type(sector));

            if (idx == 0) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: track has a pregap with length: %d\n", __debug__, track_relative_address + 1);
                mirage_track_set_track_start(self->priv->open_track, track_relative_address + 1);

                /* Create pregap fragment */
                MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL); /* FIXME: need image writer to allocate this properly! */
                mirage_track_add_fragment(self->priv->open_track, -1, fragment);
                g_object_unref(fragment);
            } else {
                /* Create data fragment */
                MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL); /* FIXME: need image writer to allocate this properly! */
                mirage_track_add_fragment(self->priv->open_track, -1, fragment);
                g_object_unref(fragment);
            }

            self->priv->last_recorded_tno = tno;
            self->priv->last_recorded_idx = idx;
        } else if (idx != self->priv->last_recorded_idx) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: index changed: %d -> %d\n", __debug__, self->priv->last_recorded_idx, idx);
            if (idx == 1) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: end of pregap\n", __debug__);

                /* Create data fragment */
                MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL); /* FIXME: need image writer to allocate this properly! */
                mirage_track_add_fragment(self->priv->open_track, -1, fragment);
                g_object_unref(fragment);
            } else {
                CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: adding index at track-relative address: %d\n", __debug__, track_relative_address);
                mirage_track_add_index(self->priv->open_track, track_relative_address, NULL);
            }

            self->priv->last_recorded_idx = idx;
        }
    } else if (ctl == 2) {
        /* MCN */
        if (self->priv->open_session && !mirage_session_get_mcn(self->priv->open_session)) {
            gchar mcn[13] = "";
            mirage_helper_subchannel_q_decode_mcn(&subchannel[1], mcn);
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: setting MCN: %s\n", __debug__, mcn);
            mirage_session_set_mcn(self->priv->open_session, mcn);
        }
    } else if (ctl == 3) {
        /* ISRC */
        if (self->priv->open_track && !mirage_track_get_isrc(self->priv->open_track)) {
            gchar isrc[12] = "";
            mirage_helper_subchannel_q_decode_isrc(&subchannel[1], isrc);
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: setting ISRC: %s\n", __debug__, isrc);
            mirage_track_set_isrc(self->priv->open_track, isrc);
        }
    }

    /* FIXME: write sector on libMirage's side */
    mirage_object_set_parent(MIRAGE_OBJECT(sector), self->priv->open_track);

    /* Increase the size of the last track's fragment */
    MirageFragment *fragment = mirage_track_get_fragment_by_index(self->priv->open_track, -1, NULL);
    mirage_fragment_set_length(fragment, mirage_fragment_get_length(fragment) + 1);
    g_object_unref(fragment);

    cdemu_device_recording_write_sector(self, sector);

    return TRUE;
}

static gboolean cdemu_device_raw_recording_write_sectors (CdemuDevice *self, gint start_address, gint num_sectors)
{
    MirageSector *sector = g_object_new(MIRAGE_TYPE_SECTOR, NULL);

    GError *local_error = NULL;
    gboolean succeeded = TRUE;

    const struct ModePage_0x05 *p_0x05 = cdemu_device_get_mode_page(self, 0x05, MODE_PAGE_CURRENT);
    const struct RECORDING_DataFormat *format = &recording_data_formats[p_0x05->data_block_type]; /* FIXME: we should force validity of this with mode page validation! */

    /* Write all sectors */
    for (gint address = start_address; address < start_address + num_sectors; address++) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: sector %d\n", __debug__, address);

        /* Read data from host */
        cdemu_device_read_buffer(self, format->main_size + format->subchannel_size);

        /* Feed the sector; in raw recording, sectors are scrambled and raw */
        if (!mirage_sector_feed_data(sector, address, MIRAGE_SECTOR_RAW_SCRAMBLED, self->priv->buffer, format->main_size, format->subchannel_format, self->priv->buffer + format->main_size, format->subchannel_size, 0, &local_error)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to feed sector for writing: %s!\n", __debug__, local_error->message);
            g_error_free(local_error);
            local_error = NULL;
            break;
        }

        /* Write sector */
        succeeded = cdemu_device_raw_recording_write_sector(self, address, sector);
        if (!succeeded) {
            break;
        }

        self->priv->num_written_sectors ++;
    }

    g_object_unref(sector);

    return succeeded;
}

static gint cdemu_device_raw_recording_get_next_writable_address (CdemuDevice *self)
{
    /* NWA base is at the beginning of lead-in */
    gint nwa_base = self->priv->medium_leadin; /* FIXME: multisession! */
    return nwa_base + self->priv->num_written_sectors;
}


/* Commands structure */
static const CdemuRecording recording_commands_raw = {
    .get_next_writable_address = cdemu_device_raw_recording_get_next_writable_address,
    .close_track = cdemu_device_recording_close_track, /* Use generic function */
    .close_session = cdemu_device_recording_close_session, /* Use generic function */
    .write_sectors = cdemu_device_raw_recording_write_sectors,
};


/**********************************************************************\
 *                   Session-at-once (SAO) recording                  *
\**********************************************************************/
#undef __debug__
#define __debug__ "SAO recording"

struct SAO_MainFormat
{
    gint format;
    MirageSectorType sector_type;
    gint data_size;
    gint ignore_data;
};

struct SAO_SubchannelFormat
{
    gint format;
    MirageSectorSubchannelFormat mode;
    gint data_size;
};

static const struct SAO_MainFormat sao_main_formats[] = {
    /* CD-DA */
    { 0x00, MIRAGE_SECTOR_AUDIO, 2352, 0 },
    { 0x01, MIRAGE_SECTOR_AUDIO,    0, 0 },
    /* CD-ROM Mode 1 */
    { 0x10, MIRAGE_SECTOR_MODE1, 2048, 0 },
    { 0x11, MIRAGE_SECTOR_MODE1, 2352, MIRAGE_VALID_SYNC | MIRAGE_VALID_HEADER | MIRAGE_VALID_EDC_ECC },
    { 0x12, MIRAGE_SECTOR_MODE1, 2048, MIRAGE_VALID_DATA },
    { 0x13, MIRAGE_SECTOR_MODE1, 2352, MIRAGE_VALID_SYNC | MIRAGE_VALID_HEADER | MIRAGE_VALID_DATA | MIRAGE_VALID_EDC_ECC },
    { 0x14, MIRAGE_SECTOR_MODE1,    0, 0 },
    /* CD-ROM XA, CD-I */
    { 0x20, MIRAGE_SECTOR_MODE2_MIXED, 2336, MIRAGE_VALID_EDC_ECC },
    { 0x21, MIRAGE_SECTOR_MODE2_MIXED, 2352, MIRAGE_VALID_SYNC | MIRAGE_VALID_HEADER | MIRAGE_VALID_EDC_ECC },
    { 0x22, MIRAGE_SECTOR_MODE2_MIXED, 2336, MIRAGE_VALID_DATA | MIRAGE_VALID_EDC_ECC },
    { 0x23, MIRAGE_SECTOR_MODE2_MIXED, 2352, MIRAGE_VALID_SYNC | MIRAGE_VALID_HEADER | MIRAGE_VALID_DATA | MIRAGE_VALID_EDC_ECC },
    { 0x24, MIRAGE_SECTOR_MODE2_FORM2,    0, 0 },
    /* CD-ROM Mode 2*/
    { 0x30, MIRAGE_SECTOR_MODE2, 2336, 0 },
    { 0x31, MIRAGE_SECTOR_MODE2, 2352, MIRAGE_VALID_SYNC | MIRAGE_VALID_HEADER },
    { 0x32, MIRAGE_SECTOR_MODE2, 2336, MIRAGE_VALID_DATA },
    { 0x33, MIRAGE_SECTOR_MODE2, 2352, MIRAGE_VALID_SYNC | MIRAGE_VALID_HEADER | MIRAGE_VALID_DATA },
    { 0x34, MIRAGE_SECTOR_MODE2,    0, 0 },
};

static const struct SAO_SubchannelFormat sao_subchannel_formats[] = {
    { 0x00, MIRAGE_SUBCHANNEL_NONE,  0 },
    { 0x01, MIRAGE_SUBCHANNEL_PW,   96 },
    { 0x03, MIRAGE_SUBCHANNEL_RW,   96 },
};

static const struct SAO_MainFormat *sao_main_formats_find (gint format)
{
    const struct SAO_MainFormat *descriptor = NULL;

    format &= 0x3F;

    for (guint i = 0; i < G_N_ELEMENTS(sao_main_formats); i++) {
        if (sao_main_formats[i].format == format) {
            descriptor = &sao_main_formats[i];
            break;
        }
    }

    return descriptor;
}

static const struct SAO_SubchannelFormat *sao_subchannel_formats_find (gint format)
{
    const struct SAO_SubchannelFormat *descriptor = NULL;

    format >>= 6;

    for (guint i = 0; i < G_N_ELEMENTS(sao_main_formats); i++) {
        if (sao_subchannel_formats[i].format == format) {
            descriptor = &sao_subchannel_formats[i];
            break;
        }
    }

    return descriptor;
}

static gboolean cdemu_device_sao_recording_open_session (CdemuDevice *self)
{
    /* Use generic function first */
    if (!cdemu_device_recording_open_session(self)) {
        return FALSE;
    }

    /* Copy MCN from CUE sheet */
    mirage_session_set_mcn(self->priv->open_session, mirage_session_get_mcn(self->priv->cue_sheet));

    return TRUE;
}

static gboolean cdemu_device_sao_recording_open_track (CdemuDevice *self)
{
    /* Use generic function first */
    if (!cdemu_device_recording_open_track(self, mirage_track_get_sector_type(self->priv->cue_entry))) {
        return FALSE;
    }

    /* Setup fragments */
    gint num_fragments = mirage_track_get_number_of_fragments(self->priv->cue_entry);

    for (gint i = 0; i < num_fragments; i++) {
        MirageFragment *entry_fragment = mirage_track_get_fragment_by_index(self->priv->cue_entry, i, NULL);
        MirageFragment *track_fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL); /* FIXME: need image writer to allocate this properly! */

        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: constructing fragment #%d from CUE sheet - length %d\n", __debug__, i, mirage_fragment_get_length(entry_fragment));

        mirage_fragment_set_length(track_fragment, mirage_fragment_get_length(entry_fragment));

        mirage_track_add_fragment(self->priv->open_track, -1, track_fragment);

        g_object_unref(entry_fragment);
        g_object_unref(track_fragment);
    }


    /* Setup the properties from CUE entry */
    mirage_track_set_flags(self->priv->open_track, mirage_track_get_flags(self->priv->cue_entry));
    mirage_track_set_isrc(self->priv->open_track, mirage_track_get_isrc(self->priv->cue_entry));

    /* Start and indices */
    mirage_track_set_track_start(self->priv->open_track, mirage_track_get_track_start(self->priv->cue_entry));

    gint num_indices = mirage_track_get_number_of_indices(self->priv->cue_entry);
    for (gint i = 0; i < num_indices; i++) {
        MirageIndex *index = mirage_track_get_index_by_number(self->priv->cue_entry, i, NULL);
        mirage_track_add_index(self->priv->open_track, mirage_index_get_address(index), NULL);
        g_object_unref(index);
    }

    return TRUE;
}

static gboolean cdemu_device_sao_recording_write_sectors (CdemuDevice *self, gint start_address, gint num_sectors)
{
    /* We need a valid CUE sheet */
    if (!self->priv->cue_sheet) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: CUE sheet not set!\n", __debug__);
        cdemu_device_write_sense(self, CHECK_CONDITION, COMMAND_SEQUENCE_ERROR);
        return FALSE;
    }

    const struct SAO_MainFormat *main_format_ptr = NULL;
    const struct SAO_SubchannelFormat *subchannel_format_ptr = NULL;

    MirageFragment *cue_fragment = NULL;

    gboolean succeeded = TRUE;

    MirageSector *sector = g_object_new(MIRAGE_TYPE_SECTOR, NULL);
    GError *local_error = NULL;

    /* Write all sectors */
    for (gint address = start_address; address < start_address + num_sectors; address++) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: sector %d\n", __debug__, address);

        /* Grab track entry from CUE sheet, if necessary */
        if (!self->priv->cue_entry || !mirage_track_layout_contains_address(self->priv->cue_entry, address)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: getting track entry from CUE sheet for sector %d...\n", __debug__, address);

            if (self->priv->cue_entry) {
                g_object_unref(self->priv->cue_entry);
            }

            self->priv->cue_entry = mirage_session_get_track_by_address(self->priv->cue_sheet, address, NULL);
            if (!self->priv->cue_entry) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: failed to find track entry in CUE sheet for address %d!\n", __debug__, address);
                cdemu_device_write_sense(self, CHECK_CONDITION, COMMAND_SEQUENCE_ERROR);
                succeeded = FALSE;
                goto finish;
            }

            if (cue_fragment) {
                g_object_unref(cue_fragment);
                cue_fragment = NULL;
            }

            /* Open new track for writing */
            if (!self->priv->open_session) {
                cdemu_device_sao_recording_open_session(self);
            }
            cdemu_device_sao_recording_open_track(self);
        }

        /* Grab fragment entry from CUE sheet, if necessary */
        gint track_start = mirage_track_layout_get_start_sector(self->priv->cue_entry);
        if (!cue_fragment || !mirage_fragment_contains_address(cue_fragment, address - track_start)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: getting fragment entry from CUE track entry for sector %d...\n", __debug__, address);

            if (cue_fragment) {
                g_object_unref(cue_fragment);
            }

            cue_fragment = mirage_track_get_fragment_by_address(self->priv->cue_entry, address - track_start, NULL);
            if (!cue_fragment) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: failed to find fragment entry in CUE track entry for address %d!\n", __debug__, address);
                cdemu_device_write_sense(self, CHECK_CONDITION, COMMAND_SEQUENCE_ERROR);
                succeeded = FALSE;
                goto finish;
            }

            /* Get data format for this fragment */
            gint format = mirage_fragment_main_data_get_format(cue_fragment);

            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: data type for subsequent sectors: 0x%X)!\n", __debug__, format);

            /* Find corresponding format descriptors */
            main_format_ptr = sao_main_formats_find(format);
            subchannel_format_ptr = sao_subchannel_formats_find(format);
        }

        /* Make sure we have data format descriptors set */
        if (!main_format_ptr|| !subchannel_format_ptr) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: data format not set!\n", __debug__);
            cdemu_device_write_sense(self, CHECK_CONDITION, COMMAND_SEQUENCE_ERROR);
            succeeded = FALSE;
            goto finish;
        }

        /* Read data from host */
        cdemu_device_read_buffer(self, main_format_ptr->data_size + subchannel_format_ptr->data_size);

        /* Feed the sector */
        if (!mirage_sector_feed_data(sector, address, main_format_ptr->sector_type, self->priv->buffer, main_format_ptr->data_size, subchannel_format_ptr->mode, self->priv->buffer + main_format_ptr->data_size, subchannel_format_ptr->data_size, main_format_ptr->ignore_data, &local_error)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to feed sector for writing: %s!\n", __debug__, local_error->message);
            g_error_free(local_error);
            local_error = NULL;
        }

        /* FIXME: write sector on libMirage's side */
        mirage_object_set_parent(MIRAGE_OBJECT(sector), self->priv->open_track);
        cdemu_device_recording_write_sector(self, sector);

        self->priv->num_written_sectors++;
    }

finish:

    /* Check if we have reached end of session */
    if (start_address + num_sectors >= mirage_session_layout_get_start_sector(self->priv->cue_sheet) + mirage_session_layout_get_length(self->priv->cue_sheet)) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: end of session reached; closing\n", __debug__);
        cdemu_device_recording_close_session(self);
    }

    g_object_unref(sector);

    if (cue_fragment) {
        g_object_unref(cue_fragment);
    }

    return succeeded;
}

static void cdemu_device_sao_recording_create_cue_sheet (CdemuDevice *self)
{
    /* Clear old CUE sheet model and create new session for it */
    if (self->priv->cue_sheet) {
        g_object_unref(self->priv->cue_sheet);
    }
    self->priv->cue_sheet = g_object_new(MIRAGE_TYPE_SESSION, NULL);

    /* Set session number, start sector and first track number */
    gint session_number = mirage_disc_layout_get_first_session(self->priv->disc) + mirage_disc_get_number_of_sessions(self->priv->disc);
    gint start_sector = mirage_disc_layout_get_start_sector(self->priv->disc) + mirage_disc_layout_get_length(self->priv->disc);
    gint first_track = mirage_disc_layout_get_first_track(self->priv->disc) + mirage_disc_get_number_of_tracks(self->priv->disc);

    mirage_session_layout_set_session_number(self->priv->cue_sheet, session_number);
    mirage_session_layout_set_start_sector(self->priv->cue_sheet, start_sector);
    mirage_session_layout_set_first_track(self->priv->cue_sheet, first_track);

    /* Grab session type from Mode Page 0x05 */
    struct ModePage_0x05 *p_0x05 = cdemu_device_get_mode_page(self, 0x05, MODE_PAGE_CURRENT);
    switch (p_0x05->session_format) {
        case 0x00: {
            mirage_session_set_session_type(self->priv->cue_sheet, MIRAGE_SESSION_CD_ROM);
            break;
        }
        case 0x10: {
            mirage_session_set_session_type(self->priv->cue_sheet, MIRAGE_SESSION_CD_I);
            break;
        }
        case 0x20: {
            mirage_session_set_session_type(self->priv->cue_sheet, MIRAGE_SESSION_CD_ROM_XA);
            break;
        }
    }
}

gboolean cdemu_device_sao_recording_parse_cue_sheet (CdemuDevice *self, const guint8 *cue_sheet, gint cue_sheet_size)
{
    gint num_entries = cue_sheet_size / 8;

    CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: number of CUE sheet entries: %d\n", __debug__, num_entries);

    /* We build our internal representation of a CUE sheet inside a
       MirageSession object. In order to minimize necessary book-keeping,
       we process CUE sheet in several passes:
       1. create all tracks
       2. determine tracks' lengths and pregaps
       3. determine indices and set MCN and ISRCs */

    cdemu_device_sao_recording_create_cue_sheet(self);

    /* First pass */
    CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: first pass: creating tracks...\n", __debug__);
    for (gint i = 0; i < num_entries; i++) {
        const guint8 *cue_entry = cue_sheet + i*8;

        gint adr = (cue_entry[0] & 0x0F);
        gint tno = cue_entry[1];
        gint idx = cue_entry[2];

        /* Here we are interested only in ADR 1 entries */
        if (adr != 1) {
            continue;
        }

        /* Skip lead-in and lead-out */
        if (tno == 0 || tno == 0xAA) {
            continue;
        }

        /* Ignore index entries */
        if (idx > 1) {
            continue;
        }

        MirageTrack *track = mirage_session_get_track_by_number(self->priv->cue_sheet, tno, NULL);
        if (!track) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: creating track #%d\n", __debug__, tno);

            /* Create track entry */
            track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
            mirage_session_add_track_by_number(self->priv->cue_sheet, tno, track, NULL);

            /* Determine track's sector type from format */
            const struct SAO_MainFormat *main_format_ptr = sao_main_formats_find(cue_entry[3]);
            if (main_format_ptr) {
                mirage_track_set_sector_type(track, main_format_ptr->sector_type);
            } else {
                CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: invalid format 0x%X for TNO %d in CUE sheet!\n", __debug__, cue_entry[3], tno);
            }
        }
        g_object_unref(track);
    }

    /* Second pass; this one goes backwards, because it's easier to
       compute lengths that way */
    CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: second pass: setting lengths and data formats...\n", __debug__);
    gint last_address = 0;
    for (gint i = num_entries - 1; i >= 0; i--) {
        const guint8 *cue_entry = cue_sheet + i*8;

        gint adr = (cue_entry[0] & 0x0F);
        gint tno = cue_entry[1];
        gint idx = cue_entry[2];

        /* Here we are interested only in ADR 1 entries */
        if (adr != 1) {
            continue;
        }

        /* Skip lead-in */
        if (tno == 0) {
            continue;
        }

        /* Ignore index entries */
        if (idx > 1) {
            continue;
        }

        /* Convert MSF to address */
        gint address = mirage_helper_msf2lba(cue_entry[5], cue_entry[6], cue_entry[7], TRUE);

        if (tno != 0xAA) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: track #%d index #%d has length of %d sectors; data format: %02hX\n", __debug__, tno, idx, (last_address - address), cue_entry[3]);

            MirageTrack *track = mirage_session_get_track_by_number(self->priv->cue_sheet, tno, NULL);

            MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
            mirage_fragment_set_length(fragment, last_address - address);
            mirage_fragment_main_data_set_format(fragment, cue_entry[3]); /* We abuse fragment's main channel format to store the data form supplied by CUE sheet */
            mirage_track_add_fragment(track, 0, fragment);

            /* Pregap */
            if (idx == 0) {
                mirage_track_set_track_start(track, last_address - address);
            }

            g_object_unref(fragment);
            g_object_unref(track);
        }
        last_address = address;
    }

    /* Final pass: ISRC, MCN and indices */
    CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: final pass: ISRC, MCN and indices...\n", __debug__);
    for (gint i = 0; i < num_entries; i++) {
        const guint8 *cue_entry = cue_sheet + i*8;

        gint adr = (cue_entry[0] & 0x0F);
        gint tno = cue_entry[1];
        gint idx = cue_entry[2];

        if (adr == 1) {
            gint address = mirage_helper_msf2lba(cue_entry[5], cue_entry[6], cue_entry[7], TRUE);

            if (idx == 1) {
                last_address = address;
            } else if (idx > 1) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: adding track #%d, index #%d\n", __debug__, tno, idx);

                MirageTrack *track = mirage_session_get_track_by_number(self->priv->cue_sheet, tno, NULL);
                mirage_track_add_index(track, address - last_address, NULL);
                g_object_unref(track);
            }
        } else if (adr == 2 || adr == 3) {
            /* MCN or ISRC; this means next entry must be valid, and must have same adr! */
            if (i + 1 >= num_entries) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: missing next CUE entry for MCN/ISRC; skipping!\n", __debug__, i);
                continue;
            }

            const guint8 *next_cue_entry = cue_sheet + (i + 1)*8;
            if ((next_cue_entry[0] & 0x0F) != adr) {
                continue;
            }

            if (adr == 2) {
                /* MCN */
                gchar *mcn = g_malloc0(13 + 1);
                memcpy(mcn, cue_entry+1, 7);
                memcpy(mcn+7, next_cue_entry+1, 6);
                CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: MCN: %s\n", __debug__, mcn);
                mirage_session_set_mcn(self->priv->cue_sheet, mcn);
                g_free(mcn);
            } else {
                /* ISRC */
                gchar *isrc = g_malloc0(12 + 1);
                memcpy(isrc, cue_entry+2, 6);
                memcpy(isrc+6, next_cue_entry+2, 6);
                CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: ISRC for track #%d: %s\n", __debug__, cue_entry[1], isrc);

                MirageTrack *track = mirage_session_get_track_by_number(self->priv->cue_sheet, tno, NULL);
                mirage_track_set_isrc(track, isrc);
                g_object_unref(track);

                g_free(isrc);
            }

            i++;
        }
    }

    return TRUE;
}

static gint cdemu_device_sao_recording_get_next_writable_address (CdemuDevice *self)
{
    /* NWA base is at the beginning of first-track's pregap */
    gint nwa_base = -150; /* FIXME: multisession! */
    return nwa_base + self->priv->num_written_sectors;
}


/* Commands structure */
static const CdemuRecording recording_commands_sao = {
    .get_next_writable_address = cdemu_device_sao_recording_get_next_writable_address,
    .close_track = cdemu_device_recording_close_track, /* Use generic function */
    .close_session = cdemu_device_recording_close_session, /* Use generic function */
    .write_sectors = cdemu_device_sao_recording_write_sectors,
};


/**********************************************************************\
 *                        Recording mode switch                       *
\**********************************************************************/
#undef __debug__
#define __debug__ "Recording"

void cdemu_device_recording_set_mode (CdemuDevice *self, gint mode)
{
    /* Activate mode */
    switch (mode) {
        case 1: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: activating track-at-once recording\n", __debug__);
            self->priv->recording = &recording_commands_tao;
            break;
        }
        case 2: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: activating session-at-once recording\n", __debug__);
            self->priv->recording = &recording_commands_sao;
            break;
        }
        case 3: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_RECORDING, "%s: activating raw recording\n", __debug__);
            self->priv->recording = &recording_commands_raw;
            break;
        }
        default: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: unhandled recording mode: %d\n", __debug__, mode);
            self->priv->recording = NULL;
            break;
        }
    }
}
