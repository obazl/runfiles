/* Runfiles lookup library for Bazel-built C binaries and tests.
   USAGE:
 1.  In your MODULES.bazel file add a dep:  bazel_dep(name = "runfiles",       version = "0.1.0")
 2.  In your build rule, depend on this runfiles library and define a C preprocessor
     macro exposing the local repository name (by convention, BAZEL_CURRENT_REPOSITORY):
       cc_binary(
           name = "my_binary",
           ...
           local_defines = ["BAZEL_CURRENT_REPOSITORY=\\\"{}\\\"".format(package_relative_label("@myrepo").repo_name)],
           ...
           deps = ["@runfiles//lib:runfiles"],
       )
      alternatively:  "BAZEL_CURRENT_REPOSITORY=\\\"{}\\\"".format(repository_name()[1:])
  3.  Include the runfiles library header:
         #include "librunfiles.h"
  3.  Initialize the lib from the exe path argv[0] (do this *before* any chdir op)
             rf_init(argv[0]);
      (since runfiles dir is relative to launch dir)
  4.  call rf_rlocation(char*) to get a path
             char *libs7_repo = rf_rlocation(BAZEL_CURRENT_REPOSITORY "/path/to/data.txt");
 */
#include <errno.h>
#if INTERFACE
#include <fts.h>
#endif
#include <libgen.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "liblogc.h"
#include "utstring.h"

#include "runfiles.h"

#define TRACE_FLAG     rf_trace
bool    TRACE_FLAG     = false;
#define DEBUG_LEVEL    rf_debug
int     DEBUG_LEVEL    = 0;

static bool initialized = false;

static char *rf_manifest_file = NULL;
static char *rf_exe_root = NULL;
static char *rf_tree_root = NULL;
static char *rf_repo_mapping = NULL;

static UT_string *runfile_abs;

#if EXPORT_INTERFACE
struct runfiles_s {
    char *key;
    char *val;
}
#endif

/* env vars: RUNFILES_MANIFEST_FILE, RUNFILES_DIR */
/* on Windows, RUNFILES_MANIFEST_FILE is set */
/* on other platforms, both may be empty */
/* at launch: argv[0] is pgmname path,
   and cwd is argv[0].runfiles/<repo name>/
   the manifest is in two places:
   argv[0].runfiles_manifest, and
   argv[0].runfiles/MANIFEST
*/

EXPORT char *rf_root(void)
{
    return rf_tree_root;
}

EXPORT char *rf_repo_map(void)
{
    TRACE_ENTRY;
    return rf_repo_mapping;
    TRACE_EXIT;
}

