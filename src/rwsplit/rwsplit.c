/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 * rwsplit takes a sequence of input files and generates a set
 * of sample files from them.  Each file is a single sample.
 *
 * Sampling criteria currnently has the following parameters:
 *
 * --basename: The name of the stub file to write to
 * --ip-limit: how many addresses to contain in a sample
 * --flow-limit: how many flows to contain in a sample
 * --packet-limit: how many packets to contain in a sample
 * --byte-limit: how many bytes
 * --sample-ratio: specifies that 1/n flows should be taken for the
 *                 sample file.
 * --file-ratio: specifies that 1/n possible sample files will be used.
 * sample is going to progress through the data linearly, so if you're
 * going to use time, make sure you sort on time.
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: rwsplit.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/rwrec.h>
#include <silk/skipset.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* where to write --help output */
#define USAGE_FH stdout

/* maximum number of output files; the file suffix is generated by
 * "%08u", so we can only have eight 9's worth of files */
#define MAX_OUTPUT_FILES 99999999

/* keep this in sync with the appOptionsEnum! */
typedef enum aggmode {
    AGGMODE_IPS, AGGMODE_FLOWS, AGGMODE_PKTS, AGGMODE_BYTES,
    /* none must be last */
    AGGMODE_NONE
} aggmode_t;


/* LOCAL VARIABLES */

/* for looping over files on the command line */
static sk_options_ctx_t *optctx;

/* basename of output files */
static char *out_basename = NULL;

/* current output file */
static skstream_t *stream_out = NULL;

/* current input file */
static skstream_t *stream_in;

/* IPset in which to store unique IPs */
static skipset_t *ips = NULL;

/* the index of the output file are we writing */
static uint32_t output_ctr = 0;

/* max number of output files */
static uint32_t max_outputs = MAX_OUTPUT_FILES;

/* max ip/flow/packet/byte per file */
static uint64_t tag_limit = 0;

/* current count of ip/flow/packet/byte */
static uint64_t tag_current = 0;

/* how many records we need to read before we write one */
static uint32_t sample_ratio = 1;

/* how many records we've read on the way to reading 'sample_ratio'
 * records */
static uint32_t current_sample_count = 0;

/* instead of writing each file, write each 'file_ratio' file */
static uint32_t file_ratio = 1;

/* the thing we are aggregating */
static aggmode_t aggmode = AGGMODE_NONE;

/* whether the user specified the seed */
static int seed_specified = 0;

/* the compression method to use when writing the file.
 * skCompMethodOptionsRegister() will set this to the default or
 * to the value the user specifies. */
static sk_compmethod_t comp_method;

/* handle to argc and argv used to write invocation into header of
 * output files. */
static int pargc;
static char **pargv;


/* OPTIONS SETUP */

typedef enum {
    /* the aggregate list--keep this set in sync with aggmode_t */
    OPT_IP_LIMIT, OPT_FLOW_LIMIT, OPT_PACKET_LIMIT, OPT_BYTE_LIMIT,
    OPT_BASENAME,
    OPT_SEED, OPT_SAMPLE_RATIO, OPT_FILE_RATIO,
    OPT_MAX_OUTPUTS
} appOptionsEnum;

/* value to subtract from appOptionsEnum to get a aggmode_t */
static unsigned int opt2agg_offset = OPT_IP_LIMIT;

static struct option appOptions[] = {
    {"ip-limit",     REQUIRED_ARG, 0, OPT_IP_LIMIT},
    {"flow-limit",   REQUIRED_ARG, 0, OPT_FLOW_LIMIT},
    {"packet-limit", REQUIRED_ARG, 0, OPT_PACKET_LIMIT},
    {"byte-limit",   REQUIRED_ARG, 0, OPT_BYTE_LIMIT},
    {"basename",     REQUIRED_ARG, 0, OPT_BASENAME},
    {"seed",         REQUIRED_ARG, 0, OPT_SEED},
    {"sample-ratio", REQUIRED_ARG, 0, OPT_SAMPLE_RATIO},
    {"file-ratio",   REQUIRED_ARG, 0, OPT_FILE_RATIO},
    {"max-outputs",  REQUIRED_ARG, 0, OPT_MAX_OUTPUTS},
    {0,0,0,0}           /* sentinel entry */
};

