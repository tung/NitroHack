/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* DynaHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "date.h"
/*
 * All the references to the contents of patchlevel.h have been moved
 * into makedefs....
 */
#include "patchlevel.h"

/* #define BETA_INFO "" */	/* "[ beta n]" */

static char *getversionstring(char *);


/* fill buffer with short version (so caller can avoid including date.h) */
char *version_string(char *buf)
{
	return strcpy(buf, VERSION_STRING);
}

/* fill and return the given buffer with the long nethack version string */
char *getversionstring(char *buf)
{
	strcpy(buf, VERSION_ID);
#if defined(BETA) && defined(BETA_INFO)
	sprintf(eos(buf), " %s", BETA_INFO);
#endif
	return buf;
}

int doversion(void)
{
	char buf[BUFSZ];

	pline("%s", getversionstring(buf));
	return 0;
}


boolean check_version(struct version_info *version_data, const char *filename,
		      boolean complain)
{
	if (
#ifdef VERSION_COMPATIBILITY
	    version_data->incarnation < VERSION_COMPATIBILITY ||
	    version_data->incarnation > VERSION_NUMBER
#else
	    version_data->incarnation != VERSION_NUMBER
#endif
	  ) {
	    if (complain)
		pline("Version mismatch for file \"%s\".", filename);
	    return FALSE;
	} else if (
#ifndef IGNORED_FEATURES
		   version_data->feature_set != VERSION_FEATURES ||
#else
		   (version_data->feature_set & ~IGNORED_FEATURES) !=
			  (VERSION_FEATURES & ~IGNORED_FEATURES) ||
#endif
		   version_data->entity_count != VERSION_SANITY1) {
	    if (complain)
		pline("Configuration incompatibility for file \"%s\".",
		      filename);
	    return FALSE;
	}
	return TRUE;
}

/* this used to be based on file date and somewhat OS-dependant,
   but now examines the initial part of the file's contents */
boolean uptodate(struct memfile *mf, const char *name)
{
    struct version_info vers_info;
    boolean verbose = name ? TRUE : FALSE;

    vers_info.incarnation = mread32(mf);
    vers_info.feature_set = mread32(mf);
    vers_info.entity_count = mread32(mf);
    if (!check_version(&vers_info, name, verbose))
	return FALSE;
    
    return TRUE;
}

void store_version(struct memfile *mf)
{
	mtag(mf, 0, MTAG_VERSION);
	mwrite32(mf, VERSION_NUMBER);
	mwrite32(mf, VERSION_FEATURES);
	mwrite32(mf, VERSION_SANITY1);
}


/*version.c*/
