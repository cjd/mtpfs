/*
    FUSE: Filesystem in Userspace
    Copyright (C) 2001-2005  Miklos Szeredi <miklos@szeredi.hu>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#include <mtpfs.h>

#if DEBUG
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define DBG(a...) {g_printf( "[" __FILE__ ":" TOSTRING(__LINE__) "] " a );g_printf("\n");}
#else
#define DBG(a...)
#endif

#if DEBUG
static void dump_mtp_error()
{
	LIBMTP_Dump_Errorstack(device);
	LIBMTP_Clear_Errorstack(device);
}
#else
#define dump_mtp_error()
#endif

#define enter_lock(a...)       do { DBG("lock"); DBG(a); g_static_mutex_lock(&device_lock); } while(0)
#define return_unlock(a)       do { DBG("return unlock"); g_static_mutex_unlock(&device_lock); return a; } while(0)

void
free_files(LIBMTP_file_t *filelist)
{
    LIBMTP_file_t *file = filelist, *tmp;
    while (file) {
        tmp = file;
        file = file->next;
        LIBMTP_destroy_file_t(tmp);
    }
}

void
free_playlists(LIBMTP_playlist_t *pl)
{
    LIBMTP_playlist_t *playlist = pl, *tmp;
    while (playlist) {
        tmp = playlist;
        playlist = playlist->next;
        LIBMTP_destroy_playlist_t(tmp);
    }
}

void
check_files ()
{
    if (files_changed) {
        DBG("Refreshing Filelist");
        LIBMTP_file_t *newfiles = NULL;
        if (files) free_files(files);
        newfiles = LIBMTP_Get_Filelisting_With_Callback(device, NULL, NULL);
        files = newfiles;
        newfiles = NULL;
        files_changed = FALSE;
        //check_lost_files ();
        DBG("Refreshing Filelist exiting");
    }
}

static void
check_lost_files ()
{
	int last_parent_id = -1;
	gboolean last_parent_found = FALSE;
	LIBMTP_file_t *item;

	if (lostfiles != NULL)
		g_slist_free (lostfiles);

	lostfiles = NULL;
	for (item = files; item != NULL; item = item->next) {
		gboolean parent_found;

		if (last_parent_id == -1 || last_parent_id != item->parent_id) {
			if (item->parent_id == 0) {
				parent_found = TRUE;
			} else {
                int i;
                for (i=0;i<4;i++) {
                    if (storageArea[i].folders!=NULL) {
                        if (LIBMTP_Find_Folder (storageArea[i].folders, item->parent_id) != NULL) {
				            parent_found = FALSE;
                        }
                    }
                }
			}
			last_parent_id = item->parent_id;
			last_parent_found = parent_found;
		} else {
			parent_found = last_parent_found;
		}
		DBG("MTPFS checking for lost files %s, parent %d - %s", item->filename, last_parent_id, ( parent_found ? "FALSE" : "TRUE" ) );
		if (parent_found == FALSE) {
			lostfiles = g_slist_append (lostfiles, item);
		}
	}
	DBG("MTPFS checking for lost files exit found %d lost tracks", g_slist_length (lostfiles) );
}

void
check_folders ()
{
    int i;
    for (i=0;i<4;i++) {
        if (storageArea[i].folders_changed) {
            DBG("Refreshing Folderlist %d-%s", i,storageArea[i].storage->StorageDescription);
            LIBMTP_folder_t *newfolders = NULL;
            if (storageArea[i].folders) {
                LIBMTP_destroy_folder_t(storageArea[i].folders);
            }
            newfolders = LIBMTP_Get_Folder_List_For_Storage(device,storageArea[i].storage->id);
            storageArea[i].folders = newfolders;
            newfolders = NULL;
            storageArea[i].folders_changed= FALSE;
        }
    }
}

void
check_playlists ()
{
    if (playlists_changed) {
        DBG("Refreshing Playlists");
        LIBMTP_playlist_t *newplaylists;
        if (playlists) free_playlists(playlists);
        newplaylists = LIBMTP_Get_Playlist_List(device);
        playlists = newplaylists;
        playlists_changed = FALSE;
    }
}

int
save_playlist (const char *path, struct fuse_file_info *fi)
{
    DBG("save_playlist");
    int ret=0;

    LIBMTP_playlist_t *playlist;
    FILE *file = NULL;
    char item_path[1024];
    uint32_t item_id=0;
    uint32_t *tracks;
    gchar **fields;
    GSList *tmplist=NULL;

    fields = g_strsplit(path,"/",-1);
    gchar *playlist_name;
    playlist_name = g_strndup(fields[2],strlen(fields[2])-4);
    DBG("Adding:%s",playlist_name);
    g_strfreev(fields);

    playlist=LIBMTP_new_playlist_t();
    playlist->name=g_strdup(playlist_name);

    file = fdopen(fi->fh,"r");
    while (fgets(item_path,sizeof(item_path)-1,file) != NULL){
        g_strchomp(item_path);
        item_id = parse_path(item_path);
        if (item_id != -1) {
            tmplist = g_slist_append(tmplist,GUINT_TO_POINTER(item_id));
            DBG("Adding to tmplist:%d",item_id);
        }
    }
    playlist->no_tracks = g_slist_length(tmplist);
    tracks = g_malloc(playlist->no_tracks * sizeof(uint32_t));
    int i;
    for (i = 0; i < playlist->no_tracks; i++) {
            tracks[i]=(uint32_t)GPOINTER_TO_UINT(g_slist_nth_data(tmplist,i));
            DBG("Adding:%d-%d",i,tracks[i]);
    }
    playlist->tracks = tracks;
    DBG("Total:%d",playlist->no_tracks);
    
    int playlist_id = 0;
    LIBMTP_playlist_t *tmp_playlist;
    check_playlists();
    tmp_playlist=playlists;
    while (tmp_playlist != NULL){
        if (g_ascii_strcasecmp(tmp_playlist->name,playlist_name) == 0){
            playlist_id=playlist->playlist_id;
        }
        tmp_playlist=tmp_playlist->next;
    }

    if (playlist_id > 0) {
        DBG("Update playlist %d",playlist_id);
        playlist->playlist_id=playlist_id;
        ret = LIBMTP_Update_Playlist(device,playlist);
    } else {
        DBG("New playlist");
        ret = LIBMTP_Create_New_Playlist(device,playlist);
    }
    playlists_changed=TRUE;
    return ret;
}

/* Find the file type based on extension */
static LIBMTP_filetype_t
find_filetype (const gchar * filename)
{
    DBG("find_filetype");
    gchar **fields;
    fields = g_strsplit (filename, ".", -1);
    gchar *ptype;
    ptype = g_strdup (fields[g_strv_length (fields) - 1]);
    g_strfreev (fields);
    LIBMTP_filetype_t filetype;
    
    // This need to be kept constantly updated as new file types arrive.
    if (!g_ascii_strncasecmp (ptype, "wav",3)) {
        filetype = LIBMTP_FILETYPE_WAV;
    } else if (!g_ascii_strncasecmp (ptype, "mp3",3)) {
        filetype = LIBMTP_FILETYPE_MP3;
    } else if (!g_ascii_strncasecmp (ptype, "wma",3)) {
        filetype = LIBMTP_FILETYPE_WMA;
    } else if (!g_ascii_strncasecmp (ptype, "ogg",3)) {
        filetype = LIBMTP_FILETYPE_OGG;
    } else if (!g_ascii_strncasecmp (ptype, "aa",2)) {
        filetype = LIBMTP_FILETYPE_AUDIBLE;
    } else if (!g_ascii_strncasecmp (ptype, "mp4",3)) {
        filetype = LIBMTP_FILETYPE_MP4;
    } else if (!g_ascii_strncasecmp (ptype, "wmv",3)) {
        filetype = LIBMTP_FILETYPE_WMV;
    } else if (!g_ascii_strncasecmp (ptype, "avi",3)) {
        filetype = LIBMTP_FILETYPE_AVI;
    } else if (!g_ascii_strncasecmp (ptype, "mpeg",4) || !g_ascii_strncasecmp (ptype, "mpg",3)) {
        filetype = LIBMTP_FILETYPE_MPEG;
    } else if (!g_ascii_strncasecmp (ptype, "asf",3)) {
        filetype = LIBMTP_FILETYPE_ASF;
    } else if (!g_ascii_strncasecmp (ptype, "qt",2) || !g_ascii_strncasecmp (ptype, "mov",3)) {
        filetype = LIBMTP_FILETYPE_QT;
    } else if (!g_ascii_strncasecmp (ptype, "wma",3)) {
        filetype = LIBMTP_FILETYPE_WMA;
    } else if (!g_ascii_strncasecmp (ptype, "jpg",3) || !g_ascii_strncasecmp (ptype, "jpeg",4)) {
        filetype = LIBMTP_FILETYPE_JPEG;
    } else if (!g_ascii_strncasecmp (ptype, "jfif",4)) {
        filetype = LIBMTP_FILETYPE_JFIF;
    } else if (!g_ascii_strncasecmp (ptype, "tif",3) || !g_ascii_strncasecmp (ptype, "tiff",4)) {
        filetype = LIBMTP_FILETYPE_TIFF;
    } else if (!g_ascii_strncasecmp (ptype, "bmp",3)) {
        filetype = LIBMTP_FILETYPE_BMP;
    } else if (!g_ascii_strncasecmp (ptype, "gif",3)) {
        filetype = LIBMTP_FILETYPE_GIF;
    } else if (!g_ascii_strncasecmp (ptype, "pic",3) || !g_ascii_strncasecmp (ptype, "pict",4)) {
        filetype = LIBMTP_FILETYPE_PICT;
    } else if (!g_ascii_strncasecmp (ptype, "png",3)) {
        filetype = LIBMTP_FILETYPE_PNG;
    } else if (!g_ascii_strncasecmp (ptype, "wmf",3)) {
        filetype = LIBMTP_FILETYPE_WINDOWSIMAGEFORMAT;
    } else if (!g_ascii_strncasecmp (ptype, "ics",3)) {
        filetype = LIBMTP_FILETYPE_VCALENDAR2;
    } else if (!g_ascii_strncasecmp (ptype, "exe",3) || !g_ascii_strncasecmp (ptype, "com",3) ||
               !g_ascii_strncasecmp (ptype, "bat",3) || !g_ascii_strncasecmp (ptype, "dll",3) ||
               !g_ascii_strncasecmp (ptype, "sys",3)) {
        filetype = LIBMTP_FILETYPE_WINEXEC;
    } else if (!g_ascii_strncasecmp (ptype, "txt",3)) {
        filetype = LIBMTP_FILETYPE_TEXT;
    } else if (!g_ascii_strncasecmp (ptype, "htm",3) || !g_ascii_strncasecmp (ptype, "html",4) ) {
        filetype = LIBMTP_FILETYPE_HTML;
    } else if (!g_ascii_strncasecmp (ptype, "bin",3)) {
        filetype = LIBMTP_FILETYPE_FIRMWARE;
    } else if (!g_ascii_strncasecmp (ptype, "aac",3)) {
        filetype = LIBMTP_FILETYPE_AAC;
    } else if (!g_ascii_strncasecmp (ptype, "flac",4) || !g_ascii_strncasecmp (ptype, "fla",3)) {
        filetype = LIBMTP_FILETYPE_FLAC;
    } else if (!g_ascii_strncasecmp (ptype, "mp2",3)) {
        filetype = LIBMTP_FILETYPE_MP2;
    } else if (!g_ascii_strncasecmp (ptype, "m4a",3)) {
        filetype = LIBMTP_FILETYPE_M4A;
    } else if (!g_ascii_strncasecmp (ptype, "doc",3)) {
        filetype = LIBMTP_FILETYPE_DOC;
    } else if (!g_ascii_strncasecmp (ptype, "xml",3)) {
        filetype = LIBMTP_FILETYPE_XML;
    } else if (!g_ascii_strncasecmp (ptype, "xls",3)) {
        filetype = LIBMTP_FILETYPE_XLS;
    } else if (!g_ascii_strncasecmp (ptype, "ppt",3)) {
        filetype = LIBMTP_FILETYPE_PPT;
    } else if (!g_ascii_strncasecmp (ptype, "mht",3)) {
        filetype = LIBMTP_FILETYPE_MHT;
    } else if (!g_ascii_strncasecmp (ptype, "jp2",3)) {
        filetype = LIBMTP_FILETYPE_JP2;
    } else if (!g_ascii_strncasecmp (ptype, "jpx",3)) {
        filetype = LIBMTP_FILETYPE_JPX;
    } else {
        DBG("Sorry, file type \"%s\" is not yet supported", ptype);
        DBG("Tagging as unknown file type.");
        filetype = LIBMTP_FILETYPE_UNKNOWN;
    }
	g_free (ptype);
    return filetype;
}