static const char *appHelp[] = {
    ("Begin new subfile when unique IP address count in current\n"
     "\tsubfile meets or exceeds this value"),
    ("Begin new sample file when flow count in current subfile\n"
     "\tmeets this value"),
    ("Begin new sample file when packet count across all\n"
     "\tflows in current subfile meets or exceeds this value"),
    ("Begin new sample file when byte count across all flows\n"
     "\tin current subfile meets or exceeds this value"),
    "Specify basename to use for output subfiles",
    "Seed the the random number generator with this value",
    ("Set ratio of records read to number written in sample\n"
     "\tfile (e.g., 100 means to write 1 out of 100 records). Def. 1"),
    ("Set ratio of sample file names generated to total number\n"
     "\twritten (e.g., 10 means 1 of every 10 files will be saved). Def. 1"),
    ("Write no more than this number of files to disk.\n"
     "\tDef. 999999999"),
    (char *)NULL
};



/* LOCAL FUNCTION PROTOTYPES */

static int  appOptionsHandler(clientData cData, int opt_index, char *opt_arg);
static int  processRec(const rwRec *input_rec);
static void newOutput(void);
static int  closeOutput(void);


/* FUNCTION DEFINITIONS */

/*
 *  appUsageLong();
 *
 *    Print complete usage information to USAGE_FH.  Pass this
 *    function to skOptionsSetUsageCallback(); skOptionsParse() will
 *    call this funciton and then exit the program when the --help
 *    option is given.
 */
static void
appUsageLong(
    void)
{
#define USAGE_MSG                                                            \
    ("--basename=F --{ip|flow|packet|byte}-limit=N [SWITCHES] [FILES]\n"     \
     "\tSplit a stream of SiLK Flow records into a set of flow files that\n" \
     "\teach contain a subset of the records.\n")

    FILE *fh = USAGE_FH;
    unsigned int i;

    fprintf(fh, "%s %s", skAppName(), USAGE_MSG);
    fprintf(fh, "\nSPLITTING CRITERION:\n");
    for (i = opt2agg_offset;
         appOptions[i].name && i < (opt2agg_offset+AGGMODE_NONE);
         ++i)
    {
        fprintf(fh, "--%s %s. %s\n", appOptions[i].name,
                SK_OPTION_HAS_ARG(appOptions[i]), appHelp[i]);
    }

    fprintf(fh, "\nSWITCHES:\n");
    skOptionsDefaultUsage(fh);
    for (i = 0; appOptions[i].name; ++i) {
        if (i >= opt2agg_offset && i < (opt2agg_offset+AGGMODE_NONE)) {
            continue;
        }
        fprintf(fh, "--%s %s. %s\n", appOptions[i].name,
                SK_OPTION_HAS_ARG(appOptions[i]), appHelp[i]);
    }
    skOptionsCtxOptionsUsage(optctx, fh);
    skOptionsNotesUsage(fh);
    skCompMethodOptionsUsage(fh);
    sksiteOptionsUsage(fh);

    fprintf(fh, ("\nNote: The --basename and one of the --*-limit"
                 " switches are required.\n"));
}


/*
 *  appTeardown()
 *
 *    Teardown all modules, close all files, and tidy up all
 *    application state.
 *
 *    This function is idempotent.
 */
static void
appTeardown(
    void)
{
    static int teardownFlag = 0;

    if (teardownFlag) {
        return;
    }
    teardownFlag = 1;

    closeOutput();
    skStreamDestroy(&stream_in);

    if (ips) {
        skIPSetDestroy(&ips);
    }

    skOptionsNotesTeardown();
    skOptionsCtxDestroy(&optctx);
    skAppUnregister();
}


