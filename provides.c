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
#include <archive.h>
#include <fcntl.h>
#include <string.h>
#include <archive_entry.h>
#include <pcre.h>
#include <libgen.h>
#include <sys/queue.h>

static char myname[] = "provides";
static char myversion[] = "0.1.0";
static char mydescription[] = "A plugin for querying which package provides a particular file";
static struct pkg_plugin *self;
bool force_flag = false;

void provides_progressbar_start(const char *pmsg);
void provides_progressbar_stop(void);
void provides_progressbar_tick(int64_t current, int64_t total);
int mkpath(char *path);


#define BUFLEN 4096
#define MAX_FN_SIZE 255
#define PKG_DB_PATH "/var/db/pkg/provides/"
#define PKG_DB_URL  "http://pkg-provides.osorio.me/"

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

int
pkg_plugin_shutdown(struct pkg_plugin *p __unused)
{
    /* nothing to be done here */
    return (EPKG_OK);
}

void
plugin_provides_usage(void)
{
    fprintf(stderr, "usage: pkg %s [-u]\n\n", myname);
    fprintf(stderr, "%s\n", mydescription);
}

int get_filename(char *filename)
{
    char arch_full[256];
    char *word, *brkt;
    char *sep = ":";
    int counter = 0;

    if (pkg_get_myarch(arch_full,256) != EPKG_OK) {
        return (-1);
    }

    strcpy(filename,"pkg:");
    for (word = strtok_r(arch_full, sep, &brkt);
         word;
         word = strtok_r(NULL, sep, &brkt)) {
        switch (counter) {
        case 2:
            strcat(filename, ":");
        case 1:
            strcat(filename, word);
            break;
        }
        counter++;
    }
    strcat(filename,".db");

    return (0);
}

int
plugin_archive_extract(int fd, const char *out)
{

    struct archive_entry *ae = NULL;
    struct archive *ar = NULL;
    int fd_out = -1;

    fd_out = open(out, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
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
    char filename[MAX_FN_SIZE];
    char url[MAXPATHLEN];

    if(get_filename(filename) != 0) {
        fprintf(stderr,"Can't get the OS ABI\n");
        return (-1);
    }

    sprintf(url, "%s%s.xz", PKG_DB_URL, filename);
    ft = open( PKG_DB_PATH "provides.db", O_RDWR);
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
            fprintf(stderr,"fetchStatURL error\n");
            return -1;
        }
        if(us.mtime < sb.st_mtim.tv_sec && (!force_flag)) {
            printf("The provides database is up to date.\n");
            return 0;
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

    provides_progressbar_start("Feching provides database");
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

int
plugin_provides_search(char *pattern)
{
    FILE *fh;
    char line[BUFLEN];
    int pcreErrorOffset;
    const char *pcreErrorStr;
    pcre *pcre;
    struct pkg_head_t head;
    char *repo_name;
    struct pkg_repo *r = NULL;
    fpkg_t *pnode;
    file_t *pfile;

    SLIST_INIT (&head);

    fh = fopen(PKG_DB_PATH "provides.db","rb");
    if (fh == NULL) {
        fprintf(stderr, "Provides database not found, please update before.\n");
        return (-1);
    }

    pcre = pcre_compile(pattern, 0, &pcreErrorStr, &pcreErrorOffset, NULL);

    if(pcre == NULL) {
        fprintf(stderr, "Invalid serach pattern\n");
        goto error_pcre;
    }

    while(fgets(line,BUFLEN,fh) != NULL) {
        int match;
        int len = strlen(line);
        for (int i=len-1; i > 0; i--) {
            if (line[i] == '\r' || line[i] == '\n') {
                line[i] = 0;
            } else {
                break;
            }
        }

        char *sep = strstr(line,"*");
        if(sep != NULL) {
            *sep = 0;
            char *test;
            sep++;

            if(strstr(pattern,"/")) {
                test = sep;
            } else {
                test = basename(sep);
            }

            if (pcre_exec(pcre, NULL, test, strlen(test), 0, 0, NULL, 0) >= 0) {
                int found = 0;
                SLIST_FOREACH(pnode,&head, next) {
                    if(strcmp(pnode->pkg_name, line)==0) {
                        found = 1;
                        break;
                    }
                }

                if (found == 0) {
                    pnode = malloc (sizeof(struct fpkg_t));
                    if (pnode == NULL) {
                        exit(ENOMEM);
                    } else {
                        pnode->pkg_name = strdup(line);
                        if(pnode->pkg_name == NULL) {
                            exit(ENOMEM);
                        }
                        SLIST_INIT (&(pnode->files));
                    }
                    SLIST_INSERT_HEAD(&head,pnode,next);
                }
                pfile = malloc(sizeof(struct file_t));
                if(pfile == NULL) {
                    exit(ENOMEM);
                }
                pfile->name = strdup(sep);
                if(pfile->name == NULL) {
                    exit(ENOMEM);
                }
                SLIST_INSERT_HEAD(&pnode->files,pfile,next);
            }
        }
    }

    while (pkg_repos(&r) == EPKG_OK) {
        if (pkg_repo_enabled(r)) {
            repo_name = (char *)pkg_repo_name(r);
            display_per_repo(repo_name, &head);
        }
    }

    fclose(fh);
    pcre_free(pcre);
    free_list(&head);

error_pcre:
    fclose(fh);
    pcre_free(pcre);
    return (-1);
}

int
plugin_provides_update(void)
{
    return plugin_fetch_file();
}

int cb_event(void *data, struct pkgdb *db) {
    struct pkg_event *ev = data;
    if (ev->type == PKG_EVENT_INCREMENTAL_UPDATE) {
        plugin_provides_update();
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
        return plugin_provides_update();
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
