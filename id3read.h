#ifndef ID3HEADER_INCLUDED
#define ID3HEADER_INCLUDED

void remove_tag_from_mp3file(gchar *path);
void set_tag_for_mp3file  (int fd, const LIBMTP_track_t *trackdata, int override);
gchar * getArtist (struct id3_tag *tag);
gchar * getAlbum(struct id3_tag *tag);
gchar * getYear(struct id3_tag *tag);
gchar * getTitle(struct id3_tag *tag);
gchar * getGenre(struct id3_tag *tag);
int getSonglen (struct id3_tag *tag);
gchar * getTracknum (struct id3_tag *tag);
gchar * getOrigFilename (struct id3_tag *tag);

#endif