/*
 *  appSetup(argc, argv);
 *
 *    Perform all the setup for this application include setting up
 *    required modules, parsing options, etc.  This function should be
 *    passed the same arguments that were passed into main().
 *
 *    Returns to the caller if all setup succeeds.  If anything fails,
 *    this function will cause the application to exit with a FAILURE
 *    exit status.
 */
static void
appSetup(
    int                 argc,
    char              **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    unsigned int optctx_flags;
    int rv;

    /* verify same number of options and help strings */
    assert((sizeof(appHelp)/sizeof(char *)) ==
           (sizeof(appOptions)/sizeof(struct option)));

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    /* hang onto argc and argv */
    pargc = argc;
    pargv = argv;

    optctx_flags = (SK_OPTIONS_CTX_INPUT_SILK_FLOW | SK_OPTIONS_CTX_ALLOW_STDIN
                    | SK_OPTIONS_CTX_XARGS | SK_OPTIONS_CTX_PRINT_FILENAMES);

    /* register the options */
    if (skOptionsCtxCreate(&optctx, optctx_flags)
        || skOptionsCtxOptionsRegister(optctx)
        || skOptionsRegister(appOptions, &appOptionsHandler, NULL)
        || skOptionsNotesRegister(NULL)
        || skCompMethodOptionsRegister(&comp_method)
        || sksiteOptionsRegister(SK_SITE_FLAG_CONFIG_FILE))
    {
        skAppPrintErr("Unable to register options");
        exit(EXIT_FAILURE);
    }

    /* register the teardown handler */
    if (atexit(appTeardown) < 0) {
        skAppPrintErr("Unable to register appTeardown() with atexit()");
        appTeardown();
        exit(EXIT_FAILURE);
    }

    /* parse the options */
    rv = skOptionsCtxOptionsParse(optctx, argc, argv);
    if (rv < 0) {
        skAppUsage();           /* never returns */
    }

    /* try to load site config file; if it fails, we will not be able
     * to resolve flowtype and sensor from input file names */
    sksiteConfigure(0);

    /*
     * We now check for correctness.  This implies:
     * A splitting criterion has been chosen.
     * An output stub name has been specified.
     */
    if (aggmode == AGGMODE_NONE) {
        skAppPrintErr("No splitting criterion chosen; you must specify one");
        exit(EXIT_FAILURE);
    }
    if (out_basename == NULL) {
        skAppPrintErr("You must specify the output files' basename");
        exit(EXIT_FAILURE);
    }

    /* need to initialize the state */
    current_sample_count = sample_ratio;

    /* create IPset if required */
    if (aggmode == AGGMODE_IPS) {
        skIPSetCreate(&ips, 0);
    }

    return;  /* OK */
}


/*
 *  status = appOptionsHandler(cData, opt_index, opt_arg);
 *
 *    This function is passed to skOptionsRegister(); it will be called
 *    by skOptionsParse() for each user-specified switch that the
 *    application has registered; it should handle the switch as
 *    required---typically by setting global variables---and return 1
 *    if the switch processing failed or 0 if it succeeded.  Returning
 *    a non-zero from from the handler causes skOptionsParse() to return
 *    a negative value.
 *
 *    The clientData in 'cData' is typically ignored; 'opt_index' is
 *    the index number that was specified as the last value for each
 *    struct option in appOptions[]; 'opt_arg' is the user's argument
 *    to the switch for options that have a REQUIRED_ARG or an
 *    OPTIONAL_ARG.
 */
