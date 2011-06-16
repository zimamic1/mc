/* Virtual File System: Midnight Commander file system.

   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2007,
   2008, 2009, 2010, 2011
   Free Software Foundation, Inc.

   Written by Wayne Roberts <wroberts1@home.com>, 1997
   Andrew V. Samoilov <sav@bcs.zp.ua> 2002, 2003
   Rewritten to use libsmbclient by Andrew Borodin, 2011

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License
   as published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/**
 * \file
 * \brief Source: Virtual File System: smb file system
 * \author Wayne Roberts <wroberts1@home.com>
 * \author Andrew V. Samoilov <sav@bcs.zp.ua>
 * \author Andrew Borodin
 * \date 1997, 2002, 2003, 2011
 *
 * Namespace: exports init_smbfs, smbfs_set_debug()
 */

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>           /* time() */
#include <string.h>
#include <netinet/in.h>         /* struct in_addr */
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include <libsmbclient.h>

#include "lib/global.h"
#include "lib/strutil.h"
#include "lib/util.h"
#include "lib/widget.h"         /* message() */

#include "lib/vfs/vfs.h"
#include "lib/vfs/netutil.h"
#include "lib/vfs/utilvfs.h"
#include "lib/vfs/xdirentry.h"

#include "smbfs.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define SMB_PORT 139

#define HEADER_LEN 6

#define SUP ((smb_super_data_t *) super->data)

#define smbfs_lstat smbfs_stat  /* no symlinks on smb filesystem? */

/*** file scope type declarations ****************************************************************/

typedef struct
{
    char *name;
    struct stat st;
} smb_dirinfo_t;

/* ATTENTION: new directory can be opend only after close of old one */
typedef struct
{
    int handle;                 /* for smbc_opendir, smbc_readdir, smbc_closedir */
    GSList *dir_list;           /* list of smb_dirinfo_t */
    GSList *current;            /* current of dir_list */
} smb_super_data_t;

/*** file scope variables ************************************************************************/

static struct vfs_class vfs_smbfs_ops;

static const char *const URL_HEADER = "smb" VFS_PATH_URL_DELIMITER;

static int my_errno;

static char debugf[BUF_1K];
static FILE *dbf;
static int smb_debug_level = 0;

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
smbfs_free_dirinfo (void *data)
{
    smb_dirinfo_t *info = (smb_dirinfo_t *) data;

    g_free (info->name);
    g_free (info);
}

/* --------------------------------------------------------------------------------------------- */

