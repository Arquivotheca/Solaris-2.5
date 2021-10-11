/*
 *
 *			res.c
 *
 * Implements routines to create a cache resource file.
 */

#pragma ident "@(#)res.c   1.15     94/11/11 SMI"

/*
 *  Copyright (c) 1994, by Sun Microsystems, Inc.
 *	All rights reserved.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/fs/cachefs_fs.h>
#include "res.h"

struct res {
	int			 p_magic;	/* magic number */
	int			 p_done:1;	/* 1 if res_done called */
	void			*p_addrp;	/* address of mapped file */
	long			 p_size;	/* size of mapped file */
	struct cache_usage	*p_cusagep;	/* ptr to cache_usage */
	struct lru_info		*p_linfop;	/* ptr to lru_info */
	struct lru_idents	*p_lidentp;	/* ptr to lru_idents */
	struct lru_pointers	*p_lptrp;	/* ptr to lru_pointers */
	int			 p_totentries;	/* max number of lru entries */
	char		 p_name[MAXPATHLEN];	/* name of resource file */
};

#define	MAGIC 8272
#define	precond(A) assert(A)
#define	MININDEX 1

/* forward references */
void res_lru_remove(res *resp, int index);
void res_lru_append(res *resp, int index);
void res_reset(res *resp);
void res_clear(res *resp);

/*
 *
 *			res_create
 *
 * Description:
 *	Creates a res object and returns a pointer to it.
 *	The specified file is used to store resource file data.
 * Arguments:
 *	namep	name of the resource file
 *	entries	max number of lru entries in the file
 * Returns:
 *	Returns a pointer to the object or NULL if an error occurred.
 * Preconditions:
 *	precond(namep)
 *	precond(entries > 3)
 *	precond(strlen(namep) < MAXPATHLEN)
 */

res *
res_create(char *namep, int entries)
{
	int xx;
	long size;
	int fd;
	char buf[1024];
	long cnt;
	unsigned int amt;
	ssize_t result;
	void *addrp;
	res *resp;
	long identoff;
	long ptroff;
	struct stat statinfo;

	precond(namep);
	precond(entries > MININDEX);

	/* determine the size needed for the resource file */
	size = MAXBSIZE;
	ptroff = size;
	size += entries * sizeof (struct lru_pointers);
	size = ((size + MAXBSIZE - 1) / MAXBSIZE) * MAXBSIZE;
	identoff = size;
	size += entries * sizeof (struct lru_idents);
	size = ((size + MAXBSIZE - 1) / MAXBSIZE) * MAXBSIZE;

	/* if the file does not exist or is the wrong size/type */
	xx = lstat(namep, &statinfo);
	if ((xx == -1) || (statinfo.st_size != size) ||
	    !(S_ISREG(statinfo.st_mode))) {

		/* remove the resource file */
		xx = unlink(namep);
		if ((xx == -1) && (errno != ENOENT))
			return (NULL);

		/* create and open the file */
		fd = open(namep, O_CREAT | O_RDWR, 0600);
		if (fd == -1)
			return (NULL);

		/* fill the file with zeros */
		memset(buf, 0, sizeof (buf));
		for (cnt = size; cnt > 0; cnt -= result) {
			amt = sizeof (buf);
			if (amt > cnt)
				amt = cnt;
			result = write(fd, buf, amt);
			if (result == -1) {
				close(fd);
				return (NULL);
			}
		}
	}

	/* else open the file */
	else {
		fd = open(namep, O_RDWR);
		if (fd == -1)
			return (NULL);
	}

	/* mmap the file into our address space */
	addrp = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (addrp == (void *)-1) {
		close(fd);
		return (NULL);
	}

	/* close the file descriptor, we do not need it anymore */
	close(fd);

	/* allocate memory for the res object */
	resp = malloc(sizeof (res));
	if (resp == NULL) {
		munmap(addrp, size);
		return (NULL);
	}

	/* initialize the object */
	resp->p_magic = MAGIC;
	resp->p_done = 0;
	resp->p_addrp = addrp;
	resp->p_size = size;
	resp->p_cusagep = (struct cache_usage *)addrp;
	resp->p_linfop = (struct lru_info *)((char *)addrp +
	    sizeof (struct cache_usage));
	resp->p_lidentp = (struct lru_idents *)((char *)addrp + identoff);
	resp->p_lptrp = (struct lru_pointers *)((char *)addrp + ptroff);
	resp->p_totentries = entries;
	strcpy(resp->p_name, namep);

	/* reset the resource file in preperation to rebuild it */
	res_reset(resp);

	/* return the object */
	return (resp);
}