static int
appOptionsHandler(
    clientData   UNUSED(cData),
    int                 opt_index,
    char               *opt_arg)
{
    aggmode_t new_aggmode;
    int rv;

    switch ((appOptionsEnum)opt_index) {
      case OPT_IP_LIMIT:
      case OPT_BYTE_LIMIT:
      case OPT_PACKET_LIMIT:
      case OPT_FLOW_LIMIT:
        new_aggmode = (aggmode_t)(opt_index - opt2agg_offset);
        if (aggmode != AGGMODE_NONE) {
            if (aggmode == new_aggmode) {
                skAppPrintErr("Invalid %s: Switch used multiple times",
                              appOptions[opt_index].name);
            } else {
                skAppPrintErr(("Can only give one splitting criterion\n"
                               "\tBoth %s and %s specified"),
                              appOptions[aggmode+opt2agg_offset].name,
                              appOptions[opt_index].name);
            }
            return 1;
        }
        aggmode = new_aggmode;
        rv = skStringParseUint64(&tag_limit, opt_arg, 1, 0);
        if (rv) {
            goto PARSE_ERROR;
        }
        break;

      case OPT_BASENAME:
        if (out_basename) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        out_basename = opt_arg;
        break;

      case OPT_SEED:
        {
            uint32_t t32 = 0;
            rv = skStringParseUint32(&t32, opt_arg, 0, 0);
            if (rv) {
                goto PARSE_ERROR;
            }
            srandom((unsigned int)t32);
            seed_specified = 1;
        }
        break;

      case OPT_SAMPLE_RATIO:
        rv = skStringParseUint32(&sample_ratio, opt_arg,
                                 1, (UINT32_MAX / sizeof(rwRec)));
        if (rv) {
            goto PARSE_ERROR;
        }
        break;

      case OPT_FILE_RATIO:
        rv = skStringParseUint32(&file_ratio, opt_arg, 1, 0);
        if (rv) {
            goto PARSE_ERROR;
        }
        break;

      case OPT_MAX_OUTPUTS:
        rv = skStringParseUint32(&max_outputs, opt_arg,
                                 1, MAX_OUTPUT_FILES);
        if (rv) {
            goto PARSE_ERROR;
        }
    }

    return 0;  /* OK */

  PARSE_ERROR:
    skAppPrintErr("Invalid %s '%s': %s",
                  appOptions[opt_index].name, opt_arg,
                  skStringParseStrerror(rv));
    return 1;
}


static int
closeOutput(
    void)
{
    int rv = 0;

    if (stream_out) {
        rv = skStreamClose(stream_out);
        if (rv) {
            skStreamPrintLastErr(stream_out, rv, &skAppPrintErr);
        }
        skStreamDestroy(&stream_out);
    }
    return rv;
}


/*
 *  newOutput();
 *
 *    Create a new data file using the basename and allocates a handle
 *    to it as the current file.
 */
static void
newOutput(
    void)
{
    static uint32_t sample_die_roll = 0;
    char datafn[PATH_MAX];
    int rv;

    if (file_ratio != 1) {
        if (0 == (output_ctr % file_ratio)) {
            sample_die_roll = (uint32_t)(random() % file_ratio);
        }
        if ((output_ctr % file_ratio) != sample_die_roll) {
            ++output_ctr;
            return;
        }
    }

    /* have we written the maximum number of output files? */
    if (max_outputs == 0) {
        exit(EXIT_SUCCESS);
    }
    --max_outputs;

    /* create new file name, open it, write the headers */
    snprintf(datafn, sizeof(datafn),  ("%s.%08" PRIu32 ".rwf"),
             out_basename, output_ctr);
    if ((rv = skStreamCreate(&stream_out, SK_IO_WRITE, SK_CONTENT_SILK_FLOW))
        || (rv = skStreamBind(stream_out, datafn))
        || (rv = skStreamSetCompressionMethod(stream_out, comp_method))
        || (rv = skOptionsNotesAddToStream(stream_out))
        || (rv = skHeaderAddInvocation(skStreamGetSilkHeader(stream_out),
                                       1, pargc, pargv))
        || (rv = skStreamOpen(stream_out))
        || (rv = skStreamWriteSilkHeader(stream_out)))
    {
        skStreamPrintLastErr(stream_out, rv, &skAppPrintErr);
        skStreamDestroy(&stream_out);
        exit(EXIT_FAILURE);
    }

    ++output_ctr;
}


/*
 *  int processRec(rwRec *rwrec)
 *
 *    Given a single record, it updates its count and states and
 *    determines whether or not it is time to move onto the next value
 *    in the dataset.
 */
