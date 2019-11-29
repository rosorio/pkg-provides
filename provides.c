/*-
 * Copyright (c) 2017 Rodrigo Osorio <rodrigo@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sysexits.h>
#include <unistd.h>
#include <pkg.h>
#include <fetch.h>
#include <errno.h>
#include <strings.h>
#include <archive.h>
#include <fcntl.h>
#include <string.h>
#include <archive_entry.h>
#include <pcre.h>
#include <libgen.h>
#include <sys/sysctl.h>
#include <sys/queue.h>

static char myname[] = "provides";
static char myversion[] = "0.6.1";
static char dbversion[] = "v3";
static char mydescription[] = "A plugin for querying which package provides a particular file";
static struct pkg_plugin *self;
bool force_flag = false;

bool fetch_on_update = true;

void provides_progressbar_start(const char *pmsg);
void provides_progressbar_stop(void);
void provides_progressbar_tick(int64_t current, int64_t total);
int mkpath(char *path);
int bigram_expand(FILE *fp, void (*match_cb)(const char *,struct search_t *), void *extra);

int config_fetch_on_update();
char * config_get_remote_url();


#define BUFLEN 4096
#define MAX_FN_SIZE 255
#define PKG_DB_PATH "/var/db/pkg/provides/"

typedef struct file_t {
    char *name;
    SLIST_ENTRY (file_t) next;
} file_t;
SLIST_HEAD (file_head_t, file_t);

typedef struct fpkg_t {
    char *pkg_name;
    struct file_head_t files;
    SLIST_ENTRY (fpkg_t) next;
} fpkg_t;
SLIST_HEAD (pkg_head_t, fpkg_t);

struct search_t {
    struct pkg_head_t head;
    pcre *pcre;
    pcre_extra *pcreExtra;
    fpkg_t *pnode;
    char * pattern;
};

int
pkg_plugin_shutdown(struct pkg_plugin *p __unused)
{
    /* nothing to be done here */
    return (EPKG_OK);
}

void
plugin_provides_usage(void)
{
    fprintf(stderr, "usage: pkg %s [-uf] pattern\n\n", myname);
    fprintf(stderr, "%s\n", mydescription);
}

int get_filepath(char *filename, size_t size)
{
    static char osver[1024];
    static char arch[1024];
    static char osname[1024];
    int mib_arch[] = { (CTL_HW), (HW_MACHINE_ARCH) };
    int mib_ver[] = { (KERN_OSTYPE), (KERN_OSRELEASE) };
    int mib_os[] = { (KERN_OSTYPE), (KERN_OSTYPE) };
    size_t len;
    char * ptr;
#ifdef __FreeBSD__
    char sep[] = ".";
#else
    char sep[] = "-";
#endif


    len = sizeof osname;
    if (sysctl(mib_os, 2, &osname, &len, NULL, 0) == -1) {
        return -1;
    }

    len = sizeof osver;
    if (sysctl(mib_ver, 2, &osver, &len, NULL, 0) == -1) {
        return (-1);
    }

    ptr = strstr(osver, sep);
    if (ptr == NULL) {
        return (-1);
    } else {
        ptr[0] = '\0';
    }

    len = sizeof arch;
    if (sysctl(mib_arch, 2, &arch, &len, NULL, 0) == -1) {
        return -1;
    }
#ifndef __FreeBSD__
    if ((ptr = strstr(arch, "_")) != NULL) {
        ptr[0] = ':';
    }
#endif

    if(snprintf(filename, size, "%s/%s:%s",osname,osver, arch) < 0) {
        return -1;
    }

    return (0);
}

