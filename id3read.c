/* id3read.c
   interface for the id3tag library
   Copyright (C) 2001 Linus Walleij

This file is part of the GNOMAD package.

GNOMAD is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

You should have received a copy of the GNU General Public License
along with GNOMAD; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA. 

*/

#define ID3V2_MAX_STRING_LEN 4096
#include <id3tag.h>
#include <libmtp.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>


/* Converts a figure representing a number of seconds to
 *  * a string in mm:ss notation */
gchar *seconds_to_mmss(uint32_t seconds)
{
    gchar tmp2[10];
    gchar tmp[10];
    uint32_t secfrac = seconds % 60;
    uint32_t minfrac = seconds / 60;

    if (seconds == 0)
        return g_strdup("0:00");

    snprintf(tmp2, 10, "0%u", secfrac);
    while (strlen(tmp2)>2) {
        tmp2[0]=tmp2[1];
        tmp2[1]=tmp2[2];
        tmp2[2]='\0';
    }
    snprintf(tmp, 10, "%u:%s", minfrac, tmp2);
    return g_strdup(tmp);
}

/* Converts a string in mm:ss notation to a figure
 *  * representing seconds */
guint mmss_to_seconds(gchar *mmss)
{
    gchar **tmp;
    guint seconds = 0;

    if (!mmss)
        return seconds;

    tmp = g_strsplit(mmss, ":", 0);
    if (tmp[1] != NULL) {
        seconds = 60 * strtoul(tmp[0],NULL,10);
        seconds += strtoul(tmp[1],NULL,10);
    }
    if (tmp != NULL)
        g_strfreev(tmp);
    return seconds;
}

/* Eventually make charset selectable */

static id3_utf8_t *
charset_to_utf8 (const id3_latin1_t * str)
{
    id3_utf8_t *tmp;

    tmp =
        (id3_utf8_t *) g_convert ((gchar *) str, -1, "UTF-8", "ISO-8859-1",
                                  NULL, NULL, NULL);
    return (id3_utf8_t *) tmp;
}

/*****************************************************************************
 * ID3TAG interface
 * Many parts of this code copied in from gtkpod or plagiated, I confess.
 * Acknowledgements to Jorg Schuler for that...
 *****************************************************************************/

/*
 * Returns a text frame, or NULL
 */

static gchar *
getFrameText (struct id3_tag *tag, char *frame_name)
{
    const id3_ucs4_t *string;
    struct id3_frame *frame;
    union id3_field *field;
    gchar *utf8 = NULL;
    enum id3_field_textencoding encoding = ID3_FIELD_TEXTENCODING_ISO_8859_1;

    frame = id3_tag_findframe (tag, frame_name, 0);
    if (!frame)
        return NULL;

    /* Find the encoding used for the field */
    field = id3_frame_field (frame, 0);
    //printf ("field: %p\n", field);
    if (field && (id3_field_type (field) == ID3_FIELD_TYPE_TEXTENCODING)) {
        encoding = field->number.value;
        //printf ("encoding: %d\n", encoding);
    }

    if (frame_name == ID3_FRAME_COMMENT)
        field = id3_frame_field (frame, 3);
    else
        field = id3_frame_field (frame, 1);

    //printf ("field: %p\n", field);

    if (!field)
        return NULL;

    if (frame_name == ID3_FRAME_COMMENT)
        string = id3_field_getfullstring (field);
    else
        string = id3_field_getstrings (field, 0);

    // g_debug("string: %s\n", string);

    if (!string)
        return NULL;

    if (frame_name == ID3_FRAME_GENRE)
        string = id3_genre_name (string);

    if (encoding == ID3_FIELD_TEXTENCODING_ISO_8859_1) {
        /* ISO_8859_1 is just a "marker" -- most people just drop
           whatever coding system they are using into it, so we use
           charset_to_utf8() to convert to utf8 */
        id3_latin1_t *raw = id3_ucs4_latin1duplicate (string);
        utf8 = (gchar *) charset_to_utf8 (raw);
        g_free (raw);
    } else {
        /* Standard unicode is being used -- we won't have to worry
           about charsets then. */
        // g_debug("This frame is a Unicode frame!\n");
        utf8 = (gchar *) id3_ucs4_utf8duplicate (string);
    }
    // g_debug("Found tag: %s, value: %s\n", frame_name, utf8);
    return utf8;
}


/*****************************************************************************
 * FUNCTIONS FOR GETTING ID3v2 FIELDS
 *****************************************************************************/

gchar *
getArtist (struct id3_tag *tag)
{
    gchar *artname = NULL;

    artname = getFrameText (tag, "TPE1");
    if (artname == NULL)
        artname = getFrameText (tag, "TPE2");
    if (artname == NULL)
        artname = getFrameText (tag, "TPE3");
    if (artname == NULL)
        artname = getFrameText (tag, "TPE4");
    if (artname == NULL)
        artname = getFrameText (tag, "TCOM");
    return artname;
}

gchar *
getTitle (struct id3_tag *tag)
{
    return getFrameText (tag, "TIT2");
}

gchar *
getAlbum (struct id3_tag *tag)
{
    return getFrameText (tag, "TALB");
}

gchar *
getYear (struct id3_tag *tag)
{
    gchar *year = NULL;

    year = getFrameText (tag, "TYER");
    if (year == NULL) {
        year = getFrameText (tag, "TDRC");
    }
    if (year == NULL) {
        year = g_strdup("None");
    }
    return year;
}

gchar *
getGenre (struct id3_tag *tag)
{
    return getFrameText (tag, "TCON");
}

int
getSonglen (struct id3_tag *tag)
{
    gchar *timetext;
    long milliseconds;
    int seconds;

    timetext = getFrameText (tag, "TLEN");
    if (timetext == NULL)
        return -1;

    // g_debug("Found time tag TLEN: %s ms ... ", timetext);
    milliseconds = atol (timetext);
    g_free (timetext);

    if (milliseconds > 0) {
        seconds = (int) milliseconds / 1000;
        // g_debug("%d milliseconds is %d seconds.\n", milliseconds, seconds);
        return seconds;
    }

    g_debug ("ID3v2 TLEN tag time was 0\n");

    return -1;
}

gchar *
getTracknum (struct id3_tag *tag)
{
    gchar trackno[40];
    gchar *trackstr = getFrameText (tag, "TRCK");
    gchar *posstr = getFrameText (tag, "TPOS");
    gint i;

    if (trackstr == NULL) {
        return NULL;
    }
    trackno[0] = '\0';
    // Take care of any a/b formats
    for (i = 0; i < strlen (trackstr); i++) {
        if (trackstr[i] == '/') {
            // Terminate it at switch character
            trackstr[i] = '\0';
            break;
        }
    }
    // "Part of set" variable
    if (posstr != NULL) {
        // Same a/b format problem again
        for (i = 0; i < strlen (posstr); i++) {
            if (posstr[i] == '/') {
                // Terminate it at switch character
                posstr[i] = '\0';
                break;
            }
        }
        strncpy (trackno, posstr, sizeof (trackno));
        if (strlen (trackstr) == 1) {
            strcat (trackno, "0");
        }
        strncat (trackno, trackstr, sizeof (trackno));
        g_free (trackstr);
        g_free (posstr);
    } else {
        strncpy (trackno, trackstr, sizeof (trackno));
        g_free (trackstr);
    }
    // Terminate and return
    trackno[sizeof (trackno) - 1] = '\0';
    return g_strdup (trackno);
}

gchar *
getOrigFilename (struct id3_tag *tag)
{
    return getFrameText (tag, "TOFN");
}

