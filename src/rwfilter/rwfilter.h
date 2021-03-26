/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWFILTER_H
#define _RWFILTER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWFILTER_H, "$SiLK: rwfilter.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/*
**  rwfilter.h
**
**  Privater header information for the rwfilter application
**
*/

#include <silk/rwrec.h>
#include <silk/skipaddr.h>
#include <silk/skplugin.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/utils.h>

/* TYPEDEFS AND DEFINES */

/* where to write --help output */
#define USAGE_FH stdout

/* where to send --dry-run output */
#define DRY_RUN_FH stdout

/* where to send file names when --print-filenames is active */
#define PRINT_FILENAMES_FH stderr

/* whether rwfilter supports threads */
#define SK_RWFILTER_THREADED 1

/* environment variable that determines number of threads */
#define RWFILTER_THREADS_ENVAR  "SILK_RWFILTER_THREADS"

/* default number of threads to use */
#define RWFILTER_THREADS_DEFAULT 1


/* maximum number of dynamic libraries that we support */
#define APP_MAX_DYNLIBS 8

/* maximum number of filter checks */
#define MAX_CHECKERS (APP_MAX_DYNLIBS + 2)

/*
 *  The number and types of skstream_t output streams: pass, fail, all
 */
#define DESTINATION_TYPES 3
#define DEST_PASS  0
#define DEST_FAIL  1
#define DEST_ALL   2


typedef struct destination_st destination_t;

struct destination_st {
    skstream_t     *stream;
    destination_t  *next;
};

typedef struct dest_type_st {
    uint64_t        max_records;
    destination_t  *dest_list;
    int             count;
} dest_type_t;

/* for counting the flows, packets, and bytes */
typedef struct rec_count_st {
    uint64_t  flows;
    uint64_t  pkts;
    uint64_t  bytes;
} rec_count_t;

/* holds filter-statistics data */
typedef struct filter_stats_st {
    rec_count_t     read;           /* count of records read */
    rec_count_t     pass;           /* count of records that passed */
    uint32_t        files;          /* count of files */
} filter_stats_t;

/* output of checker functions */
typedef enum {
    RWF_FAIL,                   /* filter fails the record */
    RWF_PASS,                   /* filter passes the record */
    RWF_PASS_NOW,               /* filter passes the record;
                                 * run no more filters */
    RWF_IGNORE                  /* this record neither passes or
                                 * fails; run no more filters */
} checktype_t;


/*
 *  INCR_REC_COUNT(count, rec)
 *
 *    Increment the values in the filter_stats_t_t 'count' by the
 *    values in the rwRec* 'rec'.
 *
 */
#define INCR_REC_COUNT(count, rec)                 \
    {                                              \
        (count).flows++;                           \
        (count).pkts  += rwRecGetPkts(rec);        \
        (count).bytes += rwRecGetBytes(rec);       \
    }





/* EXTERNAL VARIABLES (rwfilter.c) */

/* information about the destination types (ALL, PASS, FAIL); includes
 * a linked list of destination streams */
extern dest_type_t dest_type[DESTINATION_TYPES];

/* support for --print-statistics and --print-volume-statistics; NULL
 * when the switch has not been provided */
extern skstream_t *print_stat;

/* where to print output during a --dry-run; NULL when the switch has
 * not been provided */
extern FILE *dryrun_fp;

/* where to print output for --print-filenames; NULL when the switch
 * has not been provided */
extern FILE *filenames_fp;

/* input file specified by --input-pipe; NULL when the switch has not
 * been provided */
extern const char *input_pipe;

/* support for the --xargs switch; NULL when the switch has not been
 * provided */
extern skstream_t *xargs;

/* index into argv of first option that does not start with a '--'.
 * This assumes getopt rearranges the options, which gnu getopt will
 * do. */
extern int arg_index;

/* true as long as we are reading records */
extern int reading_records;

/* whether to print volume statistics */
extern int print_volume_stats;

/* number of total threads */
extern uint32_t thread_count;

/* number of checks to preform */
extern int checker_count;

/* function pointers to handle checking and or processing */
extern checktype_t (*checker[MAX_CHECKERS])(rwRec*);


/* FUNCTION DECLARATIONS */

/* fglob functions (fglob.c) */

void
fglobUsage(
    FILE               *);
int
fglobSetup(
    void);
void
fglobTeardown(
    void);
char *
fglobNext(
    char               *buf,
    size_t              bufsize);
int
fglobFileCount(
    void);
int
fglobValid(
    void);
int
fglobSetFilters(
    sk_bitmap_t       **sensor_bitmap,
    sk_bitmap_t       **flowtype_bitmap);


/* application setup functions (rwfilterutils.c) */

void
appSetup(
    int                 argc,
    char              **argv);
void
appTeardown(
    void);
void
filterIgnoreSigPipe(
    void);
int
filterOpenInputData(
    skstream_t        **stream,
    skcontent_t         content_type,
    const char         *filename);


/* application functions (rwfilter.c) */

int
closeAllDests(
    void);
int
closeOutputDests(
    const int           dest_id,
    int                 quietly);
int
closeOneOutput(
    const int           dest_id,
    destination_t      *dest);
char *
appNextInput(
    char               *buf,
    size_t              bufsize);


/* filtering  functions (rwfiltercheck.c) */

int
filterCheckFile(
    skstream_t         *path,
    const char         *ip_dir);
checktype_t
filterCheck(
    const rwRec        *rwrec);
void
filterUsage(
    FILE*);
int
filterGetCheckCount(
    void);
int
filterGetFGlobFilters(
    void);
int
filterSetup(
    void);
void
filterTeardown(
    void);


/* filtering functions (rwfiltertuple.c) */

int
tupleSetup(
    void);
void
tupleTeardown(
    void);
void
tupleUsage(
    FILE               *fh);
checktype_t
tupleCheck(
    const rwRec        *rwrec);
int
tupleGetCheckCount(
    void);



/* "main" for filtering when threaded (rwfilterthread.c) */

int
threadedFilter(
    filter_stats_t     *stats);




#ifdef __cplusplus
}
#endif
#endif /* _RWFILTER_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