static int
find_storage(const gchar * path)
{
    int i;
    DBG("find_storage:%s",path);
    for (i=0;i<4;i++) {
        if (storageArea[i].storage != NULL) {
            int maxlen = strlen(storageArea[i].storage->StorageDescription);
            if (strlen(path+1) < maxlen) maxlen = strlen(path+1);
            if (strncmp(storageArea[i].storage->StorageDescription,path+1,maxlen)==0) {
                DBG("%s found as %d",storageArea[i].storage->StorageDescription,i);
                return i;
            }
        }
    }
    return -1;
}

static int
lookup_folder_id (LIBMTP_folder_t * folderlist, gchar * path, gchar * parent)
{
    DBG("lookup_folder_id %s,%s",path, parent);
    int ret = -1;
    if (folderlist == NULL) {
        return -1;
    }
    gchar * mypath;
    mypath=path;
    check_folders();
    if (parent==NULL) {
        if (g_strrstr(path+1,"/") == NULL) {
            DBG("Storage dir");
            return -2;
        } else {
            DBG("Strip storage area name"); 
            mypath=strstr(path+1,"/");
            parent="";
        }
    }

    gchar *current;
    current = g_strconcat(parent, "/", folderlist->name,NULL);
    LIBMTP_devicestorage_t *storage;

    DBG("compare %s,%s",mypath, current);
    if (g_ascii_strcasecmp (mypath, current) == 0) {
        ret = folderlist->folder_id;
    } else if (g_ascii_strncasecmp (mypath, current, strlen (current)) == 0) {
        ret = lookup_folder_id (folderlist->child, mypath, current);
    }

    if (ret == -1) {
		ret = lookup_folder_id (folderlist->sibling, mypath, parent);
	}
    g_free(current);
    return ret;
}

