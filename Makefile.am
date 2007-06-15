bin_PROGRAMS = mtpfs
mtpfs_SOURCES = id3read.c mtpfs.c mtpfs.h id3read.h
mtpfs_CPPFLAGS = -DFUSE_USE_VERSION=22 $(FUSE_CFLAGS)
mtpfs_LDADD = $(FUSE_LIBS)
