/**
 * $Id$
 *
 * CDI CD-image file support
 *
 * Copyright (c) 2005 Nathan Keynes.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include "gdrom/gddriver.h"

#define CDI_V2_ID 0x80000004
#define CDI_V3_ID 0x80000005
#define CDI_V35_ID 0x80000006


static gboolean cdi_image_is_valid( FILE *f );
static gdrom_disc_t cdi_image_open( const gchar *filename, FILE *f );

struct gdrom_image_class cdi_image_class = { "DiscJuggler", "cdi", 
        cdi_image_is_valid, cdi_image_open };

static const char TRACK_START_MARKER[20] = { 0,0,1,0,0,0,255,255,255,255,
        0,0,1,0,0,0,255,255,255,255 };
static const char EXT_MARKER[9] = {0,255,255,255,255,255,255,255,255 };

struct cdi_trailer {
    uint32_t cdi_version;
    uint32_t header_offset;
};

struct cdi_track_data {
    uint32_t pregap_length;
    uint32_t length;
    char unknown2[6];
    uint32_t mode;
    char unknown3[0x0c];
    uint32_t start_lba;
    uint32_t total_length;
    char unknown4[0x10];
    uint32_t sector_size;
    char unknown5[0x1D];
} __attribute__((packed));

gboolean cdi_image_is_valid( FILE *f )
{
    int len;
    struct cdi_trailer trail;

    fseek( f, -8, SEEK_END );
    len = ftell(f)+8;
    fread( &trail, sizeof(trail), 1, f );
    if( trail.header_offset >= len ||
            trail.header_offset == 0 )
        return FALSE;
    return trail.cdi_version == CDI_V2_ID || trail.cdi_version == CDI_V3_ID ||
    trail.cdi_version == CDI_V35_ID;
}

gdrom_disc_t cdi_image_open( const gchar *filename, FILE *f )
{
    gdrom_disc_t disc = NULL;
    int i,j;
    uint16_t session_count;
    uint16_t track_count;
    int total_tracks = 0;
    int posn = 0;
    long len;
    struct cdi_trailer trail;
    char marker[20];

    fseek( f, -8, SEEK_END );
    len = ftell(f)+8;
    fread( &trail, sizeof(trail), 1, f );
    if( trail.header_offset >= len ||
            trail.header_offset == 0 )
        return NULL;

    if( trail.cdi_version != CDI_V2_ID && trail.cdi_version != CDI_V3_ID &&
            trail.cdi_version != CDI_V35_ID ) {
        return NULL;
    }

    if( trail.cdi_version == CDI_V35_ID ) {
        fseek( f, -(long)trail.header_offset, SEEK_END );
    } else {
        fseek( f, trail.header_offset, SEEK_SET );
    }
    fread( &session_count, sizeof(session_count), 1, f );

    disc = gdrom_image_new(filename, f);
    if( disc == NULL ) {
        ERROR("Unable to allocate memory!");
        return NULL;
    }

    for( i=0; i< session_count; i++ ) {        
        fread( &track_count, sizeof(track_count), 1, f );
        if( track_count + total_tracks > 99 ) {
            ERROR( "Invalid number of tracks, bad cdi image\n" );
            disc->destroy(disc,FALSE);
            return NULL;
        }
        for( j=0; j<track_count; j++ ) {
            struct cdi_track_data trk;
            uint32_t new_fmt = 0;
            uint8_t fnamelen = 0;
            fread( &new_fmt, sizeof(new_fmt), 1, f );
            if( new_fmt != 0 ) { /* Additional data 3.00.780+ ?? */
                fseek( f, 8, SEEK_CUR ); /* Skip */
            }
            fread( marker, 20, 1, f );
            if( memcmp( marker, TRACK_START_MARKER, 20) != 0 ) {
                ERROR( "Track start marker not found, error reading cdi image\n" );
                disc->destroy(disc,FALSE);
                return NULL;
            }
            fseek( f, 4, SEEK_CUR );
            fread( &fnamelen, 1, 1, f );
            fseek( f, (int)fnamelen, SEEK_CUR ); /* skip over the filename */
            fseek( f, 19, SEEK_CUR );
            fread( &new_fmt, sizeof(new_fmt), 1, f );
            if( new_fmt == 0x80000000 ) {
                fseek( f, 10, SEEK_CUR );
            } else {
                fseek( f, 2, SEEK_CUR );
            }
            fread( &trk, sizeof(trk), 1, f );
            disc->track[total_tracks].session = i;
            disc->track[total_tracks].lba = trk.start_lba + 150;
            disc->track[total_tracks].sector_count = trk.length;
            switch( trk.mode ) {
            case 0:
                disc->track[total_tracks].mode = GDROM_CDDA;
                disc->track[total_tracks].sector_size = 2352;
                disc->track[total_tracks].flags = 0x01;
                if( trk.sector_size != 2 ) {
                    ERROR( "Invalid combination of mode %d with size %d", trk.mode, trk.sector_size );
                    disc->destroy(disc,FALSE);
                    return NULL;
                }
                break;
            case 1:
                disc->track[total_tracks].mode = GDROM_MODE1;
                disc->track[total_tracks].sector_size = 2048;
                disc->track[total_tracks].flags = 0x41;
                if( trk.sector_size != 0 ) {
                    ERROR( "Invalid combination of mode %d with size %d", trk.mode, trk.sector_size );
                    disc->destroy(disc,FALSE);
                    return NULL;
                }
                break;
            case 2:
                disc->track[total_tracks].flags = 0x41;
                switch( trk.sector_size ) {
                case 0:
                    disc->track[total_tracks].mode = GDROM_MODE2_FORM1;
                    disc->track[total_tracks].sector_size = 2048;
                    break;
                case 1:
                    disc->track[total_tracks].mode = GDROM_SEMIRAW_MODE2;
                    disc->track[total_tracks].sector_size = 2336;
                    break;
                case 2:
                default:
                    ERROR( "Invalid combination of mode %d with size %d", trk.mode, trk.sector_size );
                    disc->destroy(disc,FALSE);
                    return NULL;
                }
                break;
                default:
                    ERROR( "Unsupported track mode %d", trk.mode );
                    disc->destroy(disc,FALSE);
                    return NULL;
            }
            disc->track[total_tracks].offset = posn + 
            trk.pregap_length * disc->track[total_tracks].sector_size ;
            posn += trk.total_length * disc->track[total_tracks].sector_size;
            total_tracks++;
            fread( marker, 1, 9, f );
            if( memcmp( marker, EXT_MARKER, 9 ) == 0 ) {
                fseek( f, 79, SEEK_CUR );
            } else {
                fseek( f, -9, SEEK_CUR );
            }
        }
        fseek( f, 12, SEEK_CUR );
    }
    disc->track_count = total_tracks;
    gdrom_set_disc_type(disc);
    return disc;
}
