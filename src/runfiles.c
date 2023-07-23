#include <errno.h>
#include <libgen.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "utstring.h"

#include "runfiles.h"

bool debug;
bool trace;
bool verbose;

/* api:
   runfiles_new() -> UT_hash of MANIFEST (relpath, realpath) pairs
   runfiles_lookup(runfiles, path)
   c:    runfiles_lookup(runfiles, "my_workspace/path/to/my/data.txt");
   c++:  runfiles->Rlocation("my_workspace/path/to/my/data.txt");
 */

#if EXPORT_INTERFACE
struct runfiles_s {
    char *key;
    char *val;
}
#endif

/* env vars: RUNFILES_MANIFEST_FILE, RUNFILES_DIR */
/* at launch: argv[0] is pgmname path,
   and cwd is argv[0].runfiles/<repo name>/
   the manifest is in two places:
   argv[0].runfiles_manifest, and
   argv[0].runfiles/MANIFEST
*/

EXPORT struct runfiles_s *runfiles_new(char *argv0)
{
    /* struct runfiles_s *rfs = _runfiles_init(argv0); */
    /* char *rf_repo_mapping = _get_repo_mapping(); */

    char *rf_manifest_file = _get_runfiles_manifest(argv0);
    log_debug("Found rf manifest: %s", rf_manifest_file);

    char *rf_dir = _get_runfiles_dir(argv0);
    log_debug("Found rf dir: %s", rf_dir);

    //FIXME: if we have an rf dir, why bother reading the manifest? We
    //only need to read the _repo_mapping file.

    // if rfs->rf_manifest null AND rfs->rf_dir null then abort

    // else

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
    if (debug)
        log_debug(RED "line ct:" CRESET " %d", line_ct);

    struct runfiles_s *runfiles = calloc(sizeof(struct runfiles_s),
                                         line_ct + 1);

    rewind(fpManifest);

    if (debug) log_debug("Reading MANIFEST");
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
        if (debug) {
            printf(RED "kv[0]:" CRESET " %s\n", kv[0]);
            printf(RED "kv[1]:" CRESET "     %s\n", kv[1]);
        }
        runfiles[i].key = strdup(kv[0]);
        runfiles[i].val = strdup(kv[1]);
        i++;
    }
    fclose(fpManifest);
    if (debug)
        log_debug(BLU "DONE" CRESET);

    return runfiles;
}

/* LOCAL char *_get_repo_mapping(void) */
/* { */
/*     log_debug("ENTRY: _get_repo_mapping"); */
/*     char *rf_repo_mapping; */

/*     UT_string *manifest_file; */
/*     utstring_new(manifest_file); */

/*     /\* launch_dir = getenv("TEST_SRCDIR"); // getcwd(NULL, 0); *\/ */

/*     runfiles_manifest_file = getenv("RUNFILES_MANIFEST_FILE"); */
/*     if (debug) */
/*         log_debug("RUNFILES_MANIFEST_FILE: %s", runfiles_manifest_file); */
/*     if (runfiles_manifest_file) { */
/*         /\* utstring_printf(manifest_file, "%s", runfiles_manifest_file); *\/ */
/*         /\* int rc = access(utstring_body(manifest_file), R_OK); *\/ */
/*         int rc = access(runfiles_manifest_file, R_OK); */
/*         if (rc == 0) { */
/*             if (debug) */
/*                 log_debug("using RUNFILES_MANIFEST_FILE: %s", */
/*                           runfiles_manifest_file); */
/*             return strndup(runfiles_manifest_file, */
/*                            strlen(runfiles_manifest_file)); */
/*         } */
/*     } */
/*     // else try argv0-relative */
/*     // argv[0] is executable; runfiles subdir is same name w/sfx '.runfiles' */
/*     // manifest is either <exec>.runfiles/MANIFEST */
/*     // or <exec>.runfiles_manifest */
/*     log_debug("trying argv0: %s", argv0); */

/*     utstring_renew(manifest_file); */
/*     utstring_printf(manifest_file, "%s.runfiles/MANIFEST", argv0); */
/*     if (debug) */
/*         log_debug("accessing argv0 MANIFEST: %s", utstring_body(manifest_file)); */
/*     int rc = access(utstring_body(manifest_file), R_OK); */
/*     if (rc == 0) { */
/*         if (debug) */
/*             log_debug("using argv0/MANIFEST: %s", utstring_body(manifest_file)); */
/*         char *result = strndup(utstring_body(manifest_file), */
/*                                utstring_len(manifest_file)); */
/*         utstring_free(manifest_file); */
/*         return result; */
/*     } */