EXPORT char *rf_rlocation(char *runfile)
{
    TRACE_ENTRY;
    LOG_INFO(0, "runfile: %s", runfile);
    LOG_INFO(0, "rf_tree_root: %s", rf_tree_root);

    if (!initialized) {
        fprintf(stderr, "%s:%d ERROR: librunfiles not initialized; call rf_init(dir) first.\n", __FILE__, __LINE__);
        // exit?
        return NULL;
    }

    utstring_new(runfile_abs);
    utstring_printf(runfile_abs, "%s/%s", rf_tree_root, runfile);

    /* first try rf tree root (i.e. external resource) */
    LOG_DEBUG(0, "accessing runfile: %s", utstring_body(runfile_abs));
    int rc = access(utstring_body(runfile_abs), R_OK);
    if (rc == 0) {
        LOG_DEBUG(0, "found runfile: %s", utstring_body(runfile_abs));
        char *result = strndup(utstring_body(runfile_abs),
                               utstring_len(runfile_abs));
        utstring_free(runfile_abs);
        return result;
    } else {
        /* try _main/ */
        LOG_DEBUG(0, "NOT found runfile: %s", utstring_body(runfile_abs));
        utstring_renew(runfile_abs);
        utstring_printf(runfile_abs, "%s/%s", rf_exe_root, runfile);
        LOG_DEBUG(0, "accessing runfile: %s", utstring_body(runfile_abs));
        int rc = access(utstring_body(runfile_abs), R_OK);
        if (rc == 0) {
            LOG_DEBUG(0, "found runfile: %s", utstring_body(runfile_abs));
            char *result = strndup(utstring_body(runfile_abs),
                                   utstring_len(runfile_abs));
            utstring_free(runfile_abs);
            return result;
        } else {
            LOG_DEBUG(0, "NOT found runfile: %s", utstring_body(runfile_abs));
            utstring_free(runfile_abs);
        }
    }
    return NULL;
}
LOCAL char *_get_runfiles_manifest(char *argv0)
{
    TRACE_ENTRY;
    char *runfiles_manifest_file;
    /* char *runfiles_dir; */
    /* char *launch_dir; */

    UT_string *manifest_file;
    utstring_new(manifest_file);

    /* launch_dir = getenv("TEST_SRCDIR"); // getcwd(NULL, 0); */

    // if BAZEL_TEST, then one of RUNFILES_DIR,
    // RUNFILES_MANIFEST_FILE should be defined,
    // or, TEST_SRCDIR should be the runfiles dir
    // BUT, we will have no MANIFEST or runfiles_manifest file.

    // else if BUILD_WORKSPACE_DIRECTORY
    // or BUILD_WORKING_DIRECTORY defined
    // then we're in 'bazel run' env, so derive rfdir from argv0
    // else we're not running under Bazel so no runfiles
    runfiles_manifest_file = getenv("RUNFILES_MANIFEST_FILE");
    LOG_DEBUG(0, "RUNFILES_MANIFEST_FILE: %s", runfiles_manifest_file);
    if (runfiles_manifest_file) {
        /* utstring_printf(manifest_file, "%s", runfiles_manifest_file); */
        /* int rc = access(utstring_body(manifest_file), R_OK); */
        int rc = access(runfiles_manifest_file, R_OK);
        if (rc == 0) {
            LOG_DEBUG(0, "using RUNFILES_MANIFEST_FILE: %s",
                          runfiles_manifest_file);
            return strndup(runfiles_manifest_file,
                           strlen(runfiles_manifest_file));
        }
    }

    // else try argv0-relative
    // argv[0] is executable; runfiles subdir is same name w/sfx '.runfiles'
    // manifest is either <exec>.runfiles/MANIFEST
    // or <exec>.runfiles_manifest
    log_debug("trying argv0: %s", argv0);

    utstring_renew(manifest_file);
    utstring_printf(manifest_file, "%s.runfiles/MANIFEST", argv0);
    LOG_DEBUG(0, "accessing argv0 MANIFEST: %s", utstring_body(manifest_file));
    int rc = access(utstring_body(manifest_file), R_OK);
    if (rc == 0) {
        LOG_DEBUG(0, "using argv0/MANIFEST: %s", utstring_body(manifest_file));
        char *result = strndup(utstring_body(manifest_file),
                               utstring_len(manifest_file));
        utstring_free(manifest_file);
        return result;
    } else {
        LOG_DEBUG(0, "argv0 MANIFEST not found; trying <exe>.runfiles dir", "");
    }

    UT_string *runfiles_dir;
    utstring_new(runfiles_dir);
    utstring_printf(runfiles_dir, "%s.runfiles", argv0);
    LOG_DEBUG(0, "accessing argv runfiles dir: %s",
              utstring_body(runfiles_dir));
    rc = access(utstring_body(runfiles_dir), R_OK);
    if (rc == 0) {
        LOG_DEBUG(0, "using rfdir argv0.runfiles: %s",
                  utstring_body(runfiles_dir));
        char *result = strndup(utstring_body(runfiles_dir),
                               utstring_len(runfiles_dir));
        utstring_free(runfiles_dir);
        return result;
    } else {
        log_error("XXXXXXXX NO RUNFILES MANIFEST ########");
    }
    return NULL;
}

