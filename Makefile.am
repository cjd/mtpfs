bin_PROGRAMS = mtpfs
mtpfs_SOURCES = mtpfs.c mtpfs.h
mtpfs_CPPFLAGS = -DFUSE_USE_VERSION=22 $(FUSE_CFLAGS) $(GLIB_CFLAGS)
mtpfs_LDADD = $(FUSE_LIBS) $(GLIB_LIBS)

if USEMAD
mtpfs_SOURCES += id3read.c id3read.h
mtpfs_CPPFLAGS += $(MAD_CFLAGS) -DUSEMAD
mtpfs_LDADD += $(MAD_LIBS)
endif