/*
 *
 *			res_destroy
 *
 * Description:
 *	Destroys the specifed res object.
 *	If res_done has not been called on the object or if res_done
 *	failed, then the resource file will be deleted.
 * Arguments:
 *	resp	object to destroy
 * Returns:
 * Preconditions:
 *	precond(resp is a valid res object)
 */

void
res_destroy(res *resp)
{
	precond(resp);
	precond(resp->p_magic == MAGIC);

	/* unmap the file */
	munmap(resp->p_addrp, resp->p_size);

	/* if res_done not performed */
	if (resp->p_done == 0) {
		/* remove the resource file */
		unlink(resp->p_name);
	}

	/* destroy the object */
	resp->p_magic = -MAGIC;
	free(resp);
}

/*
 *
 *			res_reset
 *
 * Description:
 *	Resets the resource file in preparation to rebuild it.
 * Arguments:
 *	resp	res object
 * Returns:
 * Preconditions:
 *	precond(resp is a valid res object)
 */

void
res_reset(res *resp)
{
	int index;
	struct lru_pointers *lptrp;
	struct lru_idents *lidentp;
	int previndex;

	precond(resp);
	precond(resp->p_magic == MAGIC);

	resp->p_cusagep->cu_blksused = 0;
	resp->p_cusagep->cu_filesused = 0;
	resp->p_cusagep->cu_flags = CUSAGE_ACTIVE;	/* dirty cache */
	resp->p_linfop->lru_free = 0;
	resp->p_linfop->lru_entries = 0;
	resp->p_linfop->active_front = 0;
	resp->p_linfop->active_back = 0;

	/* zero the lru_idents section */
	memset(resp->p_lidentp, 0,
	    resp->p_totentries * sizeof (struct lru_idents));

	/* walk the lru_pointers list, look for cycles */
	previndex = 0;
	for (index = resp->p_linfop->lru_front;
	    index != 0;
	    index = lptrp->lru_bkwd_idx) {
		/* make sure offset is in bounds */
		if ((index < MININDEX) || (index >= resp->p_totentries)) {
			pr_err("index out of bounds %d", index);
			res_clear(resp);
			return;
		}

		/* get pointer to lru_pointer object */
		lptrp = resp->p_lptrp + index;

		/* check forward pointer */
		if (lptrp->lru_fwd_idx != previndex) {
			/* bad back pointer in lru list */
			pr_err("bad forward pointer %d %d",
			    lptrp->lru_fwd_idx, previndex);
			res_clear(resp);
			return;
		}

		/* check for cycle */
		lidentp = resp->p_lidentp + index;
		if (lidentp->lru_local) {
			/* cycle found in list */
			pr_err("cycle found in list %d", index);
			res_clear(resp);
			return;
		}

		/* indicate we have seen this pointer */
		lidentp->lru_local = 1;
		previndex = index;
	}

	/* set the back pointer of the lru list */
	resp->p_linfop->lru_back = previndex;
}

/*
 *
 *			res_lru_remove
 *
 * Description:
 *	Removes the specified lru_pointer object from the lru list.
 * Arguments:
 *	resp	res object
 *	index	index of lru_pointer in lru_pointer section of file
 * Returns:
 * Preconditions:
 *	precond(resp is a valid res object)
 *	precond(index is a valid index)
 */

void
res_lru_remove(res *resp, int index)
{
	int fi, bi;
	struct lru_pointers *lptrp;

	lptrp = resp->p_lptrp + index;

	fi = lptrp->lru_fwd_idx;
	bi = lptrp->lru_bkwd_idx;

	lptrp->lru_fwd_idx = 0;
	lptrp->lru_bkwd_idx = 0;

	if (fi) {
		if (bi) {
			/* back, front */
			lptrp = resp->p_lptrp + bi;
			lptrp->lru_fwd_idx = fi;
			lptrp = resp->p_lptrp + fi;
			lptrp->lru_bkwd_idx = bi;
		} else {
			/* no back, front - end of list */
			lptrp = resp->p_lptrp + fi;
			lptrp->lru_bkwd_idx = 0;
			resp->p_linfop->lru_back = fi;
		}
	} else {
		if (bi) {
			/* back, no front - start of list */
			lptrp = resp->p_lptrp + bi;
			lptrp->lru_fwd_idx = 0;
			resp->p_linfop->lru_front = bi;
		} else {
			/* no back, no front - last item on list */
			resp->p_linfop->lru_front = 0;
			resp->p_linfop->lru_back = 0;
		}
	}
}