/*     UT_string *runfiles_dir; */
/*     utstring_new(runfiles_dir); */
/*     utstring_printf(runfiles_dir, "%s.runfiles", argv0); */
/*     if (debug) */
/*         log_debug("accessing argv runfiles dir: %s", */
/*                   utstring_body(runfiles_dir)); */
/*     rc = access(utstring_body(runfiles_dir), R_OK); */
/*     if (rc == 0) { */
/*         if (debug) */
/*             log_debug("using rfdir argv0.runfiles: %s", */
/*                       utstring_body(runfiles_dir)); */
/*         char *result = strndup(utstring_body(runfiles_dir), */
/*                                utstring_len(runfiles_dir)); */
/*         utstring_free(runfiles_dir); */
/*         return result; */
/*     } */
/*     log_error("XXXXXXXXXXXXXXXX NO RUNFILES MANIFEST ################"); */
/*     return NULL; */
/* } */

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
LOCAL char *_get_runfiles_manifest(char *argv0)
{
    log_debug("ENTRY: _get_runfiles_manifest");
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
    if (debug)
        log_debug("RUNFILES_MANIFEST_FILE: %s", runfiles_manifest_file);
    if (runfiles_manifest_file) {
        /* utstring_printf(manifest_file, "%s", runfiles_manifest_file); */
        /* int rc = access(utstring_body(manifest_file), R_OK); */
        int rc = access(runfiles_manifest_file, R_OK);
        if (rc == 0) {
            if (debug)
                log_debug("using RUNFILES_MANIFEST_FILE: %s",
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
    if (debug)
        log_debug("accessing argv0 MANIFEST: %s", utstring_body(manifest_file));
    int rc = access(utstring_body(manifest_file), R_OK);
    if (rc == 0) {
        if (debug)
            log_debug("using argv0/MANIFEST: %s", utstring_body(manifest_file));
        char *result = strndup(utstring_body(manifest_file),
                               utstring_len(manifest_file));
        utstring_free(manifest_file);
        return result;
    }

    UT_string *runfiles_dir;
    utstring_new(runfiles_dir);
    utstring_printf(runfiles_dir, "%s.runfiles", argv0);
    if (debug)
        log_debug("accessing argv runfiles dir: %s",
                  utstring_body(runfiles_dir));
    rc = access(utstring_body(runfiles_dir), R_OK);
    if (rc == 0) {
        if (debug)
            log_debug("using rfdir argv0.runfiles: %s",
                      utstring_body(runfiles_dir));
        char *result = strndup(utstring_body(runfiles_dir),
                               utstring_len(runfiles_dir));
        utstring_free(runfiles_dir);
        return result;
    }
    log_error("XXXXXXXXXXXXXXXX NO RUNFILES MANIFEST ################");
    return NULL;
}

LOCAL char *_get_runfiles_dir(char *argv0)
{
    /* $TEST_SRCDIR =? $RUNFILES_DIR - see https://github.com/bazelbuild/baze
       l/issues/6093 */
    char *test_srcdir = getenv("TEST_SRCDIR");
    if (debug)
        log_debug("TEST_SRCDIR: %s", test_srcdir);

    char *runfiles_dir = getenv("RUNFILES_DIR");
    if (debug)
        log_debug("RUNFILES_DIR: %s", runfiles_dir);
    if (runfiles_dir) {
        return strndup(runfiles_dir, strlen(runfiles_dir));
    }

    UT_string *manifest_file;
    utstring_new(manifest_file);
    utstring_printf(manifest_file, "%s.runfiles", argv0);
    if (debug)
        log_debug("accessing %s", utstring_body(manifest_file));
    int rc = access(utstring_body(manifest_file), R_OK);
    if (rc == 0) {
        char *result = strndup(utstring_body(manifest_file),
                               utstring_len(manifest_file));
        utstring_free(manifest_file);
        if (debug)
            log_debug("using RUNFILES_DIR: %s", result);
        return result;
    }
    log_error("XXXXXXXXXXXXXXXX NO RUNFILES DIR ################");
    return NULL;
}

EXPORT void runfiles_delete(struct runfiles_s *runfiles)
{
    free(runfiles);
}

/* EXPORT UT_array *runfiles_array(void) */
/* { */
/*     printf("manifest_path\n"); */

/*     /\* at launch: argv[0] is pgmname path, */
/*        and cwd is argv[0].runfiles/__main__/ */
/*        the manifest is in two places: */
/*        argv[0].runfiles_manifest, and */
/*        argv[0].runfiles/MANIFEST */
/*        (== cwd/../MANIFEST) */
/*      *\/ */

/*     char *runfiles_root = getcwd(NULL, 0); */
/*     printf("runfiles_root: %s\n", runfiles_root); */

/*     char *manifest = "../MANIFEST"; */
/*     rc = access(manifest, R_OK); */

/*     if (rc) { */
/*         if (verbose) log_error("must be run by bazel..."); */
/*         exit(EXIT_FAILURE); */
/*     } else { */
/*         if (verbose) log_info("Configuring for bazel env."); */
/*     } */
/* } */

/* lookup file in manifest  */
/* EXPORT void get_runfile(char *file) */
/* { */
/*     UT_string *runfile; */
/*     utstring_new(runfile); */
/*     utstring_printf(runfile, */
/*                     "%s/external/opam/man/%s/@%s.1", */
/*                     runfiles_root, */
/*                     section, */
/*                     manpage); */
/*     printf("page src: %s\n", utstring_body(runfile)); */

/*     char *argv[] = { */
/*         "man", */
/*         utstring_body(pagesrc), */
/*         NULL */
/*     }; */

/*     int argc = (sizeof(argv) / sizeof(argv[0])) - 1; */
/*     if (verbose) */
/*         printf("displaying manpage %s\n", utstring_body(pagesrc)); */
/*     result = spawn_cmd_with_stdout(exe, argc, argv); */
/*     if (result != 0) { */
/*         fprintf(stderr, "FAIL: spawn_cmd_with_stdout for man\n"); */
/*         exit(EXIT_FAILURE); */
/*         /\* } else { *\/ */
/*         /\*     printf("install result: %s\n", result); *\/ */
/*     } */
/*     return; */
/* } */
