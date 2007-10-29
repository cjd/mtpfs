#ifndef ID3HEADER_INCLUDED
#define ID3HEADER_INCLUDED

gchar * getArtist (struct id3_tag *tag);
gchar * getAlbum(struct id3_tag *tag);
gchar * getYear(struct id3_tag *tag);
gchar * getTitle(struct id3_tag *tag);
gchar * getGenre(struct id3_tag *tag);
int getSonglen (struct id3_tag *tag);
gchar * getTracknum (struct id3_tag *tag);
gchar * getOrigFilename (struct id3_tag *tag);

#endif