/*
 *
 *			res_lru_append
 *
 * Description:
 *	Appends the specified lru_pointer object to the end
 *	of the lru list.
 * Arguments:
 *	resp	res object
 *	index	index of lru_pointer in lru_pointer section of file
 * Returns:
 * Preconditions:
 *	precond(resp is a valid res object)
 *	precond(index is a valid index)
 */

void
res_lru_append(res *resp, int index)
{
	struct lru_pointers *lptrp;

	lptrp = resp->p_lptrp + index;

	if (resp->p_linfop->lru_back == 0) {

		/* empty lru list */
		assert(resp->p_linfop->lru_front == 0);
		resp->p_linfop->lru_back = index;
		resp->p_linfop->lru_front = index;
		lptrp->lru_fwd_idx = 0;
		lptrp->lru_bkwd_idx = 0;

	} else {

		/* add to end of lru list */
		lptrp->lru_bkwd_idx = 0;
		lptrp->lru_fwd_idx = resp->p_linfop->lru_back;
		lptrp = resp->p_lptrp + resp->p_linfop->lru_back;
		lptrp->lru_bkwd_idx = index;
		resp->p_linfop->lru_back = index;
	}
}

/*
 *
 *			res_clear
 *
 * Description:
 *	Deletes all information from the resource file.
 * Arguments:
 *	resp	res object
 * Returns:
 * Preconditions:
 *	precond(resp is a valid res object)
 */

void
res_clear(res *resp)
{
	memset(resp->p_addrp, 0, resp->p_size);
}


/*
 *
 *			res_done
 *
 * Description:
 *	Called when through performing res_addfile and res_addident
 *	to complete the resource file and flush the contents to
 *	the disk file.
 * Arguments:
 *	resp	res object
 * Returns:
 *	Returns 0 for success, -1 for an error with errno set
 *	appropriatly.
 * Preconditions:
 *	precond(resp is a valid res object)
 */

int
res_done(res *resp)
{
	struct lru_idents *lidentp;
	struct lru_pointers *lptrp;
	int index;
	int xx;

	precond(resp);
	precond(resp->p_magic == MAGIC);

	/* scan the ident list to find the max allocated entry */
	resp->p_linfop->lru_entries = 0;
	lidentp = resp->p_lidentp + MININDEX;
	for (index = MININDEX; index < resp->p_totentries; index++) {
		if (lidentp->lru_fsid != 0) {
			resp->p_linfop->lru_entries = index;
		}
		lidentp++;
	}

	/* scan the ident list to fix up lru and free list */
	lidentp = resp->p_lidentp + MININDEX;
	for (index = MININDEX;
	    index < resp->p_totentries;
	    index++, lidentp++) {

		/* if entry is not valid */
		if (lidentp->lru_fsid == 0) {
			/* if entry is on the lru list */
			if (lidentp->lru_local) {
				/* remove entry from the lru list */
				res_lru_remove(resp, index);

				lidentp->lru_local = 0;
			}

			/* if entry should appear on the free list */
			if (index <= resp->p_linfop->lru_entries) {
				/* add entry to the free list */
				lptrp = resp->p_lptrp + index;
				lptrp->lru_fwd_idx = resp->p_linfop->lru_free;
				lptrp->lru_bkwd_idx = 0;
				resp->p_linfop->lru_free = index;
			}
		}
	}

	/* indicate the cache is clean */
	resp->p_cusagep->cu_flags &= ~CUSAGE_ACTIVE;

	/* sync the data to the file */
	xx = msync(resp->p_addrp, resp->p_size, 0);
	if (xx == -1)
		return (-1);

	resp->p_done = 1;

	/* return success */
	return (0);
}

