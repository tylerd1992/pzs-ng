#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "zsfunctions.h"
#include "helpfunctions.h"
#include "race-file.h"
#include "objects.h"
#include "macros.h"
#include "multimedia.h"
#include "convert.h"
#include "dizreader.h"
#include "stats.h"
#include "complete.h"
#include "crc.h"
#include "ng-version.h"
#include "audiosort.h"

#include "../conf/zsconfig.h"
#include "../include/zsconfig.defaults.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef HAVE_STRLCPY
# include "strl/strl.h"
#endif

void print_syntax(int chdir_allowed); /* Defined at the bottom of this file. */

int 
main(int argc, char *argv[])
{
	int		k, n, m, l, complete_type = 0, not_allowed = 0, argv_mode = 0;

#ifdef USING_GLFTPD
        int             gnum = 0, unum = 0;
	char		myflags[20];
#endif

	char           *ext, exec[4096], *complete_bar = 0, *inc_point[2];
	unsigned int	crc;
	struct stat	fileinfo;

	uid_t		f_uid;
	gid_t		f_gid;
	double		temp_time = 0;

	DIR		*dir, *parent;
	struct dirent	*dp;
	long		loc;
	time_t		timenow;

	short		rescan_quick = rescan_default_to_quick;
	char		one_name[NAME_MAX];
	char		*temp_p = NULL;
	int		chdir_allowed = 0, argnum = 0;
	GLOBAL		g;

#if ( program_uid > 0 )
	setegid(program_gid);
	seteuid(program_uid);
#endif

	umask(0666 & 000);

	d_log("rescan: PZS-NG (rescan v2) %s debug log.\n", ng_version);
	d_log("rescan: Rescan executed by: (uid/gid) %d/%d\n", geteuid(), getegid());

#ifdef _ALT_MAX
	d_log("rescan: PATH_MAX not found - using predefined settings! Please report to the devs!\n");
#endif

	d_log("rescan: Allocating memory for variables\n");
	g.ui = ng_realloc2(g.ui, sizeof(struct USERINFO *) * 30, 1, 1, 1);
	g.gi = ng_realloc2(g.gi, sizeof(struct GROUPINFO *) * 30, 1, 1, 1);

	bzero(one_name, NAME_MAX);

#ifdef USING_GLFTPD
	if (getenv("FLAGS")) {
		strncpy(myflags, getenv("FLAGS"), sizeof(myflags));
		myflags[sizeof(myflags) - 1] = '\0';
		n = strlen(myflags);
		while (n > 0) {
			n--;
			m = strlen(rescan_chdir_flags);
			while(m > 0) {
				m--;
				if (myflags[n] == rescan_chdir_flags[m])
					chdir_allowed = 1;
			}
		}
	}
	if (!geteuid())
		chdir_allowed = 1;
#endif

        /* With glftpd we can use env vars, rest of the world: commandline. */
#ifndef USING_GLFTPD
        if (argc < 7)
        {
            print_syntax(chdir_allowed);
            return 0;
        }
        argnum = 6;
        
        if (chdir(argv[5]) != 0)
        {
            printf("Could not chdir to <cwd = '%s'>, ftpd agnostic mode: %s\n", argv[5], strerror(errno));
            d_log("Could not chdir to <cwd = '%s'>, ftpd agnostic mode: %s\n", argv[5], strerror(errno));
            return 1;
        }
#else
	argnum = 1;
#endif

	while ((argnum < argc) && argc > 1) {
		if (!strncasecmp(argv[argnum], "--quick", 7))
			rescan_quick = TRUE;
		else if (!strncasecmp(argv[argnum], "--normal", 8))
			rescan_quick = FALSE;
		else if (!strncasecmp(argv[argnum], "--dir=", 6) && (strlen(argv[argnum]) > 7) && chdir_allowed) {
			temp_p = argv[argnum] + 6;
			if ((!matchpath(nocheck_dirs, temp_p)) && (matchpath(zip_dirs, temp_p) || matchpath(sfv_dirs, temp_p)) && !matchpath(group_dirs, temp_p)) {
				if (chdir(temp_p)) {
					d_log("rescan: Failed to chdir() to %s : %s\n", temp_p, strerror(errno));
					not_allowed = 1;
				}
			} else {
				printf("Not allowed to chdir() to %s\n", temp_p);
				return 1;
			}
			printf("PZS-NG Rescan %s: Rescanning %s\n", ng_version, temp_p);
			argv_mode = 1;

		} else if (!strncasecmp(argv[argnum], "--chroot=", 9) && (strlen(argv[argnum]) > 10) && chdir_allowed) {
			if (temp_p == NULL) {
				temp_p = argv[argnum] + 9;
				if (chroot(temp_p) == -1) {
					d_log("rescan: Failed to chroot() to %s : %s\n", temp_p, strerror(errno));
					not_allowed = 1;
				}
			} else {
				temp_p = argv[argnum] + 9;
				printf("Not allowed to chroot() to %s\n", temp_p);
				return 1;
			}
			printf("PZS-NG Rescan %s: Chroot'ing to %s\n", ng_version, temp_p);
			argv_mode = 1;

		} else if (!strncasecmp(argv[argnum], "--help", 6) || !strncasecmp(argv[argnum], "/?", 2) || !strncasecmp(argv[argnum], "--?", 3)) {
                        print_syntax(chdir_allowed);
                        return 0;
		} else {
			strncpy(one_name, argv[argnum], NAME_MAX - 1);
			rescan_quick = FALSE;
			printf("PZS-NG Rescan %s: Rescanning in FILE mode\n", ng_version);
			if (one_name[strlen(one_name) - 1] == '*') {
				one_name[strlen(one_name) - 1] = '\0';
			} else if (!fileexists(one_name)) {
				d_log("PZS-NG Rescan: No file named '%s' exists.\n", one_name);
				one_name[0] = '\0';
			}
			not_allowed = 1;
			argv_mode = 1;
		}		
		argnum++;
	} 
	if (one_name[0] == '\0') {
		if (rescan_quick == TRUE) {
			printf("PZS-NG Rescan %s: Rescanning in QUICK mode.\n", ng_version);
		} else {
			printf("PZS-NG Rescan %s: Rescanning in NORMAL mode.\n", ng_version);
		}
	}
	printf("PZS-NG Rescan %s: Use --help for options.\n\n", ng_version);

	if (not_allowed)
		return 1;

	getcwd(g.l.path, PATH_MAX);

	if (subcomp(g.l.path, g.l.basepath) && (g.l.basepath[0] == '\0'))
		strlcpy(g.l.basepath, g.l.path, PATH_MAX);
	if (strncmp(g.l.path, g.l.basepath, PATH_MAX))
		d_log("rescan: We are in subdir of %s\n", g.l.basepath);
        strncpy(g.v.misc.current_path, g.l.path, sizeof(g.v.misc.current_path));
        strncpy(g.v.misc.basepath, g.l.basepath, sizeof(g.v.misc.basepath));

	if ((matchpath(nocheck_dirs, g.l.path) && !rescan_nocheck_dirs_allowed) || (matchpath(group_dirs, g.l.path) && argv_mode) || (!matchpath(nocheck_dirs, g.l.path) && !matchpath(zip_dirs, g.l.path) && !matchpath(sfv_dirs, g.l.path) && !matchpath(group_dirs, g.l.path)) || insampledir(g.l.path)) {
		d_log("rescan: Dir matched with nocheck_dirs/sample_list, or is not in the zip/sfv/group-dirs.\n");
		d_log("rescan: Freeing memory, and exiting.\n");
		ng_free(g.ui);
		ng_free(g.gi);
		printf("Notice: Unable to rescan this dir - check config.\n\n");
		return 0;
	}
	g.v.misc.slowest_user[0] = 30000;

	bzero(&g.v.total, sizeof(struct race_total));
	g.v.misc.fastest_user[0] = 0;
	g.v.misc.release_type = RTYPE_NULL;

#ifdef USING_GLFTPD
	if (getenv("SECTION") == NULL) {
		sprintf(g.v.sectionname, "DEFAULT");
	} else {
		snprintf(g.v.sectionname, 127, getenv("SECTION"));
	}
#else
        snprintf(g.v.sectionname, 127, argv[4]);
#endif

	g.l.race = ng_realloc2(g.l.race, n = (int)strlen(g.l.path) + 12 + sizeof(storage), 1, 1, 1);
	g.l.sfv = ng_realloc2(g.l.sfv, n, 1, 1, 1);
	g.l.sfvbackup = ng_realloc2(g.l.sfvbackup, n, 1, 1, 1);
	g.l.leader = ng_realloc2(g.l.leader, n, 1, 1, 1);
	g.l.length_path = (int)strlen(g.l.path);
	g.l.length_zipdatadir = sizeof(storage);
	g.l.sfv_incomplete = 0;

	getrelname(&g);

#ifdef USING_GLFTPD
	gnum = buffer_groups(GROUPFILE, 0);
	unum = buffer_users(PASSWDFILE, 0);
#endif

	sprintf(g.l.sfv, storage "/%s/sfvdata", g.l.path);
	sprintf(g.l.sfvbackup, storage "/%s/sfvbackup", g.l.path);
	sprintf(g.l.leader, storage "/%s/leader", g.l.path);
	sprintf(g.l.race, storage "/%s/racedata", g.l.path);
	g.v.id3_artist[0] = '\0';
	g.v.id3_genre[0] = '\0';
	d_log("rescan: Creating directory to store racedata in\n");
 	maketempdir(g.l.path);

	d_log("rescan: Locking release\n");
	while (1) {
		if ((k = create_lock(&g.v, g.l.path, PROGTYPE_RESCAN, 3, 0))) {
			d_log("rescan: Failed to lock release.\n");
			if (k == 1) {
				d_log("rescan: version mismatch. Exiting.\n");
				printf("Error. You need to rm -fR ftp-data/pzs-ng/* before rescan will work.\n");
				exit(EXIT_FAILURE);
			}
			if (k == PROGTYPE_POSTDEL) {
				n = (signed int)g.v.data_incrementor;
				d_log("rescan: Detected postdel running - sleeping for one second.\n");
				if (!create_lock(&g.v, g.l.path, PROGTYPE_RESCAN, 0, g.v.data_queue))
					break;
				usleep(1000000);
				if (!create_lock(&g.v, g.l.path, PROGTYPE_RESCAN, 0, g.v.data_queue))
					break;
				if ( n == (signed int)g.v.data_incrementor) {
					d_log("rescan: Failed to get lock. Forcing unlock.\n");
					if (create_lock(&g.v, g.l.path, PROGTYPE_RESCAN, 2, g.v.data_queue)) {
						d_log("rescan: Failed to force a lock.\n");
						d_log("rescan: Exiting with error.\n");
						exit(EXIT_FAILURE);
					}
					break;
				}
			} else {
				for ( k = 0; k <= max_seconds_wait_for_lock * 10; k++) {
					d_log("rescan: sleeping for .1 second before trying to get a lock (queue: %d).\n", g.v.data_queue);
					usleep(100000);
					if (!create_lock(&g.v, g.l.path, PROGTYPE_RESCAN, 0, g.v.data_queue))
						break;
				}
				if (k >= max_seconds_wait_for_lock * 10) {
					d_log("rescan: Failed to get lock. Will not force unlock.\n");
					exit(EXIT_FAILURE);
				}
			}
		}
		usleep(10000);
		if (update_lock(&g.v, 1, 0) != -1)
			break;
	}

	move_progress_bar(1, &g.v, g.ui, g.gi);
	if (g.l.incomplete)
		unlink(g.l.incomplete);
	if (del_completebar)
		removecomplete();
	
	dir = opendir(".");
	parent = opendir("..");

	if (!((rescan_quick && findfileext(dir, ".sfv")) || *one_name)) {
		if (g.l.sfv)
			unlink(g.l.sfv);
		if (g.l.race)
			unlink(g.l.race);
	}
	printf("Rescanning files...\n");
	
	if (findfileext(dir, ".zip")) {
		strlcpy(g.v.file.name, findfileext(dir, ".zip"), NAME_MAX);
		maketempdir(g.l.path);
		stat(g.v.file.name, &fileinfo);
		//n = direntries;
		crc = 0;
		rewinddir(dir);
		timenow = time(NULL);
		while ((dp = readdir(dir))) {
			m = l = (int)strlen(dp->d_name);
			
			ext = find_last_of(dp->d_name, ".");
			if (*ext == '.')
				ext++;

			if (!strcasecmp(ext, "zip")) {
				stat(dp->d_name, &fileinfo);
				f_uid = fileinfo.st_uid;
				f_gid = fileinfo.st_gid;

				if ((timenow == fileinfo.st_ctime) && (fileinfo.st_mode & 0111)) {
					d_log("rescan.c: Seems this file (%s) is in the process of being uploaded. Ignoring for now.\n", dp->d_name);
					continue;
				}
#ifdef USING_GLFTPD
				strcpy(g.v.user.name, get_u_name(f_uid));
				strcpy(g.v.user.group, get_g_name(f_gid));
#else
				strncpy(g.v.user.name, argv[1], sizeof(g.v.user.name));
				strncpy(g.v.user.group, argv[2], sizeof(g.v.user.group));
#endif
				strlcpy(g.v.file.name, dp->d_name, NAME_MAX);
				g.v.file.speed = 2005 * 1024;
				g.v.file.size = fileinfo.st_size;
				g.v.total.start_time = 0;

				if (!fileexists("file_id.diz")) {
					sprintf(exec, "%s -qqjnCLL \"%s\" file_id.diz 2>.delme", unzip_bin, g.v.file.name);
					if (execute(exec) != 0) {
						d_log("rescan: No file_id.diz found (#%d): %s\n", errno, strerror(errno));
					} else {
						if ((loc = findfile(dir, "file_id.diz.bad"))) {
							seekdir(dir, loc);
							dp = readdir(dir);
							unlink(dp->d_name);
						}
						if (chmod("file_id.diz", 0666))
							d_log("rescan: Failed to chmod %s: %s\n", "file_id.diz", strerror(errno));
					}
				}
				sprintf(exec, "%s -qqt \"%s\" >.delme", unzip_bin, g.v.file.name);
				if (system(exec) == 0) {
					writerace(g.l.race, &g.v, crc, F_CHECKED);
				} else {
					writerace(g.l.race, &g.v, crc, F_BAD);
					if (g.v.file.name)
						unlink(g.v.file.name);
				}
			}
		}
//		g.v.total.files = read_diz("file_id.diz");
		g.v.total.files = read_diz();
		if (!g.v.total.files) {
			g.v.total.files = 1;
			unlink("file_id.diz");
		}
		g.v.total.files_missing = g.v.total.files;
		readrace(g.l.race, &g.v, g.ui, g.gi);
		sortstats(&g.v, g.ui, g.gi);
		if (g.v.total.files_missing < 0) {
			g.v.total.files -= g.v.total.files_missing;
			g.v.total.files_missing = 0;
		}
		buffer_progress_bar(&g.v);
		if (g.v.total.files_missing == 0) {
			complete(&g, complete_type);
			createstatusbar(convert(&g.v, g.ui, g.gi, zip_completebar));
#if (chmod_completebar)
			if (!matchpath(group_dirs, g.l.path)) {
				if (chmod_each(convert(&g.v, g.ui, g.gi, zip_completebar), 0222))
					d_log("rescan: Failed to chmod a statusbar: %s\n", strerror(errno));
			}
#endif

		} else {
			if (!matchpath(group_dirs, g.l.path) || create_incomplete_links_in_group_dirs) {
				create_incomplete();
			}
				move_progress_bar(0, &g.v, g.ui, g.gi);
		}
		if (g.l.nfo_incomplete) {
			if (findfileext(dir, ".nfo")) {
				d_log("rescan: Removing missing-nfo indicator (if any)\n");
				remove_nfo_indicator(&g);
			} else if (matchpath(check_for_missing_nfo_dirs, g.l.path) && (!matchpath(group_dirs, g.l.path) || create_incomplete_links_in_group_dirs)) {
				if (!g.l.in_cd_dir) {
					d_log("rescan: Creating missing-nfo indicator %s.\n", g.l.nfo_incomplete);
					create_incomplete_nfo();
				} else {
					if (findfileextparent(parent, ".nfo")) {
						d_log("rescan: Removing missing-nfo indicator (if any)\n");
						remove_nfo_indicator(&g);
					} else {
						d_log("rescan: Creating missing-nfo indicator (base) %s.\n", g.l.nfo_incomplete);
						create_incomplete_nfo();
					}
				}
			}
		}
	} else if (findfileext(dir, ".sfv") || (create_missing_sfv && file_count(dir))) {
		if (!findfileext(dir, ".sfv") && create_missing_sfv && file_count(dir)) {
			d_log("rescan: No sfv found - creating one.\n");
			make_sfv(g.l.path);
			if (!findfileext(dir, ".sfv")) {
				d_log("rescan: Freeing memory, removing lock and exiting\n");
				unlink(g.l.sfv);
				if (fileexists(g.l.sfvbackup))
				unlink(g.l.sfvbackup);
				unlink(g.l.race);
				ng_free(g.ui);
				ng_free(g.gi);
				ng_free(g.l.race);
				ng_free(g.l.sfv);
				ng_free(g.l.sfvbackup);
				ng_free(g.l.leader);
				remove_lock(&g.v);
				return 0;
			}
		}
#if ( create_missing_sfv_link == TRUE )
		d_log("rescan: Removing missing-sfv indicator (if any)\n");
		unlink(g.l.sfv_incomplete);
#endif
		strlcpy(g.v.file.name, findfileext(dir, ".sfv"), NAME_MAX);

		maketempdir(g.l.path);
		stat(g.v.file.name, &fileinfo);

		if (copysfv(g.v.file.name, g.l.sfv, &g.v)) {
			printf("Found invalid entries in SFV - Exiting.\n");

			while ((dp = readdir(dir))) {
				m = l = (int)strlen(dp->d_name);
				ext = find_last_of(dp->d_name, "-");
				if (!strncmp(ext, "-missing", 8))
					unlink(dp->d_name);
			}

			d_log("rescan: Freeing memory, removing lock and exiting\n");
			unlink(g.l.sfv);
			if (fileexists(g.l.sfvbackup))
			unlink(g.l.sfvbackup);
			unlink(g.l.race);
			ng_free(g.ui);
			ng_free(g.gi);
			ng_free(g.l.race);
			ng_free(g.l.sfv);
			ng_free(g.l.sfvbackup);
			ng_free(g.l.leader);
			
			remove_lock(&g.v);

			return 0;
		}
		g.v.total.start_time = 0;
		rewinddir(dir);
		while ((dp = readdir(dir))) {
			if (*one_name && strncasecmp(one_name, dp->d_name, strlen(one_name)))
				continue;

			m = l = (int)strlen(dp->d_name);

			ext = find_last_of(dp->d_name, ".");
			if (*ext == '.')
				ext++;

			if (!update_lock(&g.v, 1, 0)) {
				d_log("rescan: Another process wants the lock - will comply and remove lock, then exit.\n");
				remove_lock(&g.v);
				exit(EXIT_FAILURE);
			}

			if (
				!strcomp(ignored_types, ext) &&
				(!(strcomp(allowed_types, ext) && !matchpath(allowed_types_exemption_dirs, g.l.path))) &&
				strcasecmp("sfv", ext) &&
				strcasecmp("nfo", ext) &&
				strcasecmp("bad", ext) &&
				strcmp(dp->d_name + l - 8, "-missing") &&
				strncmp(dp->d_name, ".", 1)
				) {
				
				stat(dp->d_name, &fileinfo);

				if (S_ISDIR(fileinfo.st_mode))
					continue;
				if (ignore_zero_sized_on_rescan && !fileinfo.st_size)
					continue;

				f_uid = fileinfo.st_uid;
				f_gid = fileinfo.st_gid;

#ifdef USING_GLFTPD
				strcpy(g.v.user.name, get_u_name(f_uid));
				strcpy(g.v.user.group, get_g_name(f_gid));
#else
				strncpy(g.v.user.name, argv[1], sizeof(g.v.user.name));
				strncpy(g.v.user.group, argv[2], sizeof(g.v.user.group));
#endif

				strlcpy(g.v.file.name, dp->d_name, NAME_MAX);
				g.v.file.speed = 2005 * 1024;
				g.v.file.size = fileinfo.st_size;

				temp_time = fileinfo.st_mtime;
				
				if (g.v.total.start_time == 0)
					g.v.total.start_time = temp_time;
				else
					g.v.total.start_time = (g.v.total.start_time < temp_time ? g.v.total.start_time : temp_time);
				
				g.v.total.stop_time = (temp_time > g.v.total.stop_time ? temp_time : g.v.total.stop_time);

				/* Hide users in group_dirs */
				if (matchpath(group_dirs, g.l.path) && (hide_group_uploaders == TRUE)) {
					d_log("rescan: Hiding user in group-dir:\n");
					if ((int)strlen(hide_gname) > 0) {
						snprintf(g.v.user.group, 18, "%s", hide_gname);
						d_log("rescan:    Changing groupname\n");
					}
					if ((int)strlen(hide_uname) > 0) {
						snprintf(g.v.user.name, 18, "%s", hide_uname);
						d_log("rescan:    Changing username\n");
					}
#if (show_users_in_group_dirs == FALSE)
					if ((int)strlen(hide_uname) == 0) {
						d_log("rescan:    Making username = groupname\n");
						snprintf(g.v.user.name, 18, "%s", g.v.user.group);
					}
#endif
				}

				if (!rescan_quick || (g.l.race && !match_file(g.l.race, dp->d_name)))
					crc = calc_crc32(dp->d_name);
				else
 					crc = 1;

				if (!S_ISDIR(fileinfo.st_mode)) {
					if (g.v.file.name)
						unlink_missing(g.v.file.name);
					if (l > 44) {
						if (crc == 1)
							printf("\nFile: %s CHECKED", dp->d_name + l - 44);
						else
							printf("\nFile: %s %.8x", dp->d_name + l - 44, crc);
					} else {
						if (crc == 1)
							printf("\nFile: %-44s CHECKED", dp->d_name);
						else
							printf("\nFile: %-44s %.8x", dp->d_name, crc);
					}
				}
				if(fflush(stdout))
					d_log("rescan: ERROR: %s\n", strerror(errno));
				if (!rescan_quick || (g.l.race && !match_file(g.l.race,	dp->d_name)) || !fileexists(dp->d_name))
					writerace(g.l.race, &g.v, crc, F_NOTCHECKED);
			}
		}
		printf("\n");
		testfiles(&g.l, &g.v, 1);
		printf("\n");
		readsfv(g.l.sfv, &g.v, 0);
		readrace(g.l.race, &g.v, g.ui, g.gi);
		sortstats(&g.v, g.ui, g.gi);
		buffer_progress_bar(&g.v);

		if (g.l.nfo_incomplete) {
			if (findfileext(dir, ".nfo")) {
				d_log("rescan: Removing missing-nfo indicator (if any)\n");
				remove_nfo_indicator(&g);
			} else if (matchpath(check_for_missing_nfo_dirs, g.l.path) && (!matchpath(group_dirs, g.l.path) || create_incomplete_links_in_group_dirs)) {
				if (!g.l.in_cd_dir) {
					d_log("rescan: Creating missing-nfo indicator %s.\n", g.l.nfo_incomplete);
					create_incomplete_nfo();
				} else {
					if (findfileextparent(parent, ".nfo")) {
						d_log("rescan: Removing missing-nfo indicator (if any)\n");
						remove_nfo_indicator(&g);
					} else {
						d_log("rescan: Creating missing-nfo indicator (base) %s.\n", g.l.nfo_incomplete);

						/* This is not pretty, but should be functional. */
						if ((inc_point[0] = find_last_of(g.l.path, "/")) != g.l.path)
							*inc_point[0] = '\0';
						if ((inc_point[1] = find_last_of(g.v.misc.release_name, "/")) != g.v.misc.release_name)
							*inc_point[1] = '\0';
						create_incomplete_nfo();
						if (*inc_point[0] == '\0')
							*inc_point[0] = '/';
						if (*inc_point[1] == '\0')
							*inc_point[1] = '/';
					}
				}
			}
		}
#if (create_missing_sample_link)
		if (g.l.sample_incomplete) {
			if (findfileextsub(dir)) {
				d_log("rescan: Removing missing-sample indicator (if any)\n");
				remove_sample_indicator(&g);
			} else if (matchpath(check_for_missing_sample_dirs, g.l.path) && (!matchpath(group_dirs, g.l.path) || create_incomplete_links_in_group_dirs)) {
				if (!g.l.in_cd_dir) {
					d_log("rescan: Creating missing-sample indicator %s.\n", g.l.sample_incomplete);
					create_incomplete_sample();
				} else {
					if (findfileextsubp(dir)) {
						d_log("rescan: Removing missing-sample indicator (if any)\n");
						remove_sample_indicator(&g);
					} else {
						d_log("rescan: Creating missing-sample indicator (base) %s.\n", g.l.sample_incomplete);

						/* This is not pretty, but should be functional. */
						if ((inc_point[0] = find_last_of(g.l.path, "/")) != g.l.path)
							*inc_point[0] = '\0';
						if ((inc_point[1] = find_last_of(g.v.misc.release_name, "/")) != g.v.misc.release_name)
							*inc_point[1] = '\0';
						create_incomplete_sample();
						if (*inc_point[0] == '\0')
							*inc_point[0] = '/';
						if (*inc_point[1] == '\0')
							*inc_point[1] = '/';
					}
				}
			}
		}
#endif
		if (g.v.misc.release_type == RTYPE_AUDIO) {
			get_mpeg_audio_info(findfileext(dir, ".mp3"), &g.v.audio);
			strlcpy(g.v.id3_artist, g.v.audio.id3_artist, 31);
			strlcpy(g.v.id3_genre, g.v.audio.id3_genre, 31);
		}
		if ((g.v.total.files_missing == 0) && (g.v.total.files > 0)) {

			switch (g.v.misc.release_type) {
			case RTYPE_RAR:
				complete_bar = rar_completebar;
				break;
			case RTYPE_OTHER:
				complete_bar = other_completebar;
				break;
			case RTYPE_AUDIO:
				complete_bar = audio_completebar;
#if ( create_m3u == TRUE )
				n = sprintf(exec, findfileext(dir, ".sfv"));
				strcpy(exec + n - 3, "m3u");
				create_indexfile(g.l.race, &g.v, exec);
#endif

				/* Sort if we're not in a group-dir/nosort-dir. */
				if (!matchpath(group_dirs, g.l.path) && !matchpath(audio_nosort_dirs, g.l.path))
                                {
                                    printf(" Resorting release.\n");
                                    audioSort(&g.v.audio, g.l.link_source, g.l.link_target);
                                }
				break;
			case RTYPE_VIDEO:
				complete_bar = video_completebar;
				break;
			}
			complete(&g, complete_type);

			if (complete_bar) {
				createstatusbar(convert(&g.v, g.ui, g.gi, complete_bar));
#if (chmod_completebar)
				if (!matchpath(group_dirs, g.l.path)) {
                                        if (chmod_each(convert(&g.v, g.ui, g.gi, complete_bar), 0222))
                                                d_log("rescan: Failed to chmod a statusbar: %s\n", strerror(errno));
				}
#endif
			}
		} else {
			if (!matchpath(group_dirs, g.l.path) || create_incomplete_links_in_group_dirs) {
				create_incomplete();
			}
				move_progress_bar(0, &g.v, g.ui, g.gi);
		}
	} else if (mark_empty_dirs_as_incomplete_on_rescan) {
			create_incomplete();
			printf(" Empty dir - marking as incomplete.\n");
	}
	printf(" Passed : %i\n", (int)g.v.total.files - (int)g.v.total.files_missing);
	printf(" Failed : %i\n", (int)g.v.total.files_bad);
	printf(" Missing: %i\n", (int)g.v.total.files_missing);
	printf("  Total : %i\n", (int)g.v.total.files);

	d_log("rescan: Freeing memory and removing lock.\n");
	closedir(dir);
	closedir(parent);
	updatestats_free(&g);
	ng_free(g.l.race);
	ng_free(g.l.sfv);
	ng_free(g.l.sfvbackup);
	ng_free(g.l.leader);

	if (fileexists(".delme"))
		unlink(".delme");

	remove_lock(&g.v);

#ifdef USING_GLFTPD
	buffer_groups(GROUPFILE, gnum);
	buffer_users(PASSWDFILE, unum);
#endif

	exit(0);
}

void print_syntax(int chdir_allowed)
{
    printf("PZS-NG Rescan %s options:\n\n", ng_version);
#ifndef USING_GLFTPD
    printf("  [non-glftpd] The first 4 arguments must be: <user> <group> <tagline> <section> <current working dir>, after that - normal options (or none)\n");
#endif
    printf("  --quick         - scan in quick mode - only files not previously marked as ok by the zipscript is scanned\n");
    printf("  --normal        - scan in normal mode - all files will be rescanned regardless of their status\n");
    if (chdir_allowed)
        printf("  --chroot=<PATH> - chroot to PATH before beginning to rescan.\n");
    printf("  --dir=<PATH>    - cd to (chroot'ed) PATH before beginning to rescan.\n");
    printf("  <FILE><*>    - scan only file named FILE or files beginning with FILE*.\n\n");
}