static void
smbfs_free_dirlist (smb_super_data_t * data)
{
    g_slist_foreach (data->dir_list, (GFunc) smbfs_free_dirinfo, NULL);
    g_slist_free (data->dir_list);
    data->dir_list = NULL;
    data->current = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
smbfs_set_debugf (const char *filename)
{
    if (smb_debug_level > 0)
    {
        FILE *outfile;

        outfile = fopen (filename, "w");
        if (outfile != NULL)
        {
            dbf = outfile;
            setbuf (dbf, NULL);
            g_strlcpy (debugf, filename, sizeof (debugf));
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
smbfs_auth_free (struct smb_authinfo const *a)
{
    g_free (a->host);
    g_free (a->share);
    g_free (a->domain);
    g_free (a->user);
    wipe_password (a->password);
}

/* --------------------------------------------------------------------------------------------- */
/********************** The callbacks ******************************/

static int
smbfs_archive_same (const vfs_path_element_t * vpath_element, struct vfs_s_super *super,
                    const vfs_path_t * vpath, void *cookie)
{
    (void) vpath_element;
    (void) super;
    (void) vpath;
    (void) cookie;

    return 1;                   /* same for initial step */
}

/* --------------------------------------------------------------------------------------------- */
static int
smbfs_open_archive (struct vfs_s_super *super, const vfs_path_t * vpath,
                    const vfs_path_element_t * vpath_element)
{
    (void) vpath;

    super->name =
        *vpath_element->path != '\0' ? g_strdup (vpath_element->path) : g_strdup (URL_HEADER);
    super->root =
        vfs_s_new_inode (vpath_element->class, super,
                         vfs_s_default_stat (vpath_element->class, S_IFDIR | 0755));
    super->path_element = vfs_path_element_clone (vpath_element);
    if (super->path_element->port == 0)
        super->path_element->port = SMB_PORT;

    super->data = g_new0 (smb_super_data_t, 1);

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static void
smbfs_free_archive (struct vfs_class *me, struct vfs_s_super *super)
{
    (void) me;

    smbfs_free_dirlist (SUP);
    g_free (SUP);
    super->data = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_fh_open (struct vfs_class *me, vfs_file_handler_t * fh, int flags, mode_t mode)
{
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_fh_close (struct vfs_class *me, vfs_file_handler_t * fh)
{
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_dir_load (struct vfs_class *me, struct vfs_s_inode *ino, char *path)
{
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static void
smbc_get_auth_data_cb (const char *srv, const char *shr, char *wg, int wglen, char *un, int unlen,
                       char *pw, int pwlen)
{
    g_strlcpy (un, "guest", unlen);
    g_strlcpy (pw, "", pwlen);
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_init (struct vfs_class *me)
{
    int ret;

    (void) me;

    ret = smbc_init (smbc_get_auth_data_cb, smb_debug_level);

    return ret < 0 ? 0 : 1;
}

/* --------------------------------------------------------------------------------------------- */

static void
smbfs_free (vfsid id)
{
    (void) id;
}

/* --------------------------------------------------------------------------------------------- */

static void
smbfs_fill_names (struct vfs_class *me, fill_names_f func)
{
#if 0
    size_t i;
    char *path;

    (void) me;

    for (i = 0; i < SMBFS_MAX_CONNECTIONS; i++)
    {
        if (smbfs_connections[i].cli)
        {
            path = g_strconcat (URL_HEADER,
                                smbfs_connections[i].user, "@",
                                smbfs_connections[i].host,
                                "/", smbfs_connections[i].service, (char *) NULL);
            (*func) (path);
            g_free (path);
        }
    }
#else
    (void) me;
    (void) func;
#endif
}

/* --------------------------------------------------------------------------------------------- */

static ssize_t
smbfs_read (void *data, char *buf, size_t count)
{
    ssize_t n = 0;

    (void) data;
    (void) buf;
    (void) count;

    return n;
}

/* --------------------------------------------------------------------------------------------- */

static ssize_t
smbfs_write (void *data, const char *buf, size_t count)
{
    ssize_t n = 0;

    (void) data;
    (void) buf;
    (void) count;

    return n;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_close (void *data)
{
    (void) data;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_errno (struct vfs_class *me)
{
    (void) me;

    return my_errno;
}

/* --------------------------------------------------------------------------------------------- */

/* The smbfs_opendir routine loads the complete directory */
/* It's too slow to ask the server each time */

static void *
smbfs_opendir (const vfs_path_t * vpath)
{
    void *smbfs_info;
    vfs_path_element_t *path_element;
    struct vfs_s_subclass *subclass;
    struct vfs_s_super *super;
    char *dir;
    int res;
    smb_super_data_t *ret = NULL;
    char dirbuf[BUF_1K];
    int err;

    path_element = vfs_path_get_by_index (vpath, -1);
    subclass = (struct vfs_s_subclass *) path_element->class->data;

    /* search super for this path */
    super = (struct vfs_s_super *) subclass->supers->data;      /* !!! temporary */
    /* found */
    path_element = super->path_element;

    dir = mc_build_filename (URL_HEADER, path_element->host, path_element->path, (char *) NULL);
    res = smbc_opendir (dir + 1);       /* skip leading / added in mc_build_filename() */

    if (res < 0)
    {
        message (D_ERROR, MSG_ERROR, _("Could not open dir %s\n%s"), dir,
                 unix_error_string (errno));
        return NULL;
    }

    /* FIXME: do that only on success */
    g_free (path_element->path);
    path_element->path = dir;

    ret = (smb_super_data_t *) super->data;
    smbfs_free_dirlist (ret);
    ret->handle = res;

    /* load directory content */
    while ((err = smbc_getdents (res, (struct smbc_dirent *) dirbuf, sizeof (dirbuf))) != 0)
    {
        struct smbc_dirent *dirp;

        /* An error, report it */
        if (err < 0)
        {
            smbc_closedir (res);
            return NULL;
        }

        dirp = (struct smbc_dirent *) dirbuf;

        while (err > 0)
        {
            size_t dirlen = dirp->dirlen;

            if ((strcmp (dirp->name, ".") != 0) && (strcmp (dirp->name, "..") != 0))
            {
                smb_dirinfo_t *info;

                switch (dirp->smbc_type)
                {
                    /* treat as directory */
                case SMBC_WORKGROUP:
                case SMBC_SERVER:
                case SMBC_IPC_SHARE:
                case SMBC_FILE_SHARE:
                case SMBC_PRINTER_SHARE:
                case SMBC_COMMS_SHARE:
                    {
                        info = g_new (smb_dirinfo_t, 1);
                        info->name = g_strdup (dirp->name);

                        /* don't get actual stat of these resources */
                        info->st.st_dev = 0;
                        info->st.st_ino = 0;
                        info->st.st_mode = 0755;
                        info->st.st_nlink = 0;
                        info->st.st_uid = getuid ();
                        info->st.st_gid = getgid ();
                        info->st.st_rdev = 0;
                        info->st.st_size = 0;
                        info->st.st_blksize = 0;
                        info->st.st_blocks = 0;
                        info->st.st_atime = time (NULL);        /* FIXME */
                        info->st.st_mtime = 0;
                        info->st.st_ctime = 0;

                        ret->dir_list = g_slist_prepend (ret->dir_list, info);

                        break;
                    }

                case SMBC_DIR:
                case SMBC_FILE:
                case SMBC_LINK:
                default:
                    {
                        char *full_name;
                        struct stat st;

                        full_name =
                            mc_build_filename (URL_HEADER, path_element->host,
                                               path_element->path, dirp->name, (char *) NULL);

                        if (smbc_stat (full_name + 1, &st) < 0)
                            message (D_ERROR, MSG_ERROR, _("Could not stat of %s\n%s"),
                                     full_name, unix_error_string (errno));
                        else
                        {
                            info = g_new (smb_dirinfo_t, 1);
                            info->name = g_strdup (dirp->name);
                            info->st = st;

                            ret->dir_list = g_slist_prepend (ret->dir_list, info);
                        }

                        g_free (full_name);

                        break;
                    }
                }               /* switch */
            }                   /* if */

            dirp = (struct smbc_dirent *) ((char *) dirp + dirlen);
            err -= (int) dirlen;
        }                       /* while */
    }                           /* while */

    smbc_closedir (res);

    ret->dir_list = g_slist_reverse (ret->dir_list);
    ret->current = ret->dir_list;

    return ret;
}

/* --------------------------------------------------------------------------------------------- */

static void *
smbfs_readdir (void *info)
{
    static union vfs_dirent dir;

    smb_super_data_t *super = (smb_super_data_t *) info;

    if (super->current == NULL)
        return NULL;

    g_strlcpy (dir.dent.d_name, ((smb_dirinfo_t *) super->current->data)->name, MC_MAXPATHLEN);

    super->current = g_slist_next (super->current);

    return &dir;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_closedir (void *info)
{
    smb_super_data_t *super = (smb_super_data_t *) info;

    smbfs_free_dirlist (super);
    return smbc_closedir (super->handle);
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_chmod (const vfs_path_t * vpath, mode_t mode)
{
    (void) vpath;
    (void) mode;

    /*      my_errno = EOPNOTSUPP;
       return -1;   *//* cannot chmod on smb filesystem */
    return 0;                   /* make mc happy */
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_chown (const vfs_path_t * vpath, uid_t owner, gid_t group)
{
    (void) vpath;
    (void) owner;
    (void) group;

    my_errno = EOPNOTSUPP;      /* ready for your labotomy? */
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_utime (const vfs_path_t * vpath, struct utimbuf *times)
{
    (void) vpath;
    (void) times;

    my_errno = EOPNOTSUPP;
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_readlink (const vfs_path_t * vpath, char *buf, size_t size)
{
    (void) vpath;
    (void) buf;
    (void) size;

    my_errno = EOPNOTSUPP;
    return -1;                  /* no symlinks on smb filesystem? */
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_symlink (const vfs_path_t * vpath1, const vfs_path_t * vpath2)
{
    (void) vpath1;
    (void) vpath2;

    my_errno = EOPNOTSUPP;
    return -1;                  /* no symlinks on smb filesystem? */
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_stat (const vfs_path_t * vpath, struct stat *buf)
{
    vfs_path_element_t *path_element = vfs_path_get_by_index (vpath, -1);

    (void) buf;

    /* stat dirs & files under shares now */
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static off_t
smbfs_lseek (void *data, off_t offset, int whence)
{
    (void) data;
    (void) offset;
    (void) whence;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_mknod (const vfs_path_t * vpath, mode_t mode, dev_t dev)
{
    (void) vpath;
    (void) mode;
    (void) dev;

    my_errno = EOPNOTSUPP;
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_mkdir (const vfs_path_t * vpath, mode_t mode)
{
    (void) vpath;
    (void) mode;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_rmdir (const vfs_path_t * vpath)
{
    (void) vpath;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_link (const vfs_path_t * vpath1, const vfs_path_t * vpath2)
{
    (void) vpath1;
    (void) vpath2;

    my_errno = EOPNOTSUPP;
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/* Gives up on a socket and reopens the connection, the child own the socket
 * now
 */

static void
smbfs_forget (const char *path)
{
    (void) path;
}

/* --------------------------------------------------------------------------------------------- */
static int
smbfs_setctl (const vfs_path_t * vpath, int ctlop, void *arg)
{
    vfs_path_element_t *path_element = vfs_path_get_by_index (vpath, -1);

    switch (ctlop)
    {
    case VFS_SETCTL_LOGFILE:
        smbfs_set_debugf ((char *) arg);
        break;
    case VFS_SETCTL_FORGET:
        smbfs_forget (path_element->path);
        break;
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static void *
smbfs_open (const vfs_path_t * vpath, int flags, mode_t mode)
{
    void *ret;

    (void) vpath;
    (void) flags;
    (void) mode;

    return ret;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_unlink (const vfs_path_t * vpath)
{
    (void) vpath;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_rename (const vfs_path_t * vpath1, const vfs_path_t * vpath2)
{
    (void) vpath1;
    (void) vpath2;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
smbfs_fstat (void *data, struct stat *buf)
{
    (void) data;
    (void) buf;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
smbfs_set_debug (int arg)
{
    if (arg < 0 || arg > 100)
        arg = 0;

    smb_debug_level = arg;
}

/* --------------------------------------------------------------------------------------------- */

smb_authinfo *
vfs_smb_authinfo_new (const char *host, const char *share, const char *domain,
                      const char *user, const char *pass)
{
    smb_authinfo *auth;

    auth = g_try_new (struct smb_authinfo, 1);

    if (auth != NULL)
    {
        auth->host = g_strdup (host);
        auth->share = g_strdup (share);
        auth->domain = g_strdup (domain);
        auth->user = g_strdup (user);
        auth->password = g_strdup (pass);
    }

    return auth;
}

/* --------------------------------------------------------------------------------------------- */

void
init_smbfs (void)
{
    static struct vfs_s_subclass smbfs_subclass;

    tcp_init ();

    smbfs_subclass.flags = VFS_S_REMOTE;
    smbfs_subclass.archive_same = smbfs_archive_same;
    smbfs_subclass.open_archive = smbfs_open_archive;
    smbfs_subclass.free_archive = smbfs_free_archive;
    smbfs_subclass.fh_open = smbfs_fh_open;
    smbfs_subclass.fh_close = smbfs_fh_close;
    smbfs_subclass.dir_load = smbfs_dir_load;

    vfs_s_init_class (&vfs_smbfs_ops, &smbfs_subclass);
    vfs_smbfs_ops.name = "smbfs";
    vfs_smbfs_ops.prefix = "smb";
    vfs_smbfs_ops.flags = VFSF_NOLINKS;
    vfs_smbfs_ops.init = smbfs_init;
    vfs_smbfs_ops.free = smbfs_free;
    vfs_smbfs_ops.fill_names = smbfs_fill_names;
    vfs_smbfs_ops.open = smbfs_open;
    vfs_smbfs_ops.close = smbfs_close;
    vfs_smbfs_ops.read = smbfs_read;
    vfs_smbfs_ops.write = smbfs_write;
    vfs_smbfs_ops.opendir = smbfs_opendir;
    vfs_smbfs_ops.readdir = smbfs_readdir;
    vfs_smbfs_ops.closedir = smbfs_closedir;
    vfs_smbfs_ops.stat = smbfs_stat;
    vfs_smbfs_ops.lstat = smbfs_lstat;
    vfs_smbfs_ops.fstat = smbfs_fstat;
    vfs_smbfs_ops.chmod = smbfs_chmod;
    vfs_smbfs_ops.chown = smbfs_chown;
    vfs_smbfs_ops.utime = smbfs_utime;
    vfs_smbfs_ops.readlink = smbfs_readlink;
    vfs_smbfs_ops.symlink = smbfs_symlink;
    vfs_smbfs_ops.link = smbfs_link;
    vfs_smbfs_ops.unlink = smbfs_unlink;
    vfs_smbfs_ops.rename = smbfs_rename;
    vfs_smbfs_ops.ferrno = smbfs_errno;
    vfs_smbfs_ops.lseek = smbfs_lseek;
    vfs_smbfs_ops.mknod = smbfs_mknod;
    vfs_smbfs_ops.mkdir = smbfs_mkdir;
    vfs_smbfs_ops.rmdir = smbfs_rmdir;
    vfs_smbfs_ops.setctl = smbfs_setctl;
    vfs_register_class (&vfs_smbfs_ops);
}
