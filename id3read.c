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


/* Converts a figure representing a number of seconds to
 *  * a string in mm:ss notation */
gchar *seconds_to_mmss(guint seconds)
{
    gchar tmp2[10];
    gchar tmp[10];
    guint secfrac = seconds % 60;
    guint minfrac = seconds / 60;

    if (seconds == 0)
        return g_strdup("0:00");

    snprintf(tmp2, 10, "0%u", secfrac);
    while (strlen(tmp2)>2) {
        tmp2[0]=tmp2[1];
        tmp2[1]=tmp2[2];
        tmp2[2]='\0';
    }
    snprintf(tmp, 10, "%lu:%s", minfrac, tmp2);
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

static id3_latin1_t *
charset_from_utf8 (const id3_utf8_t * str)
{
    id3_latin1_t *tmp;

    tmp =
        (id3_latin1_t *) g_convert ((gchar *) str, -1, "ISO-8859-1", "UTF-8",
                                    NULL, NULL, NULL);
    return (id3_latin1_t *) tmp;
}

/**
 * This function checks if a given file has an ID3 tag 
 * (either V1 or V2, either header and footer) or not.
 *
 * @param path a path to the file to check
 * @return true if the file has an ID3 tag, false otherwise
 */
static gboolean
hasid3tag (gchar * path)
{
    gint fd;
    guchar tag[3];
    gboolean result = FALSE;

#if !GLIB_CHECK_VERSION(2,6,0)
    fd = (gint) open (path, O_RDONLY, 0);
#else
    fd = (gint) g_open (path, O_RDONLY, 0);
#endif
    if (fd >= 0) {
        /* First check for a header */
        if (read (fd, tag, 3) == 3) {
            g_debug ("Checking for header ID3v2 tag...\n");
            if (tag[0] == 'I' && tag[1] == 'D' && tag[2] == '3') {
                result = TRUE;
            } else {
                /* Seek to end, check for footer */
                if (lseek (fd, -10, SEEK_END) > 0) {
                    if (read (fd, tag, 3) == 3) {
                        g_debug ("Checking for footer ID3v2 tag...\n");
                        if (tag[0] == '3' && tag[1] == 'D' && tag[2] == 'I') {
                            result = TRUE;
                        } else {
                            /* Seek to end, and check for ID3v1 tag */
                            if (lseek (fd, -128, SEEK_END) > 0) {
                                if (read (fd, tag, 3) == 3) {
                                    g_debug ("Checking for ID3v1 tag...\n");
                                    if (tag[0] == 'T' &&
                                        tag[1] == 'A' && tag[2] == 'G') {
                                        result = TRUE;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        close (fd);
    }
    g_debug (result ? "%s has ID3 tag.\n" : "%s has no ID3 tag.\n", path);
    return result;
}

/*
 * This function checks if a RIFF header exists at the beginning of 
 * the *data buffer. In that case its size is returned. The size
 * of the *data buffer must be supplied.
 */
static guint
riff_header_size (gchar * data, guint bufsize)
{
    guint retsize = 0;

    // Too small to be a RIFF header
    if (bufsize < 20) {
        return 0;
    }
    // Remove any RIFF header too (EVIL GARBAGE!)
    if (data[0] == 'R' &&
        data[1] == 'I' &&
        data[2] == 'F' &&
        data[3] == 'F' &&
        data[8] == 'W' &&
        data[9] == 'A' && data[10] == 'V' && data[11] == 'E') {
        gchar chunk_type[5];
        guint chunk_size;
        guint p = 12;           // Parse point

        chunk_type[4] = '\0';
        while (p + 8 < bufsize) {
            memcpy (chunk_type, &data[p], 4);
            chunk_size =
                (data[p + 7] << 24) + (data[p + 6] << 16) +
                (data[p + 5] << 8) + data[p + 4];
            // Even 16 bit boundary
            if (chunk_size % 2 != 0) {
                chunk_size++;
            }
            // Pointer behind chunk metadata
            p += 8;
            // g_debug("Chunk \'%s\' size %d bytes\n", chunk_type, chunk_size);
            if (!strcmp (chunk_type, "data")) {
                // The 'data' chunk is the vital one. We need not scan further.
                retsize = p;
                g_debug ("Found a RIFF tag of size %d bytes (%x)\n", retsize,
                         retsize);
                break;
            }
            p += chunk_size;
        }

    }
    return retsize;
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


/*
 * Set the specified frame.
 * Create it, if it doesn't exists, else change it.
 *
 * If value is NULL or the empty string "", the frame will be deleted.
 * Please note that a copy of value is made, so you can safely free()
 * id and value as soon as this function returns.
 */

static void
setFrameText (struct id3_tag *tag,
              const char *frame_name,
              const char *data,
              enum id3_field_textencoding encoding, gboolean override)
{
    int res;
    struct id3_frame *frame;
    union id3_field *field;
    id3_ucs4_t *ucs4;

    if (data == NULL)
        return;

    g_debug ("Updating id3 frame (enc: %d): %s: ", encoding, frame_name,
             data);
    g_debug (" %s\n", data);

    /*
     * An empty string removes the frame altogether.
     */
    if ((strlen (data) == 0) && (!override))
        return;
    if (strlen (data) == 0) {
        // g_debug("removing ID3 frame: %s\n", frame_name);
        while ((frame = id3_tag_findframe (tag, frame_name, 0)))
            id3_tag_detachframe (tag, frame);
        return;
    }

    frame = id3_tag_findframe (tag, frame_name, 0);
    if (!frame) {
        // g_debug("new frame: %s!\n", frame_name);
        frame = id3_frame_new (frame_name);
        id3_tag_attachframe (tag, frame);
    } else if (!override) {
        /* Do not overwrite old tags then */
        return;
    } else {
        // Needed for unicode?
        id3_tag_detachframe (tag, frame);
        frame = id3_frame_new (frame_name);
        id3_tag_attachframe (tag, frame);
    }

    /* Set the encoding - always field 0 */
    field = id3_frame_field (frame, 0);
    id3_field_settextencoding (field, encoding);

    /* Modify the default text field type */
    if (frame_name == ID3_FRAME_COMMENT) {
        /* Get the latin-1 string list, convert it to a Unicode list */
        field = id3_frame_field (frame, 3);
        field->type = ID3_FIELD_TYPE_STRINGFULL;
    } else {
        /* Get the latin-1 string, convert it to a Unicode string */
        field = id3_frame_field (frame, 1);
        field->type = ID3_FIELD_TYPE_STRINGLIST;
    }

    if (frame_name == ID3_FRAME_GENRE) {
        id3_ucs4_t *tmp_ucs4 = id3_utf8_ucs4duplicate ((id3_utf8_t *) data);
        int index = id3_genre_number (tmp_ucs4);

        if (index != -1) {
            /* valid genre -- simply store the genre number */
            gchar *tmp = g_strdup_printf ("%d", index);
            ucs4 = id3_latin1_ucs4duplicate ((id3_latin1_t *) tmp);
            g_free (tmp);
        } else {
            /* oups -- not a valid genre -- save the entire genre string */
            if (encoding == ID3_FIELD_TEXTENCODING_ISO_8859_1) {
                /* we read 'ISO_8859_1' to stand for 'any locale
                   charset' -- most programs seem to work that way */
                id3_latin1_t *raw = charset_from_utf8 ((id3_utf8_t *) data);
                ucs4 = id3_latin1_ucs4duplicate (raw);
                g_free (raw);
            } else {
                /* Yeah! We use unicode encoding and won't have to
                   worry about charsets */
                ucs4 = tmp_ucs4;
                tmp_ucs4 = NULL;
            }
        }
        // Perhaps not good that this will be NULL sometimes...
        g_free (tmp_ucs4);
    } else {
        if (encoding == ID3_FIELD_TEXTENCODING_ISO_8859_1) {
            /* we read 'ISO_8859_1' to stand for 'any locale charset'
               -- most programs seem to work that way */
            id3_latin1_t *raw = charset_from_utf8 ((id3_utf8_t *) data);
            ucs4 = id3_latin1_ucs4duplicate (raw);
            g_free (raw);
        } else {
            /* Yeah! We use unicode encoding and won't have to
               worry about charsets */
            ucs4 = id3_utf8_ucs4duplicate ((id3_utf8_t *) data);
        }
    }

    g_debug ("UCS 4 copy of tag %s:\n", frame_name);

    if (frame_name == ID3_FRAME_COMMENT) {
        res = id3_field_setfullstring (field, ucs4);
    } else {
        res = id3_field_setstrings (field, 1, &ucs4);
    }

    g_free (ucs4);

    if (res != 0)
        g_debug ("Error setting ID3 field: %s\n", frame_name);
}


static enum id3_field_textencoding
get_encoding_of_field (struct id3_tag *tag, const char *frame_name)
{
    struct id3_frame *frame;
    enum id3_field_textencoding encoding = -1;

    frame = id3_tag_findframe (tag, frame_name, 0);
    if (frame) {
        union id3_field *field = id3_frame_field (frame, 0);
        if (field && (id3_field_type (field) == ID3_FIELD_TYPE_TEXTENCODING))
            encoding = field->number.value;
    }
    return encoding;
}

/* Find out which encoding is being used. If in doubt, return
 * latin1. This code assumes that the same encoding is used in all
 * fields.  */
static enum id3_field_textencoding
get_encoding_of_tag (struct id3_tag *tag)
{
    enum id3_field_textencoding enc;

    enc = get_encoding_of_field (tag, ID3_FRAME_TITLE);
    if (enc != -1)
        return enc;
    enc = get_encoding_of_field (tag, ID3_FRAME_ARTIST);
    if (enc != -1)
        return enc;
    enc = get_encoding_of_field (tag, ID3_FRAME_ALBUM);
    if (enc != -1)
        return enc;
    enc = get_encoding_of_field (tag, "TCOM");
    if (enc != -1)
        return enc;
    enc = get_encoding_of_field (tag, ID3_FRAME_COMMENT);
    if (enc != -1)
        return enc;
    enc = get_encoding_of_field (tag, ID3_FRAME_YEAR);
    if (enc != -1)
        return enc;
    return ID3_FIELD_TEXTENCODING_ISO_8859_1;
}

/*****************************************************************************
 * FUNCTIONS FOR SETTING ID3v2 FIELDS
 *****************************************************************************/

static void
setArtist (struct id3_tag *tag, gchar * artist,
           enum id3_field_textencoding encoding, gboolean override)
{
    setFrameText (tag, "TPE1", artist, encoding, override);
}


static void
setTitle (struct id3_tag *tag, gchar * title,
          enum id3_field_textencoding encoding, gboolean override)
{
    setFrameText (tag, "TIT2", title, encoding, override);
}

static void
setAlbum (struct id3_tag *tag, gchar * album,
          enum id3_field_textencoding encoding, gboolean override)
{
    setFrameText (tag, "TALB", album, encoding, override);
}

static void
setYear (struct id3_tag *tag, gchar * year,
         enum id3_field_textencoding encoding, gboolean override)
{
    /* Some confusion here... */
    setFrameText (tag, "TYER", year, encoding, override);
    setFrameText (tag, "TDRC", year, encoding, override);
}

static void
setGenre (struct id3_tag *tag, gchar * genre,
          enum id3_field_textencoding encoding, gboolean override)
{
    setFrameText (tag, "TCON", genre, encoding, override);
}

static void
setSonglen (struct id3_tag *tag, gchar * length,
            enum id3_field_textencoding encoding, gboolean override)
{
    guint seconds;
    gchar *tmp;

    seconds = mmss_to_seconds (length);
    if (seconds > 0) {
        // Convert seconds to milliseconds
        seconds *= 1000;
        // Print the length to a string
        tmp = g_strdup_printf ("%lu", seconds);
        setFrameText (tag, "TLEN", tmp, encoding, override);
        g_free (tmp);
    }
    return;
}


static void
setTracknum (struct id3_tag *tag, guint tracknumin,
             enum id3_field_textencoding encoding, gboolean override)
{
    gchar *tracknum;

    tracknum = g_strdup_printf ("%.2d", tracknumin);
    setFrameText (tag, "TRCK", tracknum, encoding, override);
    g_free (tracknum);
    return;
}

static void
setOrigFilename (struct id3_tag *tag, gchar * filename,
                 enum id3_field_textencoding encoding, gboolean override)
{
    setFrameText (tag, "TOFN", filename, encoding, override);
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

/**
 * Removes a tag from an MP3 file or adds a tag (and removes any old at the same time)
 * @param add whether to add a tag (TRUE) or remove a tag (FALSE)
 * @param path the UTF-8 path to the file, always UTF-8
 * @param v2tag the ID3v2 tag
 * @param v2taglen the length (in bytes) of the ID3v2 tag
 * @param v1tag the ID3v1 tag
 * @param v1taglen the ID3v1 tag length in bytes (128 bytes)
 */
static void
remove_or_add_tag (gboolean add, gchar * path, guchar * v2tag, guint v2taglen,
                   guchar * v1tag, guint v1taglen)
{
    // File descriptors...
    gint f1, f2, f3;
    gchar template[128];
    guint header_taglength = 0;
    guint footer_taglength = 0;
    guint filelength = 0;
    register gchar *buffer;
    guint bufsiz;
    gchar *tmppath;

    strcpy (template, g_get_tmp_dir ());
    template[strlen (template) + 1] = '\0';
    template[strlen (template)] = G_DIR_SEPARATOR;
    strcat (template, "gnomadXXXXXX");

    g_debug ("Removing ID3 tags from/to %s\n", path);

#if GLIB_CHECK_VERSION(2,6,0)
    f1 = (gint) g_open (tmppath, O_RDONLY, 0);
#else
    f1 = (gint) open (tmppath, O_RDONLY, 0);
#endif

    if (f1 < 0) {
        g_debug ("Could not open file f1 in remove_or_add_tag()\n");
        g_free (tmppath);
        return;
    }
    // g_debug("Opened file f1...\n");


    // Allocate a copying buffer
    for (bufsiz = 0x8000; bufsiz >= 128; bufsiz >>= 1) {
        buffer = (gchar *) g_malloc (bufsiz);
        if (buffer)
            break;
    }

    // Temporary file
    f2 = g_mkstemp (template);
    // g_debug("Opened temporary file f2...\n");

    if (f2 >= 0) {
        guchar tag[10];
        gint n;

        // g_debug("Allocated a buffer of %d bytes...\n", bufsiz);

        g_debug ("Looking for ID3v2 header tag\n");

        n = read (f1, tag, 10);
        if (n == 10 && tag[0] == 'I' && tag[1] == 'D' && tag[2] == '3') {
            g_debug ("Found ID3v2 tag header...");
            // Get tag length from the tag - notice that this is an
            // UNSYNCED integer, so the highest bit of each 8-bit group 
            // is unused (always 0).
            header_taglength =
                ((tag[6] & 0x7F) << 21) | ((tag[7] & 0x7F) << 14) |
                ((tag[8] & 0x7F) << 7) | (tag[9] & 0x7F);
            if ((tag[5] & 0x10) == 0) {
                header_taglength += 10;
            } else {
                header_taglength += 20;
            }
            g_debug (" %d (0x%x) bytes\n", header_taglength,
                     header_taglength);

            // Wind past any RIFF header too
            n = read (f1, buffer, bufsiz);
            header_taglength +=
                riff_header_size (&buffer[header_taglength], n);
            g_debug ("ID3v2 header (and any RIFF header) %d (0x%x) bytes\n",
                     header_taglength, header_taglength);
        } else {
            // Any plain RIFF header is removed too.
            gint n;

            n = read (f1, buffer, bufsiz);
            header_taglength = riff_header_size (&buffer[0], n);
        }

        // As we move to the end of the file, detect the filelength
        filelength = lseek (f1, 0, SEEK_END);

        if (filelength > 0) {
            // Then detect the length of any ID3v1 tag
            g_debug ("Detecting ID3v1 tag\n");
            if (lseek (f1, -128, SEEK_END) > 0) {
                guchar tag[3];
                gint n;

                n = read (f1, tag, 3);
                if (n == 3 && tag[0] == 'T' && tag[1] == 'A' && tag[2] == 'G') {
                    g_debug ("Found ID3v1 tag footer, 128 (0x80) bytes...\n");
                    footer_taglength = 128;
                }
            }
            // Then detect the length of any ID3v2 footer tag
            g_debug ("Detecting ID3v2 footer tag\n");
            if (lseek (f1, -10 - footer_taglength, SEEK_END) > 0) {
                guchar tag[10];
                guint footlen;
                gint n;

                n = read (f1, tag, 10);
                if (n == 10 &&
                    tag[0] == '3' && tag[1] == 'D' && tag[2] == 'I') {
                    g_debug ("Found ID3v2 footer tag...");
                    footlen =
                        ((tag[6] & 0x7F) << 21) | ((tag[7] & 0x7F) << 14) |
                        ((tag[8] & 0x7F) << 7) | (tag[9] & 0x7F);
                    // First remove the tag headers
                    footer_taglength += 20;
                    // Then remove the indicated length (no looking for bad tag here)
                    footer_taglength += footlen;
                    g_debug (" %d (0x%x) bytes.\n", footlen, footlen);
                }
            }

            g_debug
                ("Header %d (0x%x) bytes, footer %d (0x%x) bytes to be removed.\n",
                 header_taglength, header_taglength, footer_taglength,
                 footer_taglength);

            if (add) {
                // Write ID3v2 tag at the beginning of the file
                g_debug ("Adding ID3v2 tag to file\n");
                if (v2taglen != write (f2, v2tag, v2taglen)) {
                    g_debug
                        ("Error writing ID3v2 header tag in remove_or_add_tag()\n");
                }
            }
            // Next skip past the header, and copy until we reach the footer
            g_debug ("Copying original file, ");
            if (lseek (f1, header_taglength, SEEK_SET) >= 0) {
                guint remain =
                    filelength - header_taglength - footer_taglength;

                g_debug ("%d bytes.\n", remain);
                while (remain > 0) {
                    register gint n;

                    if (remain > bufsiz) {
                        n = read (f1, buffer, bufsiz);
                    } else {
                        n = read (f1, buffer, remain);
                    }
                    if (n == -1) {
                        g_debug
                            ("Error reading source file during file copying in remove_or_add_tag()\n");
                        break;
                    }
                    if (n == 0) {
                        break;
                    }
                    if (n != write (f2, buffer, n)) {
                        g_debug
                            ("Error writing target file during file copying in remove_or_add_tag()\n");
                        break;
                    }
                    remain -= n;
                }
            }
        }
        close (f1);

        if (add) {
            // Write ID3v1 tag at the end of the file
            g_debug ("Adding ID3v1 tag to file\n");
            if (v1taglen != write (f2, v1tag, v1taglen)) {
                g_debug
                    ("Error writing ID3v1 footer tag in remove_or_add_tag()\n");
            }
        }
        // Then copy back the stripped file
        // Rewind the temporary file
        lseek (f2, 0, SEEK_SET);
        g_debug ("Copying the file back...\n");
#if GLIB_CHECK_VERSION(2,8,0)
        f3 = (gint) g_creat (tmppath,
                             (mode_t) (S_IRUSR | S_IWUSR | S_IRGRP |
                                       S_IROTH));
#else
        f3 = (gint) creat (tmppath,
                           (mode_t) (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
#endif
        if (f3) {
            // g_debug("Creat() on original file succeeded...\n");
            while (1) {
                register gint n;

                n = read (f2, buffer, bufsiz);
                if (n == -1) {
                    g_debug
                        ("Error reading source file during final copying in remove_or_add_tag()\n");
                    break;
                }
                if (n == 0) {
                    break;
                }
                if (n != write (f3, buffer, n)) {
                    g_debug
                        ("Error writing target file during final copying in remove_or_add_tag()\n");
                    break;
                }
            }
            close (f3);
        } else {
            g_debug
                ("Erroneous file descriptor when copying file back in remove_or_add_tag()\n");
        }
        close (f2);
        // g_debug("Deleting %s\n", template);
#if GLIB_CHECK_VERSION(2,6,0)
        g_unlink (template);
#else
        unlink (template);
#endif
    } else {
        // In case we couldn't open f2
        close (f1);
    }
    g_free (tmppath);
    g_free (buffer);
}



/*****************************************************************************
 * EXPORTED FUNCTIONS
 *****************************************************************************/


void
remove_tag_from_mp3file (gchar * path)
{
    remove_or_add_tag (FALSE, path, NULL, 0, NULL, 0);
}


void
set_tag_for_mp3file (int fd, const LIBMTP_track_t *trackdata, int override)
{
    struct id3_file *fh;
    struct id3_tag *tag;
    gchar *tmppath;
    enum id3_field_textencoding encoding;
    id3_length_t tagv2len;
    id3_length_t tagv1len;
    id3_byte_t *tagv2;
    id3_byte_t *tagv1;

    g_debug ("Setting tag info for %s...\n", tmppath);

    /* Get the tag for the old file */
    fh = id3_file_fdopen (fd, ID3_FILE_MODE_READONLY);

    if (!fh) {
        g_debug ("Could not open file %s!\n", trackdata->filename);
        return;
    }
    tag = id3_file_tag (fh);
    if (!tag) {
        g_debug ("Could not get tag for file %s!\n", trackdata->filename);
        return;
    }

    /* use the same encoding as before... */
    encoding = get_encoding_of_tag (tag);

    /* Close old file. */
    // id3_file_close(fh);

    if (trackdata->artist != NULL && strcmp (trackdata->artist, "<Unknown>")) {
        setArtist (tag, trackdata->artist, encoding, override);
    }
    if (trackdata->title != NULL && strcmp (trackdata->title, "<Unknown>")) {
        setTitle (tag, trackdata->title, encoding, override);
    }
    if (trackdata->album != NULL && strcmp (trackdata->album, "<Unknown>")) {
        setAlbum (tag, trackdata->album, encoding, override);
    }
    if (trackdata->date != NULL && strcmp(trackdata->date, "<Unknown>")) {
        setYear (tag, trackdata->date, encoding, override);
    }
    if (trackdata->genre != NULL && strcmp (trackdata->genre, "<Unknown>")) {
        setGenre (tag, trackdata->genre, encoding, override);
    }
    if (trackdata->duration != 0) {
        gchar *tmp;
        tmp = seconds_to_mmss(trackdata->duration/1000);
        setSonglen (tag, tmp, encoding, override);
        g_free(tmp);
    }
    setTracknum (tag, trackdata->tracknumber, encoding, override);
    if (trackdata->filename != NULL &&
        strlen (trackdata->filename) && strcmp (trackdata->filename, "0")) {
        setOrigFilename (tag, trackdata->filename, encoding, override);
    }

    /* Render tag so we can look at it */
    tagv2 = g_malloc (64738);
    tagv1 = g_malloc (128);

    /* Render ID3v2 tag */
    id3_tag_options (tag, ID3_TAG_OPTION_ID3V1, 0);
    tagv2len = id3_tag_render (tag, tagv2);
    g_debug ("Rendered ID3v2 tag, length %d (0x%x) bytes:\n", tagv2len,
             tagv2len);

    /* Render ID3v1 tag */
    id3_tag_options (tag, ID3_TAG_OPTION_ID3V1, ~0);
    tagv1len = id3_tag_render (tag, tagv1);
    g_debug ("Rendered ID3v1 tag 128 (0x80) bytes:\n");

    /* Close old file */
    id3_file_close (fh);

    /* Totally revamp file */
    remove_or_add_tag (TRUE, trackdata->filename, tagv2, tagv2len, tagv1, tagv1len);

    g_free (tagv2);
    g_free (tagv1);
    g_free (tmppath);
}
