/*
 * mkdir.c --- make a directory in the filesystem
 *
 * Copyright (C) 1994, 1995 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <time.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"

#ifndef EXT2_FT_DIR
#define EXT2_FT_DIR		2
#endif

errcode_t ext2fs_mkdir(ext2_filsys fs, ext2_ino_t parent, ext2_ino_t inum,
		       const char *name)
{
	ext2_extent_handle_t	handle;
	errcode_t		retval;
	struct ext2_inode	parent_inode, inode;
	ext2_ino_t		ino = inum;
	ext2_ino_t		scratch_ino;
	blk64_t			blk;
	char			*block = 0;
	int			inline_data = 0;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	/*
	 * Allocate an inode, if necessary
	 */
	if (!ino) {
		retval = ext2fs_new_inode(fs, parent, LINUX_S_IFDIR | 0755,
					  0, &ino);
		if (retval)
			goto cleanup;
	}

	if (fs->super->s_feature_incompat & EXT4_FEATURE_INCOMPAT_INLINE_DATA &&
	    ino >= EXT2_FIRST_INO(fs->super) &&
	    strcmp("lost+found", name) != 0) {
		retval = ext2fs_inline_data_mkdir(fs, parent, ino);
		if (retval)
			goto cleanup;
		inline_data = 1;
		goto make_link;
	}

	/*
	 * Allocate a data block for the directory
	 */
	retval = ext2fs_new_block2(fs, 0, 0, &blk);
	if (retval)
		goto cleanup;

	/*
	 * Create a scratch template for the directory
	 */
	retval = ext2fs_new_dir_block(fs, ino, parent, &block);
	if (retval)
		goto cleanup;

	/*
	 * Get the parent's inode, if necessary
	 */
	if (parent != ino) {
		retval = ext2fs_read_inode(fs, parent, &parent_inode);
		if (retval)
			goto cleanup;
	} else
		memset(&parent_inode, 0, sizeof(parent_inode));

	/*
	 * Create the inode structure....
	 */
	memset(&inode, 0, sizeof(struct ext2_inode));
	inode.i_mode = LINUX_S_IFDIR | (0777 & ~fs->umask);
	inode.i_uid = inode.i_gid = 0;
	ext2fs_iblk_set(fs, &inode, 1);
	if (fs->super->s_feature_incompat & EXT3_FEATURE_INCOMPAT_EXTENTS)
		inode.i_flags |= EXT4_EXTENTS_FL;
	else
		inode.i_block[0] = blk;
	inode.i_links_count = 2;
	inode.i_size = fs->blocksize;

	/*
	 * Write out the inode and inode data block.  The inode generation
	 * number is assigned by write_new_inode, which means that the call
	 * to write_dir_block must come after that.
	 */
	retval = ext2fs_write_new_inode(fs, ino, &inode);
	if (retval)
		goto cleanup;
	retval = ext2fs_write_dir_block4(fs, blk, block, 0, ino);
	if (retval)
		goto cleanup;

	if (fs->super->s_feature_incompat & EXT3_FEATURE_INCOMPAT_EXTENTS) {
		retval = ext2fs_extent_open2(fs, ino, &inode, &handle);
		if (retval)
			goto cleanup;
		retval = ext2fs_extent_set_bmap(handle, 0, blk, 0);
		ext2fs_extent_free(handle);
		if (retval)
			goto cleanup;
	}

make_link:
	/*
	 * Link the directory into the filesystem hierarchy
	 */
	if (name) {
		retval = ext2fs_lookup(fs, parent, name, strlen(name), 0,
				       &scratch_ino);
		if (!retval) {
			retval = EXT2_ET_DIR_EXISTS;
			name = 0;
			goto cleanup;
		}
		if (retval != EXT2_ET_FILE_NOT_FOUND)
			goto cleanup;
		retval = ext2fs_link(fs, parent, name, ino, EXT2_FT_DIR);
		if (retval)
			goto cleanup;
	}

	/*
	 * Update parent inode's counts
	 */
	if (parent != ino) {
		/* Reload parent inode due to inline data */
		retval = ext2fs_read_inode(fs, parent, &parent_inode);
		if (retval)
			goto cleanup;
		parent_inode.i_links_count++;
		retval = ext2fs_write_inode(fs, parent, &parent_inode);
		if (retval)
			goto cleanup;
	}

	/*
	 * Update accounting....
	 */
	if (!inline_data)
		ext2fs_block_alloc_stats2(fs, blk, +1);
	ext2fs_inode_alloc_stats2(fs, ino, +1, 1);

cleanup:
	if (block)
		ext2fs_free_mem(&block);
	return retval;

}