static int
parse_path (const gchar * path)
{
    DBG("parse_path:%s",path);
    int res;
    int item_id = -1;
    int i;
    // Check cached files first
    GSList *item;
    item = g_slist_find_custom (myfiles, path, (GCompareFunc) strcmp);
    if (item != NULL)
        return 0;

    // Check Playlists
    if (strncmp("/Playlists",path,10) == 0) {
        LIBMTP_playlist_t *playlist;

		res = -ENOENT;
        check_playlists();
        playlist=playlists;
        while (playlist != NULL) {
            gchar *tmppath;
            tmppath = g_strconcat("/Playlists/",playlist->name,".m3u",NULL);
            if (g_ascii_strcasecmp(path,tmppath) == 0) {
                res = playlist->playlist_id;
				g_free (tmppath);
				break;
			}
			g_free (tmppath);
            playlist = playlist->next;
        }
        return res;
    }

    // Check lost+found
    if (strncmp("/lost+found", path, 11) == 0) {
		GSList *item;
		gchar *filename  = g_path_get_basename (path);

		res = -ENOENT;
		for (item = lostfiles; item != NULL; item = g_slist_next (item) ) {
			LIBMTP_file_t *file = (LIBMTP_file_t *) item->data;

			if (strcmp( file->filename, filename) == 0) {
				res = file->item_id;
				break;
			}
		}
		g_free (filename);
		return res;
    }

    // Check device
    LIBMTP_folder_t *folder;
    gchar **fields;
    gchar *directory;
    directory = (gchar *) g_malloc (strlen (path));
    directory = strcpy (directory, "");
    fields = g_strsplit (path, "/", -1);
    res = -ENOENT;
    int storageid;
    storageid = find_storage(path);
    for (i = 0; fields[i] != NULL; i++) {
        if (strlen (fields[i]) > 0) {
            if (fields[i + 1] != NULL) {
                directory = strcat (directory, "/");
                directory = strcat (directory, fields[i]);
            } else {
                check_folders();
                folder = storageArea[storageid].folders;
                int folder_id = 0;
                if (strcmp (directory, "") != 0) {
                    folder_id = lookup_folder_id (folder, directory, NULL);
                }
                DBG("parent id:%d:%s", folder_id, directory);
                LIBMTP_file_t *file;
                check_files();
                file = files;
                while (file != NULL) {
			        if ((file->parent_id == folder_id) ||
                       (folder_id==-2 && (file->parent_id == 0) && (file->storage_id == storageArea[storageid].storage->id))) {
				        if (file->filename == NULL) DBG("MTPFS filename NULL");
				        if (file->filename != NULL && g_ascii_strcasecmp (file->filename, fields[i]) == 0) {
					    DBG("found:%d:%s", file->item_id, file->filename);

					    item_id = file->item_id;
					    break; // found!
				    }
			    }
                file = file->next;
            }
            if (item_id < 0) {
                directory = strcat (directory, "/");
                directory = strcat (directory, fields[i]);
                item_id = lookup_folder_id (folder, directory, NULL);
                res = item_id;
				break;
            } else {
                res = item_id;
				break;
			}
            }
        }
    }
	g_free (directory);
    g_strfreev (fields);
    DBG("parse_path exiting:%s - %d",path,res);
    return res;
}

static int
mtpfs_release (const char *path, struct fuse_file_info *fi)
{
    enter_lock("release: %s", path);
    // Check cached files first
    GSList *item;
    item = g_slist_find_custom (myfiles, path, (GCompareFunc) strcmp);

    if (item != NULL) {
        if (strncmp("/Playlists/",path,11) == 0) {
            save_playlist(path,fi);
            close (fi->fh);
            return_unlock(0);
        } else {
            //find parent id
            gchar *filename = g_strdup("");
            gchar **fields;
            gchar *directory;
            directory = (gchar *) g_malloc (strlen (path));
            directory = strcpy (directory, "/");
            fields = g_strsplit (path, "/", -1);
            int i;
            int parent_id = 0;
            int storageid;
            storageid = find_storage(fields[0]);
            for (i = 0; fields[i] != NULL; i++) {
                if (strlen (fields[i]) > 0) {
                    if (fields[i + 1] == NULL) {
                        gchar *tmp = g_strndup (directory, strlen (directory) - 1);
                        parent_id = lookup_folder_id (storageArea[storageid].folders, tmp, NULL);
						g_free (tmp);
                        if (parent_id < 0)
                            parent_id = 0;
						g_free (filename);
                        filename = g_strdup (fields[i]);
                    } else {
                        directory = strcat (directory, fields[i]);
                        directory = strcat (directory, "/");
                    }
                }
            }
            DBG("%s:%s:%d", filename, directory, parent_id);
    
            struct stat st;
            uint64_t filesize;
            fstat (fi->fh, &st);
            filesize = (uint64_t) st.st_size;
    
            // Setup file
            int ret;
            LIBMTP_filetype_t filetype;
            filetype = find_filetype (filename);
            #ifdef USEMAD
            if (filetype == LIBMTP_FILETYPE_MP3) {
                LIBMTP_track_t *genfile;
                genfile = LIBMTP_new_track_t ();
                gint songlen;
                struct id3_file *id3_fh;
                struct id3_tag *tag;
                gchar *tracknum;


                id3_fh = id3_file_fdopen (fi->fh, ID3_FILE_MODE_READONLY);
                tag = id3_file_tag (id3_fh);

                genfile->artist = getArtist (tag);
                genfile->title = getTitle (tag);
                genfile->album = getAlbum (tag);
                genfile->genre = getGenre (tag);
                genfile->date = getYear (tag);
                genfile->usecount = 0;
                genfile->parent_id = (uint32_t) parent_id;
                genfile->storage_id = storageArea[storageid].storage->id;

                /* If there is a songlength tag it will take
                 * precedence over any length calculated from
                 * the bitrate and filesize */
                songlen = getSonglen (tag);
                if (songlen > 0) {
                    genfile->duration = songlen * 1000;
                } else {
                    genfile->duration = (uint16_t)calc_length(fi->fh) * 1000;
                    //genfile->duration = 293000;
                }

                tracknum = getTracknum (tag);
                if (tracknum != NULL) {
                    genfile->tracknumber = strtoul(tracknum,NULL,10);
                } else {
                    genfile->tracknumber = 0;
                }
                g_free (tracknum);

                // Compensate for missing tag information
                if (!genfile->artist)
                    genfile->artist = g_strdup("<Unknown>");
                if (!genfile->title)
                    genfile->title = g_strdup("<Unknown>");
                if (!genfile->album)
                    genfile->album = g_strdup("<Unknown>");
                if (!genfile->genre)
                    genfile->genre = g_strdup("<Unknown>");

                genfile->filesize = filesize;
                genfile->filetype = filetype;
                genfile->filename = g_strdup (filename);
                //title,artist,genre,album,date,tracknumber,duration,samplerate,nochannels,wavecodec,bitrate,bitratetype,rating,usecount
                //DBG("%d:%d:%d",fi->fh,genfile->duration,genfile->filesize);
                ret =
                    LIBMTP_Send_Track_From_File_Descriptor (device, fi->fh,
						genfile, NULL, NULL);
                id3_file_close (id3_fh);
                LIBMTP_destroy_track_t (genfile);
                DBG("Sent TRACK %s",path);
            } else {
            #endif
                LIBMTP_file_t *genfile;
                genfile = LIBMTP_new_file_t ();
                genfile->filesize = filesize;
                genfile->filetype = filetype;
                genfile->filename = g_strdup (filename);
                genfile->parent_id = (uint32_t) parent_id;
                genfile->storage_id = storageArea[storageid].storage->id;
    
                ret =
                    LIBMTP_Send_File_From_File_Descriptor (device, fi->fh,
						genfile, NULL, NULL);
                LIBMTP_destroy_file_t (genfile);
                DBG("Sent FILE %s",path);
            #ifdef USEMAD
            }
            #endif
            if (ret == 0) {
                DBG("Sent %s",path);
            } else {
                DBG("Problem sending %s - %d",path,ret);
            }
            // Cleanup
			if (item && item->data)
				g_free (item->data);
            myfiles = g_slist_remove (myfiles, item->data);
            g_strfreev (fields);
			g_free (filename);
			g_free (directory);
            close (fi->fh);
            // Refresh filelist
            files_changed = TRUE;
            return_unlock(ret);
        }
    }
    close (fi->fh);
    return_unlock(0);
}