LOCAL char *_get_runfiles_dir(char *argv0)
{
    TRACE_ENTRY;
    /* $TEST_SRCDIR =? $RUNFILES_DIR
       - see https://github.com/bazelbuild/bazel/issues/6093 */
    /* char *test_srcdir = getenv("TEST_SRCDIR"); */
    /* LOG_DEBUG(0, "TEST_SRCDIR: %s", test_srcdir); */

    char *runfiles_dir = getenv("RUNFILES_DIR");
    LOG_DEBUG(0, "RUNFILES_DIR: %s", runfiles_dir);
    if (runfiles_dir) {
        return strndup(runfiles_dir, strlen(runfiles_dir));
    }

    UT_string *runfiles_root;
    utstring_new(runfiles_root);
    utstring_printf(runfiles_root, "%s.runfiles", argv0);
    LOG_INFO(1, "accessing %s", utstring_body(runfiles_root));
    int rc = access(utstring_body(runfiles_root), R_OK);
    if (rc == 0) {
        char *result = strndup(utstring_body(runfiles_root),
                               utstring_len(runfiles_root));
        utstring_free(runfiles_root);
        /* if (rf_debug) */
        /*     log_debug("using RUNFILES_DIR: %s", result); */
        return result;
    } else {
        log_error("XXXXXXXXXXXXXXXX NO RUNFILES ROOT ################");
        /* FIXME */
        return argv0;
    }
    return NULL;
}

/* LOCAL char *_get_repo_mapping(void) */
/* { */
/*
  case: BAZEL_TEST == 1
      running a test target with either bazel test or bazel run
      RUNFILES_DIR is defined?
      TEST_SRCDIR = runfiles dir
  case: BAZEL_TEST == undefined
      Bazel defines except BUILD_WORKSPACE_DIR,
      BUILD_WORKING_DIRECTORY undefined, e.g. TEST_SRCDIR,
      RUNFILES_* etc.
      means we're in a bazel run env.
      In this case runfiles dir derivable from argv[0]
      (executable path). e.g. if argv[0] is foo/bar,
      then we will have foo/bar.repo_mapping,
      foo/bar.runfiles, foo/bar.runfiles/MANIFEST,
      and foo/bar.runfiles_manifest.
      I.e. for executable foo/bar:baz, runfiles are
      foo/bar/baz.runfiles

      Also in this case we should set RUNFILES_MANIFEST_FILE or
      RUNFILES_DIR envvars "for the benefit of data-dependency
      binaries". Meaning? E.g. if this exec creates a runfiles object
      and sets those env vars, it may then dload a plugin, which can
      then rely on those env vars instead of rediscovering the
      runfiles stuff. ???

      "(in case of runfiles dir w/o manifest) the client needs to
      check whether the rlocation-returned path exists anyway, so
      looking up the runfile from a manifest when the directory tree
      is also available doesn't gain anything."

  See https://docs.google.com/document/d/e/2PACX-1vSDIrFnFvEYhKsCMdGdD40wZRBX3m3aZ5HhVj4CtHPmiXKDCxioTUbYsDydjKtFDAzER5eg7OjJWs3V/pub
 */

LOCAL char *_get_repo_mapping(char *argv0)
{
    TRACE_ENTRY;
    /* $TEST_SRCDIR =? $RUNFILES_DIR
       - see https://github.com/bazelbuild/bazel/issues/6093 */
    /* char *test_srcdir = getenv("TEST_SRCDIR"); */
    /* LOG_DEBUG(0, "TEST_SRCDIR: %s", test_srcdir); */

    char *runfiles_dir = getenv("RUNFILES_DIR");
    LOG_DEBUG(0, "RUNFILES_DIR: %s", runfiles_dir);
    if (runfiles_dir) {
        return strndup(runfiles_dir, strlen(runfiles_dir));
    }

    UT_string *runfiles_root;
    utstring_new(runfiles_root);
    utstring_printf(runfiles_root, "%s.repo_mapping", argv0);
    LOG_INFO(1, "accessing %s", utstring_body(runfiles_root));
    int rc = access(utstring_body(runfiles_root), F_OK);
    if (rc == 0) {
        char *result = strndup(utstring_body(runfiles_root),
                               utstring_len(runfiles_root));
        utstring_free(runfiles_root);
        /* if (rf_debug) */
        /*     log_debug("using RUNFILES_DIR: %s", result); */
        return result;
    } else {
        /* FIXME FIXME */
        /* log_error("NO ACCESS: %s", */
        /*           utstring_body(runfiles_root)); */
        /* log_info("CWD: %s", getcwd(NULL, 0)); */
        return argv0;
    }
    return NULL;
}

