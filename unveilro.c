/*
 * Copyright (c) 2019 Bryan Steele <brynet@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define DEBUG
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h> // for mode_t/mkdir

#include <util.h>    // for fparseln

int	(*orig_atexit)(void (*)(void));
int	(*orig_mkdir)(const char *, mode_t);
void	parseunveil(const char *);

int	has_setup = 0;
int	quirks_mkdir_home = 0;

int
main(int argc, char **argv)
{
	int nargc, i, ret;
	char **nargv;
	char *home = NULL;
	char *preload_path = NULL;
	struct stat sb;

	if (geteuid() == 0 || issetugid()) {
		warnx("Do not run as root.");
		goto fatal;
	}
	if (argc < 2) {
		warnx("usage: unveilro cmd ...");
		goto fatal;
	}
	if (strcmp(argv[0], "unveilro") != 0)
		goto fatal;
	if (pledge("stdio rpath exec", NULL) == -1)
		goto fatal;
	if ((nargv = reallocarray(NULL, argc - 1,
	    sizeof(char *))) == NULL) {
		warn("reallocarray");
		goto fatal;
	}
	nargc = 0;
	nargv[nargc++] = argv[1];
	for (i = 2; nargc < argc-1;)
		nargv[nargc++] = argv[i++];
	nargv[nargc] = NULL;
	home = getenv("HOME");
	if (home == NULL) {
		warn("getenv");
		goto fatal;
	}
	if (asprintf(&preload_path, "%s/bin/unveilro", home) == -1)
		goto fatal;
	ret = stat(preload_path, &sb);
	if (ret == -1 || (ret == 0 && S_ISREG(sb.st_mode) == 0)) {
		warnx("'unveilro' must be in $HOME/bin: '%s' %d",
		    preload_path, errno);
		goto fatal;
	}

	/*
	 * What's happening here is that we're preloading ourself into
	 * the target program, by attempting to hook functions called
	 * early, we can establish our imposed default unveil.
	 */
	/* XXX: This hack works because of default PIE :-) */
	setenv("LD_PRELOAD", preload_path, 1);

	if (execvp(nargv[0], nargv) == -1)
		warn("execvp '%s'", nargv[0]);
fatal:
	return 1;
}

#if notyet
struct safe_devs {
	const char *const dev;
	const char *const permissions;
} allowed_devices[] = {
	{"/dev/uhid0", "rw"},
	{"/dev/uhid1", "rw"},
	{"/dev/uhid2", "rw"},
	{"/dev/uhid3", "rw"},
	/* XXX: Add more? i386 may need legacy /dev/joy[0-3] */
	{   "/dev/fd", "rw"},
	{ "/dev/drm0", "rw"},
	{"/dev/drmR128", "rw"},
	{ "/dev/drm1", "rw"},
	{"/dev/drmR129", "rw"},
	{ "/dev/drm2", "rw"},
	{"/dev/drmR130", "rw"},
	{ "/dev/drm3", "rw"},
	{"/dev/drmR131", "rw"},
	{ "/dev/zero", "rw"},
	{ "/dev/null", "rw"},
	{"/dev/urandom", "r"},
	{NULL, NULL}
};
#endif

/*
 * XXX: This is necessary to workaround some unhelpful semantics that some
 * applications have (ioquake3/chocolatedoom), where they attempt to create
 * each path element.. robert@ fixed this in chrome and gtk3, but, well..
 */
/* XXX: mkdirat? */
int
mkdir(const char *path, mode_t mode)
{
	int ret;

	orig_mkdir = dlsym(RTLD_NEXT, "mkdir");
	if (orig_mkdir == NULL)
		_exit(1);
	ret = orig_mkdir(path, mode);
	if (ret == -1 && errno == ENOENT && quirks_mkdir_home) {
#ifndef _PATH_HOME
#define _PATH_HOME "/home"
#endif
		size_t len = strlen(_PATH_HOME);
		if (path && (strlen(path) >= len) &&
		    (strncmp(_PATH_HOME, path, len) == 0)) {
			errno = EEXIST;
			return -1;
		}
	}
	/* mkdir(2) errno not clobbered */

	return (ret);
}