void
mtpfs_destroy ()
{
    enter_lock("destroy");
    if (files) free_files(files);
    int i;
    for (i=0;i<4;i++) {
        if (storageArea[i].folders) LIBMTP_destroy_folder_t(storageArea[i].folders);
    }
    if (playlists) free_playlists(playlists);
    if (device) LIBMTP_Release_Device (device);
    return_unlock();
}

static int
mtpfs_readdir (const gchar * path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi)
{
	enter_lock("readdir %s", path);
    LIBMTP_folder_t *folder;

    // Add common entries
    filler (buf, ".", NULL, 0);
    filler (buf, "..", NULL, 0);

    // If in root directory
    if (strcmp(path,"/") == 0) {
        filler (buf, "Playlists", NULL, 0);
		if (lostfiles != NULL) {
			filler (buf, "lost+found", NULL, 0);
		}
        LIBMTP_devicestorage_t *storage;
        for (storage = device->storage; storage != 0; storage = storage->next) {
            struct stat st;
            memset (&st, 0, sizeof (st));
            st.st_nlink = 2;
            st.st_ino = storage->id;
            st.st_mode = S_IFREG | 0555;
            gchar *name;
            filler (buf, storage->StorageDescription, &st, 0);
        }
		return_unlock(0);
    }

    // Are we looking at the playlists?
    if (strncmp (path, "/Playlists",10) == 0) {
        DBG("Checking Playlists");
        LIBMTP_playlist_t *playlist;
        check_playlists();
        playlist=playlists;
        while (playlist!= NULL) {
            struct stat st;
            memset (&st, 0, sizeof (st));
            st.st_ino = playlist->playlist_id;
            st.st_mode = S_IFREG | 0666;
            gchar *name;
            name = g_strconcat(playlist->name,".m3u",NULL);
            DBG("Playlist:%s",name);
            if (filler (buf, name, &st, 0))
            {
                g_free (name);
                break;
            }
            g_free (name);
            playlist=playlist->next;
        }
        return_unlock(0);
    }

    // Are we looking at lost+found dir?
    if (strncmp (path, "/lost+found",11) == 0) {
		check_files ();
		GSList *item;

		for (item = lostfiles; item != NULL; item = g_slist_next (item) ) {
			LIBMTP_file_t *file = (LIBMTP_file_t *) item->data;

			struct stat st;
			memset (&st, 0, sizeof (st));
			st.st_ino = file->item_id;
			st.st_mode = S_IFREG | 0444;
			if (filler (buf, (file->filename == NULL ? "<mtpfs null>" : file->filename), &st, 0))
				break;
		}
		return_unlock(0);
    }

    // Get storage area
    int i;
    int storageid = -1;
    storageid=find_storage(path);
    // Get folder listing.
    int folder_id = 0;
    if (strcmp (path, "/") != 0) {
        check_folders();
        folder_id = lookup_folder_id (storageArea[storageid].folders, (gchar *) path, NULL);
    }

    DBG("Checking folders for %d",storageid);
    check_folders();
    if (folder_id==-2) {
        DBG("Root of storage area");
        folder=storageArea[storageid].folders;
    } else {
        folder = LIBMTP_Find_Folder (storageArea[storageid].folders, folder_id);
        if (folder == NULL) return_unlock(0);
        folder = folder->child;
    }
    if (folder == NULL) return_unlock(0);

    while (folder != NULL) {
        if ((folder->parent_id == folder_id) ||
           (folder_id==-2 && (folder->storage_id == storageArea[storageid].storage->id))) {
			DBG("found folder: %s, id %d", folder->name, folder->folder_id);
            struct stat st;
            memset (&st, 0, sizeof (st));
            st.st_ino = folder->folder_id;
            st.st_mode = S_IFDIR | 0777;
            if (filler (buf, folder->name, &st, 0))
                break;
        }
        folder = folder->sibling;
    }
    DBG("Checking folders end");
    LIBMTP_destroy_folder_t (folder);
    DBG("Checking files");
    // Find files
    LIBMTP_file_t *file, *tmp;
    check_files();
    file = files;
    while (file != NULL) {
        if ((file->parent_id == folder_id) ||
           (folder_id==-2 && (file->parent_id == 0) && (file->storage_id == storageArea[storageid].storage->id))) {
            struct stat st;
            memset (&st, 0, sizeof (st));
            st.st_ino = file->item_id;
            st.st_mode = S_IFREG | 0444;
            if (filler (buf, (file->filename == NULL ? "<mtpfs null>" : file->filename), &st, 0))
                break;
        }
        tmp = file;
        file = file->next;
    }
    DBG("readdir exit");
    return_unlock(0);
}