static int
processRec(
    const rwRec        *rwrec)
{
    static uint32_t grab_index = 0;
    skipaddr_t ipaddr;
    int reset_status;
    int rv;

    reset_status = 0;

    /* if we are not processing every record, decide whether to
     * process the current record. */
    if (sample_ratio != 1) {
        if (current_sample_count == sample_ratio) {
            current_sample_count = 0;
            /* figure out which record of the next sample_ratio
             * records to process */
            grab_index = 1 + random() % sample_ratio;
        }
        ++current_sample_count;
        if (grab_index != current_sample_count) {
            return 0;
        }
    }

    /* open the output file if this is the first record.  this ensures
     * we only open output files when we have data to write to
     * them. */
    if (0 == tag_current) {
        newOutput();
    }

    if (stream_out) {
        rv = skStreamWriteRecord(stream_out, rwrec);
        if (SKSTREAM_ERROR_IS_FATAL(rv)) {
            skStreamPrintLastErr(stream_out, rv, &skAppPrintErr);
            skStreamDestroy(&stream_out);
            exit(EXIT_FAILURE);
        }
    }

    /*
     * What's going on here.  This routine actually determine when an
     * element of the partition is complete and we can safely go on to
     * the next element.  To do so, we update an internal count
     * (tag_current) with whatever values we got from the update.  The
     * increase is determined by the record and the splitting
     * criterion.  Once we have determined that the updated value
     * exceeds our per-partition limit (tag_limit), we close the file
     * and move onto the next one.
     */
    switch (aggmode) {
      case AGGMODE_IPS:
        rwRecMemGetSIP(rwrec, &ipaddr);
        if (!skIPSetCheckAddress(ips, &ipaddr)) {
            skIPSetInsertAddress(ips, &ipaddr, 0);
            tag_current++;
        }
        rwRecMemGetDIP(rwrec, &ipaddr);
        if (!skIPSetCheckAddress(ips, &ipaddr)) {
            skIPSetInsertAddress(ips, &ipaddr, 0);
            tag_current++;
        }
        if (tag_current >= tag_limit) {
            reset_status = 1;
            /* reset tree */
            skIPSetRemoveAll(ips);
        }
        break;

      case AGGMODE_FLOWS:
        ++tag_current;
        if (tag_current >= tag_limit) {
            reset_status = 1;
        }
        break;

      case AGGMODE_PKTS:
        tag_current += rwRecGetPkts(rwrec);
        if (tag_current >= tag_limit) {
            reset_status = 1;
        }
        break;

      case AGGMODE_BYTES:
        tag_current += rwRecGetBytes(rwrec);
        if (tag_current >= tag_limit) {
            reset_status = 1;
        }
        break;

      case AGGMODE_NONE:
        skAbortBadCase(aggmode);
    }

    if (reset_status) {
        /* close current file */
        if (closeOutput()) {
            exit(EXIT_FAILURE);
        }
        tag_current = 0;
    }

    return 0;
}


int main(int argc, char **argv)
{
    struct timeval tv;
    rwRec in_rec;
    int ret_val = EXIT_SUCCESS;
    int rv;

    appSetup(argc, argv);                       /* never returns on error */

    if (!seed_specified) {
        gettimeofday(&tv, NULL);
        srandom((unsigned int) ((tv.tv_sec + tv.tv_usec) / getpid()));
    }

    /* for all inputs, read all records */
    /* process input */
    while ((rv = skOptionsCtxNextSilkFile(optctx, &stream_in, &skAppPrintErr))
           == 0)
    {
        while ((rv = skStreamReadRecord(stream_in, &in_rec)) == SKSTREAM_OK) {
            processRec(&in_rec);
        }
        if (SKSTREAM_ERR_EOF != rv) {
            skStreamPrintLastErr(stream_in, rv, &skAppPrintErr);
            ret_val = EXIT_FAILURE;
        }
        skStreamDestroy(&stream_in);
    }
    if (rv < 0) {
        ret_val = EXIT_FAILURE;
    }

    if (closeOutput()) {
        exit(EXIT_FAILURE);
    }

    return ret_val;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