int
atexit(void (*function)(void))
{
	int ret, save_errno, isunveilro;
	const char *progname;

	orig_atexit = dlsym(RTLD_NEXT, "atexit");
	if (orig_atexit == NULL)
		_exit(1);
	ret = orig_atexit(function);
	/* We cannot avoid clobbering errno, so save it */
	save_errno = errno;
	if (ret < 0 || has_setup)
		goto atexit_native;
	progname = getprogname();
	isunveilro = (strcmp(progname, "unveilro") == 0);
	/* Default read-only hierarchy, exec only if unveilro itself */
	if (unveil("/", isunveilro ? "rx" : "r") == -1)
		_exit(1);
	if (isunveilro == 0) {
		/* Some exceptions required by typical programs */
#if notyet
		/* XXX: This might be needed some day, but not yet.. */
		struct safe_devs *p;
		for (p = allowed_devices; p->dev != NULL; p++) {
			/* unveil(p->dev, p->permissions) */
		}
#endif
		/* This is safe, file perms prevent bad stuff .. */
		if (unveil("/dev", "rw") == -1) /* fd, ptm, null, tty */
			_exit(1);
		if (unveil("/tmp", "rwc") == -1)
			_exit(1);

#if 0
		/* XXX: getcwd unveil(2) bug again? mono games */
		if (unveil(".", "r") == -1)
			_exit(1);
#endif

		/* Any custom unveil overrides */
		parseunveil(progname);
	}
	/* Lock unveil */
	(void) unveil(NULL, NULL);
	has_setup = 1;

atexit_native:
	errno = save_errno;
	return ret;
}

/* unveil config file parsing from Robert Nagy's chromium patches. */
#define	MAXTOKENS	3
void
parseunveil(const char *progname)
{
	size_t len = 0, lineno = 0;
	char *s = NULL, *cp = NULL, *tokens[MAXTOKENS];
	char *ufile, *home;
	FILE *fp;

	home = getenv("HOME");
	if (home == NULL)
		return;
	if (asprintf(&ufile, "%s/.config/unveilro/%s.unveil", home,
	    progname) == -1)
		return;

	fp = fopen(ufile, "re");
	if (fp != NULL) {
		char path[PATH_MAX];
		char **ap;
		while (!feof(fp)) {
			if ((s = fparseln(fp, &len, &lineno, NULL,
			    FPARSELN_UNESCCOMM | FPARSELN_UNESCCONT)) == NULL) {
				if (ferror(fp)) {
					fclose(fp);
					return;
				} else
					continue;
			}
			cp = s;
			cp += strspn(cp, " \t\n"); /* eat whitespace */
			if (cp[0] == '\0')
				continue;
			for (ap = tokens; ap < &tokens[MAXTOKENS - 1] &&
			    (*ap = strsep(&cp, " \t")) != NULL;) {
				if (**ap != '\0')
					ap++;
			}
			*ap = NULL;
			if (!*tokens)
				continue;
			if (tokens[0][0] == '~') {
				if (*home == '\0') {
					fclose(fp);
					return;
				}
				memmove(tokens[0], tokens[0] + 1,
				    strlen(tokens[0]));
				strncpy(path, home, sizeof(path) - 1);
				path[sizeof(path) - 1] = '\0';
				strncat(path, tokens[0],
				    sizeof(path) - 1 - strlen(path));
			} else {
				strncpy(path, tokens[0], sizeof(path) - 1);
				path[sizeof(path) - 1] = '\0';
			}
			int quirks = 0;
			if (strcmp("quirks", path) == 0) {
				if (strcmp("mkdir_home", tokens[1]) == 0) {
#ifdef DEBUG
					fprintf(stderr,
					    "mkdir_home quirk found\n");
					quirks_mkdir_home = 1;
#endif
				}
				if (quirks_mkdir_home)
					quirks = 1;
			}
			int isnoperm = (strcmp("noperm", tokens[1]) == 0);
			if (quirks == 0 &&
			    unveil(path, isnoperm ? "" : tokens[1]) == -1) {
				fclose(fp);
				return;
			}
#ifdef DEBUG
			else if (quirks == 0)
				fprintf(stderr, "unveiling %s with %s\n", path,
				    tokens[1]);
#endif
		}
		fclose(fp);
	}
#ifdef DEBUG
	else if (fp == NULL) {
		warn("fopen %s", ufile);
		/* not fatal */
	}
#endif
}