EXPORT void runfiles_delete(struct runfiles_s *runfiles)
{
    free(runfiles);
}

EXPORT void rf_init(char* argv0)
{
    TRACE_ENTRY_MSG("%s", argv0);
    LOG_INFO(0, "BAZEL_CURRENT_REPOSITORY: %s", BAZEL_CURRENT_REPOSITORY);

    /* RUNFILES_MANIFEST_FILE is set on windows, may be empty on other platforms */
    /* RUNFILES_DIR may be empty */
    /* if rf dir tree is available, use it; otherwise (windows), use manifest */

    rf_exe_root = getcwd(NULL, 0);
    LOG_INFO(0, "rf_exe_root: %s", rf_exe_root);

    rf_tree_root = _get_runfiles_dir(argv0);
    if (rf_tree_root == NULL) {
        rf_manifest_file = _get_runfiles_manifest(argv0);
        if (rf_manifest_file == NULL) {
        } else {
            log_debug("Found rf manifest: %s", rf_manifest_file);

            /* Now what? */

            FILE * fpManifest;
            char * line = NULL;
            int ch, line_ct = 0;
            size_t len = 0;
            ssize_t read;
            fpManifest = fopen(rf_manifest_file, "r");
            if (fpManifest == NULL) {
                log_error("fopen failure %s", rf_manifest_file);
                exit(EXIT_FAILURE);
            }

            for (ch = getc(fpManifest); ch != EOF; ch = getc(fpManifest))
                if (ch == '\n') line_ct++;
            LOG_DEBUG(0, RED "line ct:" CRESET " %d", line_ct);

            struct runfiles_s *runfiles = calloc(sizeof(struct runfiles_s),
                                                 line_ct + 1);

            rewind(fpManifest);

            if (rf_debug > 1) {
                log_debug("Reading MANIFEST");
                int i = 0;
                while ((read = getline(&line, &len, fpManifest)) != -1) {
                    line[strcspn(line, "\n")] = '\0';    /* trim trailing newline */

                    /* two tokens per line, first is path relative to exec dir,
                       second is corresponding absolute path */
                    /* char *token; */
                    const char *sep = " ";

                    char **ap, *kv[2];
                    for (ap = kv; (*ap = strsep(&line, sep)) != NULL;)
                        if (**ap != '\0')
                            if (++ap >= &kv[2])
                                break;
                    if (rf_debug) {
                        printf(RED "kv[0]:" CRESET " %s\n", kv[0]);
                        printf(RED "kv[1]:" CRESET "     %s\n", kv[1]);
                    }
                    runfiles[i].key = strdup(kv[0]);
                    runfiles[i].val = strdup(kv[1]);
                    i++;
                }
                fclose(fpManifest);
            }
        }
    } else {
        LOG_DEBUG(0, "rf_tree_root: %s", rf_tree_root);
    }

    //FIXME: if we have an rf dir, why bother reading the manifest? We
    //only need to read the _repo_mapping file.

    // if rfs->rf_manifest null AND rfs->rf_tree_root null then abort

    // else

    /* broken: */
    rf_repo_mapping = _get_repo_mapping(argv0);

    initialized = true;
    /* fprintf(stdout, "%s:%d INFO: librunfiles initialized\n", */
    /*         __FILE__, __LINE__); */

    TRACE_EXIT;
    /* return runfiles; */
}

LOCAL int _compare(const FTSENT** one, const FTSENT** two)
{
    return (strcmp((*one)->fts_name, (*two)->fts_name));
}

/* LOCAL void _handle_dir(FTS* tree, FTSENT *ftsentry) */
/* { */
/*     TRACE_ENTRY; */
/*     (void)tree; */
/*     printf("_handle_dir %s\n", ftsentry->fts_path); */
/*     TRACE_EXIT; */
/* } */

/* LOCAL void _handle_symlink(FTS* tree, FTSENT *ftsentry) */
/* { */
/*     TRACE_ENTRY; */
/*     (void)tree; */
/*     printf("_handle_symlink %s\n", ftsentry->fts_path); */
/*     TRACE_EXIT; */
/* } */

