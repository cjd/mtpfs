#ifndef _MTPFS_H_
#define _MTPFS_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statfs.h>

#include <libmtp.h>
#include <glib.h>
#include <sys/mman.h>
#include <strings.h>
#ifdef USEMAD
#include <mad.h>
#include <id3tag.h>
#include "id3read.h"
#endif

typedef struct
{
  LIBMTP_devicestorage_t *storage;
  LIBMTP_folder_t *folders;
  gboolean folders_changed;
} StorageArea;

/* Function declarations */

/* local functions */
static LIBMTP_filetype_t find_filetype (const gchar * filename);
static int lookup_folder_id (LIBMTP_folder_t * folderlist, gchar * path,
			     gchar * parent);
static int parse_path (const gchar * path);
static void check_lost_files ();
void check_folders ();
static int find_storage (const gchar * path);

    /* fuse functions */
static void *mtpfs_init (void);
static int mtpfs_blank ();
static int mtpfs_release (const char *path, struct fuse_file_info *fi);
void mtpfs_destroy ();
static int mtpfs_readdir (const gchar * path, void *buf,
			  fuse_fill_dir_t filler, off_t offset,
			  struct fuse_file_info *fi);
static int mtpfs_getattr (const gchar * path, struct stat *stbuf);
static int mtpfs_mknod (const gchar * path, mode_t mode, dev_t dev);
static int mtpfs_open (const gchar * path, struct fuse_file_info *fi);
static int mtpfs_read (const gchar * path, gchar * buf, size_t size,
		       off_t offset, struct fuse_file_info *fi);
static int mtpfs_write (const gchar * path, const gchar * buf, size_t size,
			off_t offset, struct fuse_file_info *fi);
static int mtpfs_unlink (const gchar * path);
static int mtpfs_mkdir (const char *path, mode_t mode);
static int mtpfs_rmdir (const char *path);
static int mtpfs_statfs (const char *path, struct statfs *stbuf);
int calc_length (int f);

static LIBMTP_mtpdevice_t *device;
static StorageArea storageArea[4];
static LIBMTP_file_t *files = NULL;
static gboolean files_changed = TRUE;
static GSList *lostfiles = NULL;
static GSList *myfiles = NULL;
static LIBMTP_playlist_t *playlists = NULL;
static gboolean playlists_changed = FALSE;
static GMutex device_lock;

#endif /* _MTPFS_H_ */
