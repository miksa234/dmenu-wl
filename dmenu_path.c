/* See LICENSE file for copyright and license details. */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void die (const char *s);
static void printcache (void);
static int qstrcmp (const void *a, const void *b);
static void scan (void);
static int uptodate (void);

static char **items = NULL;
static char cachepath[PATH_MAX];
static char cachedirbuf[PATH_MAX];
static const char *path;

int
main (void)
{
    const char *cachedir = getenv ("XDG_CACHE_HOME");
    if (!cachedir || !*cachedir) {
        const char *home = getenv ("HOME");
        if (!home)
            die ("no $HOME");
        if (snprintf (cachedirbuf, sizeof cachedirbuf, "%s/.cache", home)
            >= (int)sizeof cachedirbuf)
            die ("cache path too long");
        cachedir = cachedirbuf;
    }
    if (!(path = getenv ("PATH")))
        die ("no $PATH");
    if (mkdir (cachedir, 0700) < 0 && errno != EEXIST)
        die ("cannot create cache directory");
    if (snprintf (cachepath, sizeof cachepath, "%s/dmenu_run", cachedir)
        >= (int)sizeof cachepath)
        die ("cache path too long");
    if (uptodate ()) {
        printcache ();
        return EXIT_SUCCESS;
    }
    scan ();
    return EXIT_SUCCESS;
}

void
printcache (void)
{
    char buf[BUFSIZ];
    FILE *cache = fopen (cachepath, "r");
    if (!cache)
        die ("cannot read cache");
    while (fgets (buf, sizeof buf, cache))
        fputs (buf, stdout);
    if (ferror (cache))
        die ("cannot read cache");
    fclose (cache);
}

void
die (const char *s)
{
    fprintf (stderr, "dmenu_path: %s\n", s);
    exit (EXIT_FAILURE);
}

int
qstrcmp (const void *a, const void *b)
{
    return strcmp (*(const char **)a, *(const char **)b);
}

void
scan (void)
{
    char buf[PATH_MAX];
    char *dir, *p;
    size_t i, count;
    struct dirent *ent;
    struct stat st;
    DIR *dp;
    FILE *cache;

    count = 0;
    if (!(p = strdup (path)))
        die ("strdup failed");
    for (dir = strtok (p, ":"); dir; dir = strtok (NULL, ":")) {
        if (!(dp = opendir (dir)))
            continue;
        while ((ent = readdir (dp))) {
            if (snprintf (buf, sizeof buf, "%s/%s", dir, ent->d_name)
                    >= (int)sizeof buf
                || ent->d_name[0] == '.' || stat (buf, &st) < 0
                || !S_ISREG (st.st_mode) || access (buf, X_OK) < 0)
                continue;
            if (!(items = realloc (items, ++count * sizeof *items)))
                die ("malloc failed");
            if (!(items[count - 1] = strdup (ent->d_name)))
                die ("strdup failed");
        }
        closedir (dp);
    }
    qsort (items, count, sizeof *items, qstrcmp);
    if (!(cache = fopen (cachepath, "w")))
        die ("open failed");
    for (i = 0; i < count; i++) {
        if (i > 0 && !strcmp (items[i], items[i - 1]))
            continue;
        fprintf (cache, "%s\n", items[i]);
        fprintf (stdout, "%s\n", items[i]);
    }
    fclose (cache);
    free (p);
}

int
uptodate (void)
{
    char *dir, *p;
    time_t mtime;
    struct stat st;

    if (stat (cachepath, &st) < 0)
        return 0;
    mtime = st.st_mtime;
    if (!(p = strdup (path)))
        die ("strdup failed");
    for (dir = strtok (p, ":"); dir; dir = strtok (NULL, ":"))
        if (!stat (dir, &st) && st.st_mtime > mtime) {
            free (p);
            return 0;
        }
    free (p);
    return 1;
}
