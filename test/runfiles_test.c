#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "liblogc.h"
#include "unity.h"
#include "librunfiles.h"

/* #include "test.h" */

bool debug;
bool trace;
extern bool verbose;

/* UT_string *buf; */

char *pgm;
static struct runfiles_s *runfiles = NULL;

void setUp(void) {
    /* printf(GRN "setUp:" CRESET " %s\n", pgm); */
    if (runfiles != NULL) return;
    rf_init(pgm);
    /* runfiles = */
    /* printf("runfiles:\n"); */
    /* int i = 0; */
    /* while (runfiles[i].key != NULL) { */
    /*     printf("entry %d: %s -> %s\n", i, */
    /*            runfiles[i].key, runfiles[i].val); */
    /*     i++; */
    /* } */
    /* printf("done\n"); */
}

void tearDown(void) {
    /* printf("\n" GRN "tearDown" CRESET "\n"); */
    /* free(runfiles); */
}

void test_runfiles(void) {
    /* char *test_str = "foo"; */
    int i = 0;
    while (runfiles[i].key != NULL) {
        char *lhs_basename = basename(runfiles[i].key);
        char *rhs_basename = basename(runfiles[i].val);
        TEST_ASSERT_EQUAL_STRING(lhs_basename, rhs_basename);
        i++;
    }
}

void test_runfile_count(void) {
    int i = 0;
    while (runfiles[i].key != NULL) {
        i++;
    }
    TEST_ASSERT_EQUAL_INT(13, i);
}

void test_runfile_search(void)
{
    char *f = BAZEL_CURRENT_REPOSITORY
        "test/test.dat";
    log_debug("searching for: %s", f);
    char *rf = rf_rlocation(f);
    int rc = access(rf, F_OK);
    TEST_ASSERT_EQUAL_INT(0, rc);
}

int main(int argc, char *argv[])
{
    char *opts = "hdtv";
    int opt;
    char *pkgarg = NULL;

    debug = true;

    while ((opt = getopt(argc, argv, opts)) != -1) {
        switch (opt) {
        case '?':
            fprintf(stderr, "uknown opt: %c", optopt);
            exit(EXIT_FAILURE);
            break;
        case ':':
            fprintf(stderr, "uknown option: %c", optopt);
            exit(EXIT_FAILURE);
            break;
        case 'd':
            debug = true;
            break;
        case 'h':
            /* _print_usage(); */
            exit(EXIT_SUCCESS);
            break;
        case 't':
            trace = true;
            break;
        case 'v':
            verbose = true;
        case 'x':
        default:
            ;
        }
    }
    if ((argc - optind) > 1) {
        fprintf(stderr, "Too many options");
        exit(EXIT_FAILURE);
    } else {
        if ((argc-optind) == 1) {
            pkgarg = argv[optind];
            printf("PATH: %s\n", pkgarg);
        }
    }

    // NB: argv could be empty
    if (debug) {
        log_debug("BAZEL_CURRENT_REPOSITORY: '%s'", BAZEL_CURRENT_REPOSITORY);

        log_debug("BAZEL_TEST: '%s'", getenv("BAZEL_TEST"));
        log_debug("TEST_WORKSPACE: '%s'", getenv("TEST_WORKSPACE"));
        log_debug("getcwd: %s", getcwd(NULL, 0));
        log_debug("getenv(PWD): %s", getenv("PWD"));
        log_debug("BUILD_WORKSPACE_DIRECTORY: %s", getenv("BUILD_WORKSPACE_DIRECTORY"));
        log_debug("BUILD_WORKING_DIRECTORY: %s", getenv("BUILD_WORKING_DIRECTORY"));
        log_debug("HOME: %s", getenv("HOME"));
        log_debug("TEST_SRCDIR: %s", getenv("TEST_SRCDIR"));
        log_debug("BINDIR: %s", getenv("BINDIR"));
        /* RUNFILES_* only set under test on macos. */
        log_debug("RUNFILES_MANIFEST_FILE: %s", getenv("RUNFILES_MANIFEST_FILE"));
        log_debug("RUNFILES_MANIFEST_ONLY: %s", getenv("RUNFILES_MANIFEST_ONLY"));
        log_debug("RUNFILES_DIR: %s", getenv("RUNFILES_DIR"));
        log_debug("BINDIR: %s", getenv("BINDIR"));
    }

    char *cwd = getcwd(NULL, 0);
    printf("cwd: %s\n", cwd);
    printf("argv[0]: %s\n", argv[0]);

    pgm = argv[0];

    UNITY_BEGIN();
    /* RUN_TEST(test_runfiles); */
    /* RUN_TEST(test_runfile_count); */
    RUN_TEST(test_runfile_search);
    UNITY_END();

    printf("test exit...\n");
    return 0;
}