static int
mtpfs_getattr_real (const gchar * path, struct stat *stbuf)
{
    int ret = 0;
    if (path==NULL) return_unlock(-ENOENT);
    memset (stbuf, 0, sizeof (struct stat));

    // Set uid/gid of file
    struct fuse_context *fc;
    fc = fuse_get_context();
    stbuf->st_uid = fc->uid;
    stbuf->st_gid = fc->gid;
    if (strcmp (path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0777;
        stbuf->st_nlink = 2;
        return_unlock(0);
    }

    // Check cached files first (stuff that hasn't been written to dev yet)
    GSList *item;
    if (myfiles != NULL) {
        item = g_slist_find_custom (myfiles, path, (GCompareFunc) strcmp);
        if (item != NULL) {
            stbuf->st_mode = S_IFREG | 0777;
            stbuf->st_size = 0;
            stbuf->st_blocks = 2;
            stbuf->st_mtime = time(NULL);
            return_unlock(0);
        }
    }

    // Special case directory 'Playlists', 'lost+found'
    // Special case root directory items
    if (g_strrstr(path+1,"/") == NULL) {
        stbuf->st_mode = S_IFDIR | 0777;
        stbuf->st_nlink = 2;
        return_unlock(0);
    }

    int storageid;
    storageid=find_storage(path);

    if (g_ascii_strncasecmp (path, "/Playlists",10) == 0) {
        LIBMTP_playlist_t *playlist;
        check_playlists();
        playlist=playlists;
        while (playlist != NULL) {
            gchar *tmppath;
            tmppath = g_strconcat("/Playlists/",playlist->name,".m3u",NULL);
            if (g_ascii_strcasecmp(path,tmppath) == 0) {
                int filesize = 0;
                int i;
                for (i=0; i <playlist->no_tracks; i++){
                    LIBMTP_file_t *file;
                    LIBMTP_folder_t *folder;
                    file = LIBMTP_Get_Filemetadata(device,playlist->tracks[i]);
                    if (file != NULL) {
                        int parent_id = file->parent_id;
                        filesize = filesize + strlen(file->filename) + 2;
                        while (parent_id != 0) {
                            check_folders();
                            folder = LIBMTP_Find_Folder(storageArea[i].folders,parent_id);
                            parent_id = folder->parent_id;
                            filesize = filesize + strlen(folder->name) + 1;
                        }
                    }
                }
                stbuf->st_mode = S_IFREG | 0777;
                stbuf->st_size = filesize;
                stbuf->st_blocks = 2;
                stbuf->st_mtime = time(NULL);
                return_unlock(0);
            }
            playlist = playlist->next;   
        }
        return_unlock(-ENOENT);
    }

    if (strncasecmp (path, "/lost+found",11) == 0) {
		GSList *item;
		int item_id = parse_path (path);
		for (item = lostfiles; item != NULL; item = g_slist_next (item)) {
			LIBMTP_file_t *file = (LIBMTP_file_t *) item->data;
	
			if (item_id == file->item_id) {
				stbuf->st_ino = item_id;
				stbuf->st_size = file->filesize;
				stbuf->st_blocks = (file->filesize / 512) +
					(file->filesize % 512 > 0 ? 1 : 0);
				stbuf->st_nlink = 1;
				stbuf->st_mode = S_IFREG | 0777;
                stbuf->st_mtime = file->modificationdate;
				return_unlock(0);
			}
		}

		return_unlock(-ENOENT);
    }

    int item_id = -1;
    check_folders();
    item_id = lookup_folder_id (storageArea[storageid].folders, (gchar *) path, NULL);
    if (item_id >= 0) {
        // Must be a folder
        stbuf->st_ino = item_id;
        stbuf->st_mode = S_IFDIR | 0777;
        stbuf->st_nlink = 2;
    } else {
        // Must be a file
        item_id = parse_path (path);
        LIBMTP_file_t *file;
        DBG("id:path=%d:%s", item_id, path);
        check_files();
        file = files;
        gboolean found = FALSE;
        while (file != NULL) {
            if (file->item_id == item_id) {
                stbuf->st_ino = item_id;
                stbuf->st_size = file->filesize;
                stbuf->st_blocks = (file->filesize / 512) +
                    (file->filesize % 512 > 0 ? 1 : 0);
                stbuf->st_nlink = 1;
                stbuf->st_mode = S_IFREG | 0777;
                DBG("time:%s",ctime(&(file->modificationdate)));
                stbuf->st_mtime = file->modificationdate;
                stbuf->st_ctime = file->modificationdate;
                stbuf->st_atime = file->modificationdate;
                found = TRUE;
            }
            file = file->next;
        }
        if (!found) {
            ret = -ENOENT;
        }
    }

    return ret;
}

static int
mtpfs_getattr (const gchar * path, struct stat *stbuf)
{
    enter_lock("getattr %s", path);

    int ret = mtpfs_getattr_real (path, stbuf);

    DBG("getattr exit");
    return_unlock(ret);
}

static int
mtpfs_mknod (const gchar * path, mode_t mode, dev_t dev)
{
	enter_lock("mknod %s", path);
    int item_id = parse_path (path);
    if (item_id > 0)
        return_unlock(-EEXIST);
    myfiles = g_slist_append (myfiles, (gpointer) (g_strdup (path)));
    DBG("NEW FILE");
    return_unlock(0);
}

static int
mtpfs_open (const gchar * path, struct fuse_file_info *fi)
{
    enter_lock("open");
    int item_id = -1;
    item_id = parse_path (path);
    if (item_id < 0)
        return_unlock(-ENOENT);

    if (fi->flags == O_RDONLY) {
        DBG("read");
    } else if (fi->flags == O_WRONLY) {
        DBG("write");
    } else if (fi->flags == O_RDWR) {
        DBG("rdwrite");
    }

    int storageid;
    storageid=find_storage(path);
    FILE *filetmp = tmpfile ();
    int tmpfile = fileno (filetmp);
    if (tmpfile != -1) {
        if (item_id == 0) {
            fi->fh = tmpfile;
        } else if (strncmp("/Playlists/",path,11) == 0) {
            // Is a playlist
            gchar **fields;
            fields = g_strsplit(path,"/",-1);
            gchar *name;
            name = g_strndup(fields[2],strlen(fields[2])-4);
            g_strfreev(fields);
            fi->fh = tmpfile;
            LIBMTP_playlist_t *playlist;
            check_playlists();
            playlist = playlists;
            while (playlist != NULL) {
                if (g_ascii_strcasecmp(playlist->name,name) == 0 ) {
                    //int playlist_id=playlist->playlist_id;
                    int i;
                    for (i=0; i <playlist->no_tracks; i++){
                        LIBMTP_file_t *file;
                        LIBMTP_folder_t *folder;
                        file = LIBMTP_Get_Filemetadata(device,playlist->tracks[i]);
                        if (file != NULL) {
                            gchar *path;
                            path = (gchar *) g_malloc (1024);
                            path = strcpy(path,"/");
                            int parent_id = file->parent_id;
                            while (parent_id != 0) {
                                check_folders();
                                folder = LIBMTP_Find_Folder(storageArea[storageid].folders,parent_id);
                                path = strcat(path,folder->name);
                                path = strcat(path,"/");
                                parent_id = folder->parent_id;
                            }
                            path = strcat (path,file->filename);
                            fprintf (filetmp,"%s\n",path);
                            DBG("%s\n",path);
                        }
                    }
                    //LIBMTP_destroy_file_t(file);
                    fflush(filetmp);
                    break;
                }
                playlist=playlist->next;
            }
        } else {
            int ret =
                LIBMTP_Get_File_To_File_Descriptor (device, item_id, tmpfile,
                                                    NULL, NULL);
            if (ret == 0) {
                fi->fh = tmpfile;
            } else {
                return_unlock(-ENOENT);
            }
        }
    } else {
        return_unlock(-ENOENT);
    }

    return_unlock(0);
}

static int
mtpfs_read (const gchar * path, gchar * buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    enter_lock("read");
    int ret;

    int item_id = -1;
    item_id = parse_path (path);
    if (item_id < 0)
        return_unlock(-ENOENT);

    ret = pread (fi->fh, buf, size, offset);
    if (ret == -1)
        ret = -errno;

    return_unlock(ret);
}

static int
mtpfs_write (const gchar * path, const gchar * buf, size_t size, off_t offset,
             struct fuse_file_info *fi)
{
    enter_lock("write");
    int ret;
    if (fi->fh != -1) {
        ret = pwrite (fi->fh, buf, size, offset);
    } else {
        ret = -ENOENT;
    }

    return_unlock(ret);
}


static int
mtpfs_unlink (const gchar * path)
{
    enter_lock("unlink");
    int ret = 0;
    int item_id = -1;
    item_id = parse_path (path);
    if (item_id < 0)
        return_unlock(-ENOENT);
    ret = LIBMTP_Delete_Object (device, item_id);
    if (ret != 0)
      LIBMTP_Dump_Errorstack (device);
    if (strncmp (path, "/Playlists",10) == 0) {
        playlists_changed = TRUE;
    } else {
        files_changed = TRUE;
    }

    return_unlock(ret);
}

static int
mtpfs_mkdir_real (const char *path, mode_t mode)
{
    if (g_str_has_prefix (path, "/.Trash") == TRUE)
      return_unlock(-EPERM);

    int ret = 0;
    GSList *item;
    item = g_slist_find_custom (myfiles, path, (GCompareFunc) strcmp);
    int item_id = parse_path (path);
    int storageid = find_storage(path);
    if ((item == NULL) && (item_id < 0)) {
        // Split path and find parent_id
        gchar *filename = g_strdup("");
        gchar **fields;
        gchar *directory;
	
        directory = (gchar *) g_malloc (strlen (path));
        directory = strcpy (directory, "/");
        fields = g_strsplit (path, "/", -1);
        int i;
        uint32_t parent_id = 0;
        for (i = 0; fields[i] != NULL; i++) {
            if (strlen (fields[i]) > 0) {
                if (fields[i + 1] == NULL) {
                    gchar *tmp = g_strndup (directory, strlen (directory) - 1);
                    check_folders();
                    parent_id = lookup_folder_id (storageArea[storageid].folders, tmp, NULL);
					g_free (tmp);
                    if (parent_id < 0)
                        parent_id = 0;
					g_free (filename);
                    filename = g_strdup (fields[i]);
                } else {
                    directory = strcat (directory, fields[i]);
                    directory = strcat (directory, "/");
                }
            }
        }
        DBG("%s:%s:%d", filename, directory, parent_id);
        ret = LIBMTP_Create_Folder (device, filename, parent_id,0);
        g_strfreev (fields);
		g_free (directory);
		g_free (filename);
        if (ret == 0) {
            ret = -EEXIST;
        } else {
            storageArea[storageid].folders_changed=TRUE;
            ret = 0;
        }
    } else {
        ret = -EEXIST;
    }
    return ret;
}


static int
mtpfs_mkdir (const char *path, mode_t mode)
{
    enter_lock("mkdir: %s", path);
    int ret = mtpfs_mkdir_real (path, mode);

    return_unlock(ret);
}

static int
mtpfs_rmdir (const char *path)
{
    enter_lock("rmdir %s", path);
    int ret = 0;
    int folder_id = -1;
    if (strcmp (path, "/") == 0) {
        return_unlock(0);
    }
    int storageid=find_storage(path);
    folder_id = lookup_folder_id (storageArea[storageid].folders, (gchar *) path, NULL);
    if (folder_id < 0)
        return_unlock(-ENOENT);
    
    LIBMTP_Delete_Object(device, folder_id);

    storageArea[storageid].folders_changed=TRUE;
    return_unlock(ret);
}

/* Not working. need some way in libmtp to rename objects 
int
mtpfs_rename (const char *oldname, const char *newname)
{
    uint32_t old_id = parse_path(oldname);
    LIBMTP_track_t *track;
    track = LIBMTP_Get_Trackmetadata(device,old_id);
    gchar *filename;
    gchar **fields;
    gchar *directory;
    directory = (gchar *) g_malloc (strlen (newname));
    directory = strcpy (directory, "/");
    fields = g_strsplit (newname, "/", -1);
    int i;
    uint32_t parent_id = 0;
    for (i = 0; fields[i] != NULL; i++) {
        if (strlen (fields[i]) > 0) {
            if (fields[i + 1] == NULL) {
                directory = g_strndup (directory, strlen (directory) - 1);
                parent_id = lookup_folder_id (folders, directory, NULL);
                if (parent_id < 0)
                    parent_id = 0;
                filename = g_strdup (fields[i]);
            } else {
                directory = strcat (directory, fields[i]);
                directory = strcat (directory, "/");

            }
        }
    }
    DBG("%s:%s:%d", filename, directory, parent_id);

    track->parent_id = parent_id;
    track->title = g_strdup(filename);
    int ret = LIBMTP_Update_Track_Metadata(device, track);
    return ret;
}
*/

/* Allow renaming of empty folders only */
int
mtpfs_rename (const char *oldname, const char *newname)
{
 	enter_lock("rename '%s' to '%s'", oldname, newname);

    int folder_id = -1, parent_id;
    int folder_empty = 1;
    int ret = -ENOTEMPTY;
    LIBMTP_folder_t *folder;
    LIBMTP_file_t *file;
	
    int storageid_old=find_storage(oldname);
    int storageid_new=find_storage(newname);
    if (strcmp (oldname, "/") != 0) {
        folder_id = lookup_folder_id (storageArea[storageid_old].folders, (gchar *) oldname, NULL);
    }
    if (folder_id < 0)
        return_unlock(-ENOENT);

    check_folders();
    folder = LIBMTP_Find_Folder (storageArea[storageid_old].folders, folder_id);

    /* MTP Folder object not found? */
    if (folder == NULL)
		return_unlock(-ENOENT);

    parent_id = folder->parent_id;
    folder = folder->child;

    /* Check if empty folder */
    DBG("Checking empty folder start for: subfolders");

    while (folder != NULL) {
        if (folder->parent_id == folder_id) {
			folder_empty = 0;
			break;
        }
        folder = folder->sibling;
    }

    DBG("Checking empty folder end for: subfolders. Result: %s", (folder_empty == 1 ? "empty" : "not empty"));
    
    if (folder_empty == 1) {
		/* Find files */
		check_files();
        DBG("Checking empty folder start for: files");
		file = files;
		while (file != NULL) {
			if (file->parent_id == folder_id) {
				folder_empty = 0;
				break;
			}
			file = file->next;
		}
        DBG("Checking empty folder end for: files. Result: %s", (folder_empty == 1 ? "empty" : "not empty"));


		/* Rename folder. First remove old folder, then create the new one */
		if (folder_empty == 1) {
			struct stat stbuf;
            if ( (ret = mtpfs_getattr_real (oldname, &stbuf)) == 0) {
				DBG("removing folder %s, id %d", oldname, folder_id);

				ret = mtpfs_mkdir_real (newname, stbuf.st_mode);
				LIBMTP_Delete_Object(device, folder_id);
				storageArea[storageid_old].folders_changed=TRUE;
				storageArea[storageid_new].folders_changed=TRUE;
			}
		}
    }
    return_unlock(ret);
}

static int
mtpfs_statfs (const char *path, struct statfs *stbuf)
{
    DBG("mtpfs_statfs");
    stbuf->f_bsize=1024;
    stbuf->f_blocks=device->storage->MaxCapacity/1024;
    stbuf->f_bfree=device->storage->FreeSpaceInBytes/1024;
    stbuf->f_ffree=device->storage->FreeSpaceInObjects/1024;
    stbuf->f_bavail=stbuf->f_bfree;
    return 0;
}

void *
mtpfs_init ()
{
    LIBMTP_devicestorage_t *storage;
    DBG("mtpfs_init");
    files_changed=TRUE;
    playlists_changed=TRUE;
    DBG("Ready");
    return 0;
}

int
mtpfs_blank()
{
    // Do nothing
}

static struct fuse_operations mtpfs_oper = {
    .chmod   = mtpfs_blank,
    .release = mtpfs_release,
    .readdir = mtpfs_readdir,
    .getattr = mtpfs_getattr,
    .open    = mtpfs_open,
    .mknod   = mtpfs_mknod,
    .read    = mtpfs_read,
    .write   = mtpfs_write,
    .unlink  = mtpfs_unlink,
    .destroy = mtpfs_destroy,
    .mkdir   = mtpfs_mkdir,
    .rmdir   = mtpfs_rmdir,
    .rename  = mtpfs_rename,
    .statfs  = mtpfs_statfs,
    .init    = mtpfs_init,
};

int
main (int argc, char *argv[])
{
    int fuse_stat;
    umask (0);
    LIBMTP_raw_device_t * rawdevices;
    int numrawdevices;
    LIBMTP_error_number_t err;
    int i;

    int opt;
    extern int optind;
    extern char *optarg;

    //while ((opt = getopt(argc, argv, "d")) != -1 ) {
        //switch (opt) {
        //case 'd':
            ////LIBMTP_Set_Debug(9);
            //break;
        //}
    //}

    //argc -= optind;
    //argv += optind;
    
    LIBMTP_Init ();

    fprintf(stdout, "Listing raw device(s)\n");
    err = LIBMTP_Detect_Raw_Devices(&rawdevices, &numrawdevices);
    switch(err) {
    case LIBMTP_ERROR_NO_DEVICE_ATTACHED:
        fprintf(stdout, "   No raw devices found.\n");
        return 0;
    case LIBMTP_ERROR_CONNECTING:
        fprintf(stderr, "Detect: There has been an error connecting. Exiting\n");
        return 1;
    case LIBMTP_ERROR_MEMORY_ALLOCATION:
        fprintf(stderr, "Detect: Encountered a Memory Allocation Error. Exiting\n");
        return 1;
    case LIBMTP_ERROR_NONE:
        {
            int i;

            fprintf(stdout, "   Found %d device(s):\n", numrawdevices);
            for (i = 0; i < numrawdevices; i++) {
                if (rawdevices[i].device_entry.vendor != NULL ||
                    rawdevices[i].device_entry.product != NULL) {
                    fprintf(stdout, "   %s: %s (%04x:%04x) @ bus %d, dev %d\n",
                      rawdevices[i].device_entry.vendor,
                      rawdevices[i].device_entry.product,
                      rawdevices[i].device_entry.vendor_id,
                      rawdevices[i].device_entry.product_id,
                      rawdevices[i].bus_location,
                      rawdevices[i].devnum);
                } else {
                    fprintf(stdout, "   %04x:%04x @ bus %d, dev %d\n",
                      rawdevices[i].device_entry.vendor_id,
                      rawdevices[i].device_entry.product_id,
                      rawdevices[i].bus_location,
                      rawdevices[i].devnum);
                }
            }
        }
        break;
    case LIBMTP_ERROR_GENERAL:
    default:
        fprintf(stderr, "Unknown connection error.\n");
        return 1;
    }

    fprintf(stdout, "Attempting to connect device\n");
    device = LIBMTP_Open_Raw_Device(&rawdevices[i]);
    if (device == NULL) {
        fprintf(stderr, "Unable to open raw device %d\n", i);
        return 1;
    }

    LIBMTP_Dump_Errorstack(device);
    LIBMTP_Clear_Errorstack(device);

    char *friendlyname;
    /* Echo the friendly name so we know which device we are working with */
    friendlyname = LIBMTP_Get_Friendlyname(device);
    if (friendlyname == NULL) {
        printf("Listing File Information on Device with name: (NULL)\n");
    } else {
        printf("Listing File Information on Device with name: %s\n", friendlyname);
        g_free(friendlyname);
    }

    /* Get all storages for this device */
    int ret = LIBMTP_Get_Storage(device, LIBMTP_STORAGE_SORTBY_NOTSORTED);
    if (ret != 0) {
        fprintf(stdout,"LIBMTP_Get_Storage() failed:%d\n",ret);
        LIBMTP_Dump_Errorstack(device);
        LIBMTP_Clear_Errorstack(device);
        return 1;
    }

    /* Check if multiple storage areas */
    LIBMTP_devicestorage_t *storage;
    i=0;
    for (storage = device->storage; storage != 0; storage = storage->next)  {
        storageArea[i].storage=storage;
        storageArea[i].folders=NULL;
        storageArea[i].folders_changed=TRUE;
        DBG("Storage%d: %d - %s\n",i, storage->id, storage->StorageDescription);
        i++;
    }

    DBG("Start fuse");

    fuse_stat=fuse_main (argc, argv, &mtpfs_oper);
    DBG("fuse_main returned %d\n", fuse_stat);
    return fuse_stat;
}

#ifdef USEMAD
/* Private buffer for passing around with libmad */
typedef struct
{
    /* The buffer of raw mpeg data for libmad to decode */
    void * buf;

    /* Cached data: pointers to the dividing points of frames
       in buf, and the playing time at each of those frames */
    void **frames;
    mad_timer_t *times;

    /* fd is the file descriptor if over the network, or -1 if
       using mmap()ed files */
    int fd;

    /* length of the current stream, corrected for id3 tags */
    ssize_t length;

    /* have we finished fetching this file? (only in non-mmap()'ed case */
    int done;

    /* total number of frames */
    unsigned long num_frames;

    /* number of frames to play */
    unsigned long max_frames;

    /* total duration of the file */
    mad_timer_t duration;

    /* filename as mpg321 has opened it */
    char filename[PATH_MAX];
} buffer;

/* XING parsing is from the MAD winamp input plugin */

struct xing {
      int flags;
      unsigned long frames;
      unsigned long bytes;
      unsigned char toc[100];
      long scale;
};

enum {
      XING_FRAMES = 0x0001,
      XING_BYTES  = 0x0002,
      XING_TOC    = 0x0004,
      XING_SCALE  = 0x0008
};

# define XING_MAGIC     (('X' << 24) | ('i' << 16) | ('n' << 8) | 'g')



/* Following two function are adapted from mad_timer, from the
   libmad distribution */
static
int parse_xing(struct xing *xing, struct mad_bitptr ptr, unsigned int bitlen)
{
  if (bitlen < 64 || mad_bit_read(&ptr, 32) != XING_MAGIC)
    goto fail;

  xing->flags = mad_bit_read(&ptr, 32);
  bitlen -= 64;

  if (xing->flags & XING_FRAMES) {
    if (bitlen < 32)
      goto fail;

    xing->frames = mad_bit_read(&ptr, 32);
    bitlen -= 32;
  }

  if (xing->flags & XING_BYTES) {
    if (bitlen < 32)
      goto fail;

    xing->bytes = mad_bit_read(&ptr, 32);
    bitlen -= 32;
  }

  if (xing->flags & XING_TOC) {
    int i;

    if (bitlen < 800)
      goto fail;

    for (i = 0; i < 100; ++i)
      xing->toc[i] = mad_bit_read(&ptr, 8);

    bitlen -= 800;
  }

  if (xing->flags & XING_SCALE) {
    if (bitlen < 32)
      goto fail;

    xing->scale = mad_bit_read(&ptr, 32);
    bitlen -= 32;
  }

  return 1;
 fail:
  xing->flags = 0;
  return 0;
}


int scan(void const *ptr, ssize_t len)
{
    int duration = 0;
    struct mad_stream stream;
    struct mad_header header;
    struct xing xing;
    xing.frames=0;

    unsigned long bitrate = 0;
    int has_xing = 0;
    int is_vbr = 0;
    mad_stream_init(&stream);
    mad_header_init(&header);

    mad_stream_buffer(&stream, ptr, len);

    int num_frames = 0;

    /* There are three ways of calculating the length of an mp3:
      1) Constant bitrate: One frame can provide the information
         needed: # of frames and duration. Just see how long it
         is and do the division.
      2) Variable bitrate: Xing tag. It provides the number of
         frames. Each frame has the same number of samples, so
         just use that.
      3) All: Count up the frames and duration of each frames
         by decoding each one. We do this if we've no other
         choice, i.e. if it's a VBR file with no Xing tag.
    */

    while (1)
    {
        if (mad_header_decode(&header, &stream) == -1)
        {
            if (MAD_RECOVERABLE(stream.error))
                continue;
            else
                break;
        }

        /* Limit xing testing to the first frame header */
        if (!num_frames++)
        {
            if(parse_xing(&xing, stream.anc_ptr, stream.anc_bitlen))
            {
                is_vbr = 1;

                if (xing.flags & XING_FRAMES)
                {
                    /* We use the Xing tag only for frames. If it doesn't have that
                       information, it's useless to us and we have to treat it as a
                       normal VBR file */
                    has_xing = 1;
                    num_frames = xing.frames;
                    break;
                }
            }
        }

        /* Test the first n frames to see if this is a VBR file */
        if (!is_vbr && !(num_frames > 20))
        {
            if (bitrate && header.bitrate != bitrate)
            {
                is_vbr = 1;
            }

            else
            {
                bitrate = header.bitrate;
            }
        }

        /* We have to assume it's not a VBR file if it hasn't already been
           marked as one and we've checked n frames for different bitrates */
        else if (!is_vbr)
        {
            break;
        }

        duration = header.duration.seconds;
    }

    if (!is_vbr)
    {
        double time = (len * 8.0) / (header.bitrate); /* time in seconds */
        //double timefrac = (double)time - ((long)(time));
        long nsamples = 32 * MAD_NSBSAMPLES(&header); /* samples per frame */

        /* samplerate is a constant */
        num_frames = (long) (time * header.samplerate / nsamples);

        duration = (int)time;
        DBG("d:%d",duration);
    }

    else if (has_xing)
    {
        /* modify header.duration since we don't need it anymore */
        mad_timer_multiply(&header.duration, num_frames);
        duration = header.duration.seconds;
    }

    else
    {
        /* the durations have been added up, and the number of frames
           counted. We do nothing here. */
    }

    mad_header_finish(&header);
    mad_stream_finish(&stream);
    return duration;
}


int calc_length(int f)
{
    struct stat filestat;
    void *fdm;
    char buffer[3];

    fstat(f, &filestat);

    /* TAG checking is adapted from XMMS */
    int length = filestat.st_size;

    if (lseek(f, -128, SEEK_END) < 0)
    {
        /* File must be very short or empty. Forget it. */
        return -1;
    }

    if (read(f, buffer, 3) != 3)
    {
        return -1;
    }

    if (!strncmp(buffer, "TAG", 3))
    {
        length -= 128; /* Correct for id3 tags */
    }

    fdm = mmap(0, length, PROT_READ, MAP_SHARED, f, 0);
    if (fdm == MAP_FAILED)
    {
        g_error("Map failed");
        return -1;
    }

    /* Scan the file for a XING header, or calculate the length,
       or just scan the whole file and add everything up. */
    int duration = scan(fdm, length);

    if (munmap(fdm, length) == -1)
    {
        g_error("Unmap failed");
        return -1;
    }

    lseek(f, 0, SEEK_SET);
    return duration;
}

#endif