int
plugin_archive_extract(int fd, const char *out)
{

    struct archive_entry *ae = NULL;
    struct archive *ar = NULL;
    int fd_out = -1;

    fd_out = open(out, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if ( fd_out < 0) {
        return -1;
    }

    ar = archive_read_new();
    if (ar == NULL) {
        goto error;
    }

    archive_read_support_compression_all(ar);
    archive_read_support_format_raw(ar);

    archive_read_open_fd(ar, fd, BUFLEN);

    if (archive_read_next_header(ar, &ae) == ARCHIVE_OK) {
        if (archive_read_data_into_fd(ar, fd_out) != 0) {
            fprintf(stderr,"archive_read_data failed\n");
            goto error;
        }
    }
    archive_read_free(ar);
    close(fd_out);
    return (0);

error:
    if (fd_out) {
        close(fd);
        unlink(out);
    }
    return -1;
}

int
plugin_fetch_file(void)
{
    char buffer[BUFLEN];
    FILE *fi;
    int fo, ft;
    int count;
    struct url_stat us;
    int64_t size = 0;
    char tmpfile[] = "/var/tmp/pkg-provides-XXXX";
    struct stat sb;
    char path[] =PKG_DB_PATH;
    char filepath[MAX_FN_SIZE + 1];
    char url[MAXPATHLEN];

    if(get_filepath(filepath, MAX_FN_SIZE) != 0) {
        fprintf(stderr,"Can't get the OS ABI\n");
        return (-1);
    }

    sprintf(url, "%s/%s/%s/provides.db.xz", config_get_remote_url(), dbversion, filepath);
    ft = open( PKG_DB_PATH "provides.db", O_WRONLY);
    if (ft < 0) {
        if (errno == ENOENT) {
            if (mkpath(path) == 0) {
                ft = open(PKG_DB_PATH "provides.db", O_RDWR | O_CREAT);
	        }
        }
        if (ft < 0) {
            fprintf(stderr,"Insufficient privileges to update the provides database.\n");
            return (-1);
        }
        unlink(PKG_DB_PATH "provides.db");
    } else {
        if(fstat(ft, &sb) < 0) {
            fprintf(stderr,"fstat error\n");
            close(ft);
            return (-1);
        }
        close(ft);
        if(fetchStatURL(url, &us, "") != 0) {
            fprintf(stderr,"fetchStatURL error : %s\n",url);
            return -1;
        }
        if(us.mtime < sb.st_mtim.tv_sec && (!force_flag)) {
            printf("The provides database is up-to-date.\n");
            return (0);
        }
    }
    close(ft);

    fo = mkstemp(tmpfile);
    if(fo < 0) {
        goto error;
    }

    fi = fetchXGetURL(url, &us, "");
    if (fi == NULL) {
        goto error;
    }

    provides_progressbar_start("Fetching provides database");
    provides_progressbar_tick(size,us.size);
    while ((count = fread(buffer, 1, BUFLEN, fi)) > 0) {
        if(write(fo, buffer, count) != count) {
            goto error;
        }
        size += count;
        provides_progressbar_tick(size,us.size);
    }

    if (!feof(fi)) {
        goto error;
    }

    printf("Extracting database....");
    fflush(stdout);

    lseek(fo, SEEK_SET, 0);
    if (plugin_archive_extract(fo, PKG_DB_PATH "provides.db") != 0 ) {
        printf("fail\n");
        goto error;
    }
    lchmod(PKG_DB_PATH "provides.db",S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    printf("success\n");

    fclose(fi);
    close(fo);

    return EPKG_OK;

error:
    if (fi != NULL) {
        fclose(fi);
        provides_progressbar_stop();
    }

    if (fo >= 0) {
        close(fo);
        unlink(tmpfile);
    }

    return (-1);
}

static void
free_list(struct pkg_head_t *head) {
    fpkg_t *pnode, *prev_n;
    file_t *pfile, *prev_f;

    prev_n = NULL;
    SLIST_FOREACH(pnode,head, next) {
        if (prev_n) {
            free(prev_n);
        }
        prev_f = NULL;
        SLIST_FOREACH(pfile,&(pnode->files), next) {
            if (prev_f) {
                free(prev_f);
            }
            free(pfile->name);
            prev_f = pfile;
        }
        free(pnode->pkg_name);
        prev_n = pnode;
    }

}

static int
display_per_repo(char *repo_name, struct pkg_head_t *head)
{
    struct pkgdb_it *it;
    struct pkg *pkg = NULL;
    const char *name, *version, *comment;
    struct pkg_repo *r = NULL;
    struct pkgdb *db = NULL;
    fpkg_t *pnode;
    file_t *pfile;

    int ret ;

    if (pkgdb_open_all(&db, PKGDB_REMOTE,repo_name) != EPKG_OK) {
        fprintf(stderr, "Can't open %s database\n",repo_name);
        return -1;
    }

    SLIST_FOREACH(pnode,head, next) {
        it = pkgdb_repo_query(db,pnode->pkg_name,MATCH_EXACT,repo_name);
        if (it == NULL) {
            continue;
        }

        if ((ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) != EPKG_OK) {
            pkgdb_it_free(it);
            continue;
        }

        pkg_get(pkg, PKG_NAME, &name, PKG_VERSION, &version, PKG_COMMENT, &comment);
        printf("Name    : %s-%s\n", name, version);
        printf("Desc    : %s\n", comment);
        printf("Repo    : %s\n", repo_name);


        SLIST_FOREACH(pfile,&(pnode->files), next) {
            if(SLIST_FIRST(&(pnode->files)) == pfile) {
                printf("Filename: %s\n",pfile->name);
            } else {
                printf("          %s\n",pfile->name);
            }
        }
        if(SLIST_NEXT(pnode, next) != NULL) {
            printf("\n");
        }
        pkgdb_it_free(it);
    }
    pkgdb_close(db);

    return (0);
}

void
match_cb(const char * line, struct search_t *search)
{
    file_t *pfile;
    fpkg_t *pnode;

    char *separator = strstr(line,"*");
    if(separator != NULL) {
        char *exp;
        char * fullpath = separator + 1;

        if(strstr(search->pattern,"/")) {
            exp = fullpath;
        } else {
            exp = basename(fullpath);
        }

        if (pcre_exec(search->pcre, search->pcreExtra, exp, strlen(exp), 0, 0, NULL, 0) >= 0) {
            int found = 0;
            char * name = strndup(line, (separator - line + 1));
            name[separator - line] = '\0';
            SLIST_FOREACH(pnode,&(search->head), next) {
                if(strcmp(pnode->pkg_name, name)==0) {
                    found = 1;
                    break;
                }
            }

            if (found == 0) {
                pnode = malloc (sizeof(struct fpkg_t));
                if (pnode == NULL) {
                    exit(ENOMEM);
                } else {
                    pnode->pkg_name = name;
                    if(pnode->pkg_name == NULL) {
                        exit(ENOMEM);
                    }
                    SLIST_INIT (&(pnode->files));
                }
                SLIST_INSERT_HEAD(&(search->head),pnode,next);
            } else {
                free(name);
            }
            pfile = malloc(sizeof(struct file_t));
            if(pfile == NULL) {
                exit(ENOMEM);
            }
            pfile->name = strdup(fullpath + 1);
            if(pfile->name == NULL) {
                exit(ENOMEM);
            }
            SLIST_INSERT_HEAD(&pnode->files,pfile,next);
        }
    }
}

int
plugin_provides_search(char *pattern)
{
    FILE *fh;
    int pcreErrorOffset;
    const char *pcreErrorStr;
    char *repo_name;
    struct pkg_repo *r = NULL;

    struct search_t search;

    search.pattern = pattern;

    SLIST_INIT (&search.head);

    fh = fopen(PKG_DB_PATH "provides.db","r");
    if (fh == NULL) {
        fprintf(stderr, "Provides database not found, please update first.\n");
        return (-1);
    }

    search.pcre = pcre_compile(pattern, 0, &pcreErrorStr, &pcreErrorOffset, NULL);

    if(search.pcre == NULL) {
        fprintf(stderr, "Invalid search pattern\n");
        goto error_pcre;
    }

    search.pcreExtra = pcre_study(search.pcre, 0, &pcreErrorStr);

    if(search.pcreExtra == NULL) {
        fprintf(stderr, "Invalid search pattern\n");
        goto error_pcre;
    }

    if (bigram_expand(fh, &match_cb, &search) == -1) {
        fprintf(stderr, "Corrupted database\n");
    }

    while (pkg_repos(&r) == EPKG_OK) {
        if (pkg_repo_enabled(r)) {
            repo_name = (char *)pkg_repo_name(r);
            display_per_repo(repo_name, &search.head);
        }
    }

    fclose(fh);
    pcre_free(search.pcre);
    pcre_free(search.pcreExtra);
    free_list(&search.head);

error_pcre:
    fclose(fh);
    if (search.pcre != NULL) {
        pcre_free(search.pcre);
    }
    if (search.pcreExtra != NULL) {
        pcre_free(search.pcreExtra);
    }
    return (-1);
}

int cb_event(void *data, struct pkgdb *db) {
    struct pkg_event *ev = data;
    if (ev->type == PKG_EVENT_INCREMENTAL_UPDATE && fetch_on_update) {
        plugin_fetch_file();
    }
    return (EPKG_OK);
}

int
plugin_provides_callback(int argc, char **argv)
{
    char ch;
    bool do_update = false;

    while ((ch = getopt(argc, argv, "uf")) != -1) {
        switch (ch) {
        case 'u':
            do_update = true;
            break;
        case 'f':
            force_flag = true;
            break;
        default:
            plugin_provides_usage();
            return (EX_USAGE);
            break;
        }
    }

    if (do_update) {
        return plugin_fetch_file();
    }

    argc -= optind;
    argv += optind;

    if (argc <= 0) {
        plugin_provides_usage();
        return (EX_USAGE);
    }

    plugin_provides_search(argv[0]);

    return (EPKG_OK);
}

int
pkg_plugin_init(struct pkg_plugin *p)
{
    self = p;

    pkg_plugin_set(p, PKG_PLUGIN_NAME, myname);
    pkg_plugin_set(p, PKG_PLUGIN_VERSION, myversion);
    pkg_plugin_set(p, PKG_PLUGIN_DESC, mydescription);

    pkg_plugin_hook_register(p, PKG_PLUGIN_HOOK_EVENT, cb_event);

    fetch_on_update = config_fetch_on_update() ? true : false;

    return (EPKG_OK);
}


int
pkg_register_cmd(int idx, const char **name, const char **desc, int (**exec)(int argc, char **argv))
{
    *name = myname;
    *desc = mydescription;
    *exec = plugin_provides_callback;

    return (EPKG_OK);
}

int
pkg_register_cmd_count (void)
{
    return 1;
}