/*
 *
 *			res_addfile
 *
 * Description:
 *	Increments the number of files and blocks resouce counts.
 * Arguments:
 *	resp	res object
 *	nbytes	number of bytes in the file
 * Returns:
 * Preconditions:
 *	precond(resp is a valid res object)
 */

void
res_addfile(res *resp, long nbytes)
{
	long nblks;

	precond(resp);
	precond(resp->p_magic == MAGIC);

	/* convert byte value to MAXBSIZE blocks */
	nblks = (nbytes + MAXBSIZE - 1) / MAXBSIZE;

	/* update block count */
	resp->p_cusagep->cu_blksused += nblks;

	/* incriment number of files */
	resp->p_cusagep->cu_filesused += 1;
}

/*
 *
 *			res_addident
 *
 * Description:
 *	Adds the specified file to the ident list.
 *	If local is 0 then the file is also added to the lru list.
 *	Updates resource counts.
 * Arguments:
 *	resp	res object
 *	index	index into idents/pointers tables
 *	fsid	identifier of cache file system
 *	fileno	file number of back file
 *	local	1 if a local file
 *	attrc	1 if an attrcache file
 *	onlru	1 if should be on lru, 0 if not
 *	nbytes	number of bytes in the file
 * Returns:
 *	Returns 0 for success or -1 if the index is already in use
 *	or is not valid.
 * Preconditions:
 *	precond(resp is a valid res object)
 *	precond(fsid)
 */

int
res_addident(res *resp, int index, int fsid, ino_t fileno, int local, int attrc,
    int onlru, long nbytes)
{
	struct lru_idents *lidentp;

	precond(resp);
	precond(resp->p_magic == MAGIC);
	precond(fsid);

	/* check index for sanity */
	if ((index < MININDEX) || (index >= resp->p_totentries)) {
#if 0
		pr_err("addident: index out of bounds %d", index);
#endif
		return (-1);
	}

	/* get pointer to ident */
	lidentp = resp->p_lidentp + index;

	/* if something already there */
	if (lidentp->lru_fsid != 0) {
#if 0
		pr_err("addident: index already in use %d", index);
#endif
		return (-1);
	}

	/* if should not be on lru list */
	if (onlru == 0) {
		/* if on the lru list */
		if (lidentp->lru_local) {
			/* remove from the lru list */
			res_lru_remove(resp, index);
		}
	}

	/* else if should be on the lru list */
	else {
		/* if not on the lru list */
		if (lidentp->lru_local == 0) {
			/* add to the end of the lru list */
			res_lru_append(resp, index);
		}
	}

	lidentp->lru_local = local;
	lidentp->lru_attrc = attrc;
	lidentp->lru_fsid = fsid;
	lidentp->lru_fileno = fileno;

	/* update resource counts */
	res_addfile(resp, nbytes);

	/* return success */
	return (0);
}

/*
 *
 *			res_clearident
 *
 * Description:
 *	Removes the specified file from the ident list.
 *	Updates resource counts.
 * Arguments:
 *	resp	res object
 *	index	index into idents/pointers tables
 *	onlru	indicates if ident is also on the lru list
 *	nbytes	number of bytes in the file
 * Returns:
 *	Returns 0.
 * Preconditions:
 *	precond(resp is a valid res object)
 *	precond(index is valid)
 *	precond(ident is in use)
 */

int
res_clearident(res *resp, int index, int onlru, int nbytes)
{
	struct lru_idents *lidentp;
	int nblks;

	precond(resp);
	precond(resp->p_magic == MAGIC);
	precond((index >= MININDEX) && (index < resp->p_totentries));

	/* get pointer to ident */
	lidentp = resp->p_lidentp + index;
	precond(lidentp->lru_fsid != 0);

	/* clear the ident */
	lidentp->lru_fsid = 0;
	lidentp->lru_fileno = 0;
	lidentp->lru_attrc = 0;

	/* indicate if on the lru */
	lidentp->lru_local = onlru;

	/* update resource counts */

	/* convert byte value to MAXBSIZE blocks */
	nblks = (nbytes + MAXBSIZE - 1) / MAXBSIZE;

	/* update block count */
	resp->p_cusagep->cu_blksused -= nblks;
	assert(resp->p_cusagep->cu_blksused >= 0);

	/* incriment number of files */
	resp->p_cusagep->cu_filesused -= 1;
}