/* LOCAL void _handle_file(FTSENT *ftsentry) */
/* { */
/*     TRACE_ENTRY; */
/*     printf("_handle_file %s\n", ftsentry->fts_path); */
/*     TRACE_EXIT; */
/* } */

EXPORT void rf_fts(char *root, void(*handle_file)(char *))
{
    TRACE_ENTRY;
    FTS* tree = NULL;
    FTSENT *ftsentry     = NULL;

    LOG_DEBUG(0, "fts root: %s", root);

    char *old_cwd = getcwd(NULL, 0);
    LOG_DEBUG(0, "old cwd: %s", old_cwd);
    int rc = chdir(root);
    (void)rc; /* FIXME */
    /* char *cwd = getcwd(NULL, 0); */
    /* LOG_DEBUG(0, "new cwd: %s", cwd); */

    char *const _traversal_root[] = {
        /* [0] = resolved_troot, // traversal_root; */
        [0] = (char *const)"./",
        NULL
    };

    errno = 0;
    tree = fts_open(_traversal_root,
                    FTS_COMFOLLOW
                    | FTS_NOCHDIR
                    | FTS_PHYSICAL,
                    // NULL
                    &_compare
                    );
    if (errno != 0) {
        log_error("fts_open error: %s", strerror(errno));
        exit(EXIT_FAILURE);
        return;
    } else {
        LOG_DEBUG(0, "fts_open ok %s", _traversal_root[0]);
    }

    if (tree != NULL) {
        while( (ftsentry = fts_read(tree)) != NULL) {
            /* LOG_DEBUG(0, "entry: %d %s", */
            /*           ftsentry->fts_info, */
            /*           ftsentry->fts_path); */
            switch (ftsentry->fts_info) {
            case FTS_SL: // symlink
                handle_file(ftsentry->fts_path);
                /* _handle_symlink(tree, ftsentry); */
                break;
            case FTS_F : // regular file
                handle_file(ftsentry->fts_path);
                break;
            case FTS_D : // dir visited in pre-order
                /* _handle_dir(tree, ftsentry); */
                break;
            case FTS_DP:
                /* postorder directory */
                break;
            case FTS_SLNONE:
                /* symlink to non-existent target */
                LOG_WARN(0, "FTS_SLNONE (dangling symlink): %s", ftsentry->fts_path);
                break;
            case FTS_ERR:
                LOG_ERROR(0, "FTS_ERR: %s", ftsentry->fts_path);
                LOG_ERROR(0, "  error: %d: %s", ftsentry->fts_errno,
                          strerror(ftsentry->fts_errno));
                break;
            case FTS_DC:
                /* dir causing a cycle dir */
                LOG_WARN(0, "FTS_DC (dir cycle): %s", ftsentry->fts_path);
                break;
            case FTS_DNR:
                /* unreadable dir */
                LOG_WARN(0, "FTS_DNR (unreadable dir): %s", ftsentry->fts_path);
                break;
            case FTS_NS:
                /* no stat info, error */
                LOG_ERROR(0, "FTS_NS (no stat info, error): %s", ftsentry->fts_path);
                LOG_ERROR(0, "  error: %d: %s", ftsentry->fts_errno,
                          strerror(ftsentry->fts_errno));
                break;
            case FTS_NSOK:
                /* no stat info, not an error */
                LOG_WARN(0, "FTS_NSOK (no stat info, non-error): %s", ftsentry->fts_path);
                break;
            case FTS_DEFAULT:
                LOG_WARN(0, "FTS_DEFAULT: %s", ftsentry->fts_path);
                break;
            case FTS_DOT : // not specified to fts_open
                LOG_INFO(0, "FTS_DOT (hidden dir): %s", ftsentry->fts_path);
                // do not process children of hidden dirs
                /* fts_set(tree, ftsentry, FTS_SKIP); */
                break;
            default:
                LOG_ERROR(0, RED "Unhandled FTS type %d\n",
                          ftsentry->fts_info);
                exit(EXIT_FAILURE);
                break;
            }
        }
        LOG_INFO(0, "end while: (ftsentry = fts_read(tree)) != NULL)", "");
        chdir(old_cwd);

    } else {
        log_error("TREE == NULL");
    }


}

