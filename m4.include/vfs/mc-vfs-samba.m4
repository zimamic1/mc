dnl Samba support
AC_DEFUN([AC_MC_VFS_SMB],
[
    AC_ARG_ENABLE([vfs-smb],
		AS_HELP_STRING([--enable-vfs-smb], [Support for SMB filesystem @<:@no@:>@]),
		[
		    if test "x$enableval" = "xno"; then
			enable_vfs_smb=no
		    else
			enable_vfs_smb=yes
		    fi
		],
		[enable_vfs_smb=no])

    if test "$enable_vfs" = "yes" -a x"$enable_vfs_smb" != x"no"; then
	AC_CHECK_HEADERS([libsmbclient.h], [enable_vfs_smb=yes], [enable_vfs_smb=no], [])

	if test "$enable_vfs_smb" = "yes"; then
	    AC_CHECK_LIB(smbclient, main,
			 [enable_vfs_smb=yes], [enable_vfs_smb=no], [])
	fi
    fi

    if test "$enable_vfs_smb" = "yes"; then
	AC_MC_VFS_ADDNAME([smb])
	AC_DEFINE([ENABLE_VFS_SMB], [1], [Define to enable VFS over SMB])
	AM_CONDITIONAL([ENABLE_VFS_SMB], [test "1" = "1"])
    else
	AM_CONDITIONAL([ENABLE_VFS_SMB], [test "1" = "2"])
    fi
])
