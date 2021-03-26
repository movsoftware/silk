/*
** Copyright (C) 2017-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  rwaggbagtool.c
 *
 *    rwaggbagtool performs various operations on Aggregate Bag files.
 *    It can add them, subtract them, manipulate their fields, and
 *    convert them to an IPset or a (normal) Bag.
 *
 *  Mark Thomas
 *  February 2017
 */


#include <silk/silk.h>

RCSIDENT("$SiLK: rwaggbagtool.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/skaggbag.h>
#include <silk/skbag.h>
#include <silk/skcountry.h>
#include <silk/skipaddr.h>
#include <silk/skipset.h>
#include <silk/skstream.h>
#include <silk/skstringmap.h>
#include <silk/sksite.h>
#include <silk/skvector.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* where to write --help output */
#define USAGE_FH stdout

/* size to use for arrays that hold field IDs */
#define AGGBAGTOOL_ARRAY_SIZE       65536

/* What to do when malloc() fails */
#define EXIT_NO_MEMORY                                               \
    do {                                                             \
        skAppPrintOutOfMemory(NULL);                                 \
        exit(EXIT_FAILURE);                                          \
    } while(0)

#define ERR_GET_COUNT(egc_key, egc_err)                                 \
    skAppPrintErr("Error getting count for key (%s): %s",               \
                  skipaddrString(ipbuf1, &(egc_key).val.addr, 0),       \
                  skAggBagStrerror(egc_err))

#define ERR_SET_COUNT(esc_key, esc_val, esc_err)                        \
    skAppPrintErr(("Error setting key=>counter (%s=>%" PRIu64 ": %s"),  \
                  skipaddrString(ipbuf1, &(esc_key).val.addr, 0),       \
                  (esc_val).val.u64, skAggBagStrerror(esc_err))

#define ERR_REMOVE_KEY(erk_key, erk_err)                                \
    skAppPrintErr("Error removing key (%s): %s",                        \
                  skipaddrString(ipbuf1, &(erk_key).val.addr, 0),       \
                  skAggBagStrerror(erk_err))

#define ERR_ITERATOR(ei_description, ei_err)                       \
    skAppPrintErr("Error in %s aggbag iterator: %s",               \
                  ei_description, skAggBagStrerror(ei_err))

/* what action the user wants to do */
typedef enum action_en {
    AB_ACTION_UNSET, AB_ACTION_ADD, AB_ACTION_SUBTRACT
} action_t;

/* parsed_value_t is a structure to hold the unparsed value, an
 * indication as to whether the value is active, and the parsed
 * value. there is an array of these for all possible field
 * identifiers */
typedef struct parsed_value_st {
    /* True if the field is part of the key or counter */
    unsigned        pv_is_used  : 1;
    /* True if the field was specified by --constant-field and its
     * value only needs to be computed once */
    unsigned        pv_is_const : 1;
    /* True if the value of the field is fixed for this input file
     * because either it was not mentioned in file's title line or
     * because it was mentioned in --constant-field */
    unsigned        pv_is_fixed : 1;
    union parsed_value_v_un {
        uint64_t        pv_int;
        sktime_t        pv_time;
        skipaddr_t      pv_ip;
    }               pv;
} parsed_value_t;

/* minmax_value_t holds a field ID and a parsed values representing
 * an argument to the --min-field or --max-field switches */
typedef struct minmax_value_st {
    /* the field ID */
    uint32_t            mmv_field;
    /* whether min(0) or max(1) */
    uint32_t            mmv_is_max: 1;
    /* the parsed value */
    parsed_value_t      mmv_val;
} minmax_value_t;

/* setmask_value_t holds a field ID and an IPset path representing an
 * argument to the --set-intersect or --set-complement switches */
typedef struct setmask_value_st {
    /* the field ID */
    uint32_t            sv_field;
    /* whether intersect(0) or complement(1) */
    uint32_t            sv_is_complement: 1;
    /* the IPset */
    skipset_t          *sv_ipset;
} setmask_value_t;


/* LOCAL VARIABLES */

/* where to write the resulting AggBag, Bag, or IPset file */
static skstream_t *out_stream = NULL;

/* the output AggBag that we create or that is used as the basis for
 * the Bag or IPset */
static sk_aggbag_t *out_ab = NULL;

/* What action the user wants to take (add, subtract, etc) */
static action_t action = AB_ACTION_UNSET;

/* Index of current file argument in argv */
static int arg_index = 0;

/* the compression method to use when writing the file.
 * skCompMethodOptionsRegister() will set this to the default or
 * to the value the user specifies. */
static sk_compmethod_t comp_method;

/* available fields */
static sk_stringmap_t *field_map = NULL;

/* the IDs for the fields specified by the --insert-field switch;
 * switch may be repeated; vector of uint32_t */
static sk_vector_t *insert_field = NULL;

/* the IDs for the fields specified by the --remove-fields switch;
 * vector of uint32_t */
static sk_vector_t *remove_fields = NULL;

/* the IDs for the fields specified by the --select-fields switch;
 * vector of uint32_t */
static sk_vector_t *select_fields = NULL;

/* arguments to the --min-field and --max-field switches; vector of
 * minmax_value_t */
static sk_vector_t *minmax_fields = NULL;

/* arguments to the --min-field and --max-field switches; vector of
 * setmask_value_t */
static sk_vector_t *setmask_fields = NULL;

/* an array capable of holding a parsed value for every possible
 * sk_aggbag_type_t, indexed by that ID.  It holds the parsed values
 * for fields set by --insert-field. */
static parsed_value_t parsed_value[AGGBAGTOOL_ARRAY_SIZE];

/* names the key and counter fields to use when --to-bag is specified */
static const char *to_bag;

/* names the field to use when --to-ipset is specified */
static const char *to_ipset;

/* options for writing the IPset when --to-ipset is specified */
static skipset_options_t ipset_options;

/* whether the --note-strip flag was specified */
static int note_strip = 0;


/* OPTIONS SETUP */

typedef enum {
    OPT_ADD,
    OPT_SUBTRACT,
    OPT_INSERT_FIELD,
    OPT_REMOVE_FIELDS,
    OPT_SELECT_FIELDS,
    OPT_TO_BAG,
    OPT_TO_IPSET,
    OPT_MIN_FIELD,
    OPT_MAX_FIELD,
    OPT_SET_INTERSECT,
    OPT_SET_COMPLEMENT,
    OPT_OUTPUT_PATH
} appOptionsEnum;

static struct option appOptions[] = {
    {"add",                  NO_ARG,       0, OPT_ADD},
    {"subtract",             NO_ARG,       0, OPT_SUBTRACT},
    {"insert-field",         REQUIRED_ARG, 0, OPT_INSERT_FIELD},
    {"remove-fields",        REQUIRED_ARG, 0, OPT_REMOVE_FIELDS},
    {"select-fields",        REQUIRED_ARG, 0, OPT_SELECT_FIELDS},
    {"to-bag",               REQUIRED_ARG, 0, OPT_TO_BAG},
    {"to-ipset",             REQUIRED_ARG, 0, OPT_TO_IPSET},
    {"min-field",            REQUIRED_ARG, 0, OPT_MIN_FIELD},
    {"max-field",            REQUIRED_ARG, 0, OPT_MAX_FIELD},
    {"set-intersect",        REQUIRED_ARG, 0, OPT_SET_INTERSECT},
    {"set-complement",       REQUIRED_ARG, 0, OPT_SET_COMPLEMENT},
    {"output-path",          REQUIRED_ARG, 0, OPT_OUTPUT_PATH},
    {0,0,0,0}                /* sentinel entry */
};

static const char *appHelp[] = {
    ("Add the counters for each key across all Aggregate Bag files.\n"
     "\tKey-fields in all Aggregate Bag files must match"),
    ("Subtract from first Aggregate Bag file all subsequent\n"
     "\tAggregate Bag files. Key-fields in all Aggregate Bag files must match"),
    ("Given an argument of FIELD=VALUE, if an input\n"
     "\tAggregate Bag file does not contain FIELD or if FIELD has been\n"
     "\tremoved by --remove-fields, insert FIELD into the Aggregate Bag\n"
     "\tand set its value to VALUE.  May be repeated to set multiple FIELDs"),
    ("Remove this comma-separated list of fields from each\n"
     "\tAggregate Bag input file.  May not be used with --select-fields,\n"
     "\t--to-bag, or --to-ipset"),
    ("Remove all fields from each Aggregate Bag input file\n"
     "\tEXCEPT those in this comma-separated list of fields.  May not be\n"
     "\tused with --remove-fields, --to-bag, or --to-ipset"),
    ("Given an argument of FIELD,FIELD, use these two fields\n"
     "\tas the key and counter, respectively, for a new Bag file.  May not\n"
     "\tbe used with --select-fields, --remove-fields, or --to-ipset"),
    ("Given an argument of FIELD, use the values in this field\n"
     "\tof the Aggregate Bag file to create a new IPset file.  May not be\n"
     "\tused with --select-fields, --remove-fields, or --to-bag"),
    ("Given an argument of FIELD=VALUE, remove from the\n"
     "\tAggregate Bag all rows where FIELD has a value less than VALUE.\n"
     "\tThis occurs immediately before producing output. May be repeated"),
    ("Given an argument of FIELD=VALUE, remove from the\n"
     "\tAggregate Bag all rows where FIELD has a value greater than VALUE.\n"
     "\tThis occurs immediately before producing output. May be repeated"),
    ("Given an argument of FIELD=SET_FILE, remove from the\n"
     "\tAggregate Bag all rows where FIELD is not in the IPset file SET_FILE.\n"
     "\tThis occurs immediately before producing output. May be repeated"),
    ("Given an argument of FIELD=SET_FILE, remove from the\n"
     "\tAggregate Bag all rows where FIELD is in the IPset file SET_NAME.\n"
     "\tThis occurs immediately before producing output. May be repeated"),
    "Write the output to this stream or file. Def. stdout",
    (char *)NULL
};


/* LOCAL FUNCTION PROTOTYPES */

static int  appOptionsHandler(clientData cData, int opt_index, char *opt_arg);
static int  createStringmap(void);
static int  chooseAction(int opt_index);
static int  abtoolCheckFields(void);
static int  writeOutput(void);
static int  parseInsertField(const char *argument);
static int  parseMinMax(int opt_index, const char *str_argument);
static ssize_t parseSetMask(int opt_index, const char *str_argument);
static int  parseFieldList(sk_vector_t **vec, int opt_idx, const char *fields);


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
#define USAGE_MSG                                                           \
    ("[SWITCHES] [AGGBAG_FILES]\n"                                          \
     "\tPerform operations on one or more Aggregate Bag files, creating\n"  \
     "\ta new Aggregate Bag file which is written to the standard output\n" \
     "\tor the --output-path.  Read Aggregate Bag files from the named\n"   \
     "\targuments or from the standard input.\n")

    FILE *fh = USAGE_FH;
    int i;

    fprintf(fh, "%s %s", skAppName(), USAGE_MSG);
    fprintf(fh, "\nSWITCHES:\n");
    skOptionsDefaultUsage(fh);
    for (i = 0; appOptions[i].name; ++i) {
        fprintf(fh, "--%s %s. ", appOptions[i].name,
                SK_OPTION_HAS_ARG(appOptions[i]));
        switch (appOptions[i].val) {
          case OPT_TO_IPSET:
            fprintf(fh, "%s\n", appHelp[i]);
            skIPSetOptionsUsageRecordVersion(fh);
            break;
          default:
            fprintf(fh, "%s\n", appHelp[i]);
            break;
        }
    }
    skOptionsNotesUsage(fh);
    skCompMethodOptionsUsage(fh);
    sksiteOptionsUsage(fh);
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

    /* free the output aggbag, stream, and set */
    skAggBagDestroy(&out_ab);
    skStreamDestroy(&out_stream);
    (void)skStringMapDestroy(field_map);
    field_map = NULL;
    skIPSetOptionsTeardown();

    /* free all vectors */
    skVectorDestroy(insert_field);
    skVectorDestroy(remove_fields);
    skVectorDestroy(select_fields);
    skVectorDestroy(minmax_fields);
    if (setmask_fields) {
        setmask_value_t *sv;
        size_t i;
        i = skVectorGetCount(setmask_fields);
        while (i > 0) {
            --i;
            sv = (setmask_value_t *)skVectorGetValuePointer(setmask_fields, i);
            skIPSetDestroy(&sv->sv_ipset);
        }
        skVectorDestroy(setmask_fields);
    }

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
    int rv;

    /* verify same number of options and help strings */
    assert((sizeof(appHelp)/sizeof(char *)) ==
           (sizeof(appOptions)/sizeof(struct option)));

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    /* initialize globals */
    memset(&ipset_options, 0, sizeof(skipset_options_t));

    /* register the options */
    if (skOptionsRegister(appOptions, &appOptionsHandler, NULL)
        || skIPSetOptionsRegisterRecordVersion(&ipset_options,
                                               "ipset-record-version")
        || skOptionsNotesRegister(&note_strip)
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

    /* initialize string-map of field identifiers, and add the locally
     * defined fields. */
    if (createStringmap()) {
        skAppPrintErr("Unable to setup fields stringmap");
        exit(EXIT_FAILURE);
    }

    /* parse the options; returns the index into argv[] of the first
     * non-option or < 0 on error  May re-arrange argv[]. */
    arg_index = skOptionsParse(argc, argv);
    assert(arg_index <= argc);
    if (arg_index < 0) {
        /* options parsing should print error */
        skAppUsage();             /* never returns */
    }

    /* check that the field sets make sense */
    if (abtoolCheckFields()) {
        exit(EXIT_FAILURE);
    }

    /* The default action is to add the aggbags together */
    if (AB_ACTION_UNSET == action) {
        action = AB_ACTION_ADD;
    }

    if ((arg_index == argc) && (FILEIsATty(stdin))) {
        skAppPrintErr("No input files on command line and"
                      " stdin is connected to a terminal");
        skAppUsage();
    }

    /* Set the default output location */
    if (out_stream == NULL) {
        if ((rv = skStreamCreate(&out_stream, SK_IO_WRITE, SK_CONTENT_SILK))
            || (rv = skStreamBind(out_stream, "-")))
        {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            skStreamDestroy(&out_stream);
            exit(EXIT_FAILURE);
        }
    }

    /* Open the output file */
    if ((rv = skStreamSetCompressionMethod(out_stream, comp_method))
        || (rv = skStreamOpen(out_stream)))
    {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        skStreamDestroy(&out_stream);
        exit(EXIT_FAILURE);
    }
    skOptionsNotesTeardown();

    return;                     /* OK */
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
    int rv;

    switch ((appOptionsEnum)opt_index) {
      case OPT_ADD:
      case OPT_SUBTRACT:
        if (chooseAction(opt_index)) {
            return 1;
        }
        break;

      case OPT_INSERT_FIELD:
        if (parseInsertField(opt_arg)) {
            return 1;
        }
        break;

      case OPT_REMOVE_FIELDS:
        if (remove_fields) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        if (parseFieldList(&remove_fields, opt_index, opt_arg)) {
            return 1;
        }
        break;

      case OPT_SELECT_FIELDS:
        if (select_fields) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        if (parseFieldList(&select_fields, opt_index, opt_arg)) {
            return 1;
        }
        break;

      case OPT_TO_BAG:
        if (to_bag) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        to_bag = opt_arg;
        break;

      case OPT_TO_IPSET:
        if (to_ipset) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        to_ipset = opt_arg;
        break;

      case OPT_MIN_FIELD:
      case OPT_MAX_FIELD:
        if (parseMinMax(opt_index, opt_arg)) {
            return 1;
        }
        break;

      case OPT_SET_INTERSECT:
      case OPT_SET_COMPLEMENT:
        if (parseSetMask(opt_index, opt_arg)) {
            return 1;
        }
        break;

      case OPT_OUTPUT_PATH:
        if (out_stream) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        if ((rv = skStreamCreate(&out_stream, SK_IO_WRITE, SK_CONTENT_SILK))
            || (rv = skStreamBind(out_stream, opt_arg)))
        {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            skStreamDestroy(&out_stream);
            return 1;
        }
        break;
    }

    return 0;                   /* OK */
}


/*
 *  ok = createStringmap();
 *
 *    Create the global 'field_map'.  Return 0 on success, or -1 on
 *    failure.
 */
static int
createStringmap(
    void)
{
    sk_stringmap_status_t sm_err;
    sk_stringmap_entry_t sm_entry;
    sk_aggbag_type_iter_t iter;
    sk_aggbag_type_t type;
    const unsigned int key_counter[] = {SK_AGGBAG_KEY, SK_AGGBAG_COUNTER};
    unsigned int i;

    memset(&sm_entry, 0, sizeof(sm_entry));

    sm_err = skStringMapCreate(&field_map);
    if (sm_err) {
        skAppPrintErr("Unable to create string map");
        return -1;
    }

    for (i = 0; i < sizeof(key_counter)/sizeof(key_counter[0]); ++i) {
        skAggBagFieldTypeIteratorBind(&iter, key_counter[i]);
        while ((sm_entry.name = skAggBagFieldTypeIteratorNext(&iter, &type))
               != NULL)
        {
            sm_entry.id = type;
            sm_err = skStringMapAddEntries(field_map, 1, &sm_entry);
            if (sm_err) {
                skAppPrintErr("Unable to add %s field named '%s': %s",
                              ((SK_AGGBAG_KEY == key_counter[i])
                               ? "key" : "counter"),
                              sm_entry.name, skStringMapStrerror(sm_err));
                return -1;
            }
            if (SKAGGBAG_FIELD_ANY_COUNTRY == type) {
                break;
            }
        }
    }

    return 0;
}


/*
 *    Map the current option 'opt_index' to an 'action_t' and set the
 *    global 'action' variable.
 */
static int
chooseAction(
    int                 opt_index)
{
    /* map an action_t to an appOptionsEnum */
    const struct action_map_st {
        action_t            am_action;
        appOptionsEnum      am_option;
    } action_map[] = {
        {AB_ACTION_ADD,         OPT_ADD},
        {AB_ACTION_SUBTRACT,    OPT_SUBTRACT}
    };
    action_t new_act;
    size_t i;

    new_act = AB_ACTION_UNSET;
    for (i = 0; i < sizeof(action_map)/sizeof(action_map[0]); ++i) {
        if (action_map[i].am_option == (appOptionsEnum)opt_index) {
            new_act = action_map[i].am_action;
            break;
        }
    }
    if (AB_ACTION_UNSET == new_act) {
        skAbortBadCase(new_act);
    }

    if (AB_ACTION_UNSET == action) {
        /* Success */
        action = new_act;
        return 0;
    }

    if (action == new_act) {
        skAppPrintErr("Invalid %s: Switch used multiple times",
                      appOptions[opt_index].name);
    } else {
        appOptionsEnum old_opt = OPT_OUTPUT_PATH;

        for (i = 0; i < sizeof(action_map)/sizeof(action_map[0]); ++i) {
            if (action_map[i].am_action == action) {
                old_opt = action_map[i].am_option;
                break;
            }
        }
        if (OPT_OUTPUT_PATH == old_opt) {
            skAbortBadCase(new_act);
        }
        skAppPrintErr("Switches --%s and --%s are incompatible",
                      appOptions[old_opt].name,
                      appOptions[opt_index].name);
    }

    return 1;
}


/*
 *    Parse a "NAME=VALUE" style argument given to the --insert-field,
 *    --min-field, or --max-field switch, where 'opt_index' is the
 *    switch and 'str_argument' is its argument.  Set the referent of
 *    'id' to the sk_aggbag_type_t that represents the field's ID.
 *    Set the referent of 'pv' to the result of parsing the
 *    value---all fields of 'pv' are set to 0 and the appropriate
 *    field of the 'pv' union is set to the value.
 *
 *    Return 0 on success; on error, print an error message and return
 *    non-zero.
 */
static int
parseSingleField(
    int                 opt_index,
    const char         *str_argument,
    uint32_t           *id,
    parsed_value_t     *pv)
{
    sk_stringmap_entry_t *sm_entry;
    sk_stringmap_status_t sm_err;
    char *argument;
    char *str_value;
    char *eq;
    sktime_t tmp_time;
    uint8_t tcp_flags;
    int parse_error = 0;
    int rv = -1;

    assert(str_argument);
    assert(id);
    assert(pv);

    memset(pv, 0, sizeof(*pv));

    argument = strdup(str_argument);
    if (NULL == argument) {
        goto END;
    }

    /* find the '=' */
    eq = strchr(argument, '=');
    if (NULL == eq) {
        skAppPrintErr(("Invalid %s '%s': Expected FIELD_NAME=VALUE"
                       " but unable to find '=' character"),
                      appOptions[opt_index].name, argument);
        goto END;
    }

    /* ensure a value is given */
    str_value = eq + 1;
    while (*str_value && isspace((int)*str_value)) {
        ++str_value;
    }
    if ('\0' == *str_value) {
        skAppPrintErr("Invalid %s '%s': No value specified for field",
                      appOptions[opt_index].name, argument);
        goto END;
    }

    /* split into name and value */
    *eq = '\0';
    str_value = eq + 1;

    /* find the field with that name */
    sm_err = skStringMapGetByName(field_map, argument, &sm_entry);
    if (sm_err) {
        skAppPrintErr("Invalid %s: Unable to find a field named '%s': %s",
                      appOptions[opt_index].name, argument,
                      skStringMapStrerror(sm_err));
        goto END;
    }

    /* parse the value */
    parse_error = 1;
    switch (sm_entry->id) {
      case SKAGGBAG_FIELD_RECORDS:
      case SKAGGBAG_FIELD_SUM_BYTES:
      case SKAGGBAG_FIELD_SUM_PACKETS:
      case SKAGGBAG_FIELD_SUM_ELAPSED:
      case SKAGGBAG_FIELD_PACKETS:
      case SKAGGBAG_FIELD_BYTES:
      case SKAGGBAG_FIELD_ELAPSED:
      case SKAGGBAG_FIELD_CUSTOM_KEY:
      case SKAGGBAG_FIELD_CUSTOM_COUNTER:
        rv = skStringParseUint64(&pv->pv.pv_int, str_value, 0, UINT64_MAX);
        if (rv) {
            goto END;
        }
        break;

      case SKAGGBAG_FIELD_SPORT:
      case SKAGGBAG_FIELD_DPORT:
      case SKAGGBAG_FIELD_ANY_PORT:
      case SKAGGBAG_FIELD_INPUT:
      case SKAGGBAG_FIELD_OUTPUT:
      case SKAGGBAG_FIELD_ANY_SNMP:
      case SKAGGBAG_FIELD_APPLICATION:
        rv = skStringParseUint64(&pv->pv.pv_int, str_value, 0, UINT16_MAX);
        if (rv) {
            goto END;
        }
        break;

      case SKAGGBAG_FIELD_PROTO:
      case SKAGGBAG_FIELD_ICMP_TYPE:
      case SKAGGBAG_FIELD_ICMP_CODE:
        rv = skStringParseUint64(&pv->pv.pv_int, str_value, 0, UINT8_MAX);
        if (rv) {
            goto END;
        }
        break;

      case SKAGGBAG_FIELD_SIPv4:
      case SKAGGBAG_FIELD_DIPv4:
      case SKAGGBAG_FIELD_NHIPv4:
      case SKAGGBAG_FIELD_ANY_IPv4:
        if (NULL == str_value) {
            skipaddrClear(&pv->pv.pv_ip);
            break;
        }
        rv = skStringParseIP(&pv->pv.pv_ip, str_value);
        if (rv) {
            goto END;
        }
#if SK_ENABLE_IPV6
        if (skipaddrIsV6(&pv->pv.pv_ip)
            && skipaddrV6toV4(&pv->pv.pv_ip, &pv->pv.pv_ip))
        {
            /* FIXME: Need to produce some error code */
        }
#endif  /* SK_ENABLE_IPV6 */
        break;

      case SKAGGBAG_FIELD_SIPv6:
      case SKAGGBAG_FIELD_DIPv6:
      case SKAGGBAG_FIELD_NHIPv6:
      case SKAGGBAG_FIELD_ANY_IPv6:
        if (NULL == str_value) {
            skipaddrClear(&pv->pv.pv_ip);
            skipaddrSetVersion(&pv->pv.pv_ip, 6);
            break;
        }
        rv = skStringParseIP(&pv->pv.pv_ip, str_value);
        if (rv) {
            goto END;
        }
#if SK_ENABLE_IPV6
        if (!skipaddrIsV6(&pv->pv.pv_ip)) {
            skipaddrV4toV6(&pv->pv.pv_ip, &pv->pv.pv_ip);
        }
#endif  /* SK_ENABLE_IPV6 */
        break;

      case SKAGGBAG_FIELD_STARTTIME:
      case SKAGGBAG_FIELD_ENDTIME:
      case SKAGGBAG_FIELD_ANY_TIME:
        rv = skStringParseDatetime(&tmp_time, str_value, NULL);
        if (rv) {
            /* FIXME: Allow small integers as epoch times? */
            goto END;
        }
        pv->pv.pv_int = sktimeGetSeconds(tmp_time);
        break;

      case SKAGGBAG_FIELD_FLAGS:
      case SKAGGBAG_FIELD_INIT_FLAGS:
      case SKAGGBAG_FIELD_REST_FLAGS:
        rv = skStringParseTCPFlags(&tcp_flags, str_value);
        if (rv) {
            goto END;
        }
        break;

      case SKAGGBAG_FIELD_TCP_STATE:
        rv = skStringParseTCPState(&tcp_flags, str_value);
        if (rv) {
            goto END;
        }
        break;

      case SKAGGBAG_FIELD_SID:
        if (isdigit((int)*str_value)) {
            rv = skStringParseUint64(&pv->pv.pv_int, str_value, 0,
                                     SK_INVALID_SENSOR-1);
            if (rv) {
                goto END;
            }
        } else {
            pv->pv.pv_int = sksiteSensorLookup(str_value);
        }
        break;

      case SKAGGBAG_FIELD_FTYPE_CLASS:
        pv->pv.pv_int = sksiteClassLookup(str_value);
        break;

      case SKAGGBAG_FIELD_FTYPE_TYPE:
        pv->pv.pv_int = (sksiteFlowtypeLookupByClassIDType(
                             parsed_value[SKAGGBAG_FIELD_FTYPE_CLASS].pv.pv_int,
                             str_value));
        break;

      case SKAGGBAG_FIELD_SIP_COUNTRY:
      case SKAGGBAG_FIELD_DIP_COUNTRY:
      case SKAGGBAG_FIELD_ANY_COUNTRY:
        pv->pv.pv_int = skCountryNameToCode(str_value);
        break;

      default:
        break;
    }

    *id = sm_entry->id;
    parse_error = 0;
    rv = 0;

  END:
    if (parse_error) {
        skAppPrintErr("Invalid %s: Error parsing %s value '%s': %s",
                      appOptions[opt_index].name, argument, str_value,
                      skStringParseStrerror(rv));
    }
    free(argument);
    return rv;
}


/*
 *    Parse the NAME=VALUE argument to the --insert-field switch.  Set
 *    the appropriate field in the global 'parsed_value' array to the
 *    value and update the global 'insert_field' vector with the
 *    numeric IDs of that field.
 *
 *    Return 0 on success or -1 on failure.
 */
static int
parseInsertField(
    const char         *str_argument)
{
    parsed_value_t tmp_pv;
    uint32_t id;

    if (parseSingleField(OPT_INSERT_FIELD, str_argument, &id, &tmp_pv)) {
        return -1;
    }
    assert(id < AGGBAGTOOL_ARRAY_SIZE);

    if (parsed_value[id].pv_is_used) {
        skAppPrintErr("Invalid %s: A value for '%s' is already set",
                      appOptions[OPT_INSERT_FIELD].name,
                      skAggBagFieldTypeGetName((sk_aggbag_type_t)id));
        return -1;
    }

    tmp_pv.pv_is_used = 1;
    parsed_value[id] = tmp_pv;

    if (NULL == insert_field) {
        insert_field = skVectorNew(sizeof(uint32_t));
        if (NULL == insert_field) {
            skAppPrintOutOfMemory("vector");
            return -1;
        }
    }
    if (skVectorAppendValue(insert_field, &id)) {
        skAppPrintOutOfMemory("vector element");
        return -1;
    }

    return 0;
}


/*
 *    Parse the NAME=VALUE argument to the --min-field or --max-field
 *    switch and append the result to the global 'minmax_fields'
 *    vector.
 *
 *    Return 0 on success or -1 on failure.
 */
static int
parseMinMax(
    int                 opt_index,
    const char         *str_argument)
{
    minmax_value_t mmv;

    assert(OPT_MAX_FIELD == opt_index || OPT_MIN_FIELD == opt_index);

    memset(&mmv, 0, sizeof(mmv));
    mmv.mmv_is_max = (OPT_MAX_FIELD == opt_index);

    if (parseSingleField(opt_index, str_argument, &mmv.mmv_field,&mmv.mmv_val))
    {
        return -1;
    }

    if (NULL == minmax_fields) {
        minmax_fields = skVectorNew(sizeof(minmax_value_t));
        if (NULL == minmax_fields) {
            skAppPrintOutOfMemory("vector");
            return -1;
        }
    }
    if (skVectorAppendValue(minmax_fields, &mmv)) {
        skAppPrintOutOfMemory("vector element");
        return -1;
    }

    return 0;
}


/*
 *    Parse the NAME=SETFILE argument to the --set-intersect or
 *    --set-complement switch and append the result to the global
 *    'setmask_fields' vector.
 *
 *    Return 0 on success or -1 on failure.
 */
static ssize_t
parseSetMask(
    int                 opt_index,
    const char         *str_argument)
{
    sk_stringmap_entry_t *sm_entry;
    sk_stringmap_status_t sm_err;
    char *argument;
    char *cp;
    char *eq;
    setmask_value_t sv;
    skstream_t *stream = NULL;
    ssize_t rv = -1;

    assert(str_argument);
    assert(OPT_SET_INTERSECT == opt_index
           || OPT_SET_COMPLEMENT == opt_index);

    memset(&sv, 0, sizeof(sv));
    sv.sv_is_complement = (OPT_SET_COMPLEMENT == opt_index);

    argument = strdup(str_argument);
    if (NULL == argument) {
        goto END;
    }

    /* find the '=' */
    eq = strchr(argument, '=');
    if (NULL == eq) {
        skAppPrintErr(("Invalid %s '%s': Expected FIELD_NAME=SET_FILE"
                       " but unable to find '=' character"),
                      appOptions[opt_index].name, argument);
        goto END;
    }

    /* ensure a value is given */
    cp = eq + 1;
    while (*cp && isspace((int)*cp)) {
        ++cp;
    }
    if ('\0' == *cp) {
        skAppPrintErr("Invalid %s '%s': No file name specified for field",
                      appOptions[opt_index].name, argument);
        goto END;
    }

    /* split into name and value */
    *eq = '\0';
    cp = eq + 1;

    /* find the field with that name */
    sm_err = skStringMapGetByName(field_map, argument, &sm_entry);
    if (sm_err) {
        skAppPrintErr("Invalid %s: Unable to find a field named '%s': %s",
                      appOptions[opt_index].name, argument,
                      skStringMapStrerror(sm_err));
        goto END;
    }
    sv.sv_field = sm_entry->id;

#if 0
    /* is this an IP address? */
    switch (sv.sv_field) {
      case SKAGGBAG_FIELD_SIPv4:
      case SKAGGBAG_FIELD_DIPv4:
      case SKAGGBAG_FIELD_NHIPv4:
      case SKAGGBAG_FIELD_ANY_IPv4:
      case SKAGGBAG_FIELD_SIPv6:
      case SKAGGBAG_FIELD_DIPv6:
      case SKAGGBAG_FIELD_NHIPv6:
      case SKAGGBAG_FIELD_ANY_IPv6:
        break;
      default:
        skAppPrintErr("Invalid %s: Ignoring switch for non-IP field %s",
                      appOptions[opt_index].name, argument);
        rv = 0;
        goto END;
    }
#endif  /* 0 */

    /* read the IPset */
    if ((rv = skStreamCreate(&stream, SK_IO_READ, SK_CONTENT_SILK))
        || (rv = skStreamBind(stream, cp))
        || (rv = skStreamOpen(stream)))
    {
        skStreamPrintLastErr(stream, rv, &skAppPrintErr);
        rv = -1;
        goto END;
    }

    rv = skIPSetRead(&sv.sv_ipset, stream);
    if (rv) {
        if (SKIPSET_ERR_FILEIO == rv) {
            skStreamPrintLastErr(stream, skStreamGetLastReturnValue(stream),
                                 &skAppPrintErr);
        } else {
            skAppPrintErr("Unable to read IPset from '%s': %s",
                          cp, skIPSetStrerror(rv));
        }
        rv = -1;
        goto END;
    }
    rv = -1;

    if (NULL == setmask_fields) {
        setmask_fields = skVectorNew(sizeof(setmask_value_t));
        if (NULL == setmask_fields) {
            skAppPrintOutOfMemory("vector");
            skIPSetDestroy(&sv.sv_ipset);
            goto END;
        }
    }
    if (skVectorAppendValue(setmask_fields, &sv)) {
        skAppPrintOutOfMemory("vector element");
        skIPSetDestroy(&sv.sv_ipset);
        goto END;
    }

    rv = 0;

  END:
    skStreamDestroy(&stream);
    free(argument);
    return rv;
}


#if 0
/* support for the --change-field switch */

/* the IDs for the fields specified by the --change-field switch;
 * switch may be repeated */
static sk_vector_t *change_field = NULL;

/*     {"change-field",         REQUIRED_ARG, 0, OPT_CHANGE_FIELD}, */
/*     ("Given an argument of FIELD1=FIELD2, if the input has a\n" */
/*      "\tfield whose type is FIELD1, change its type to FIELD2." */
/*      " May be repeated"), */

/* static int  parseChangeField(const char *argument); */

/*       case OPT_CHANGE_FIELD: */
/*         if (parseChangeField(opt_arg)) { */
/*             return 1; */
/*         } */
/*         break; */

/*
 *    Parse the TYPE1=TYPE2 argument to the --change-field switch.
 *    Set the appropriate field in the global 'parsed_value' array to
 *    the target type and update the global 'change_field' vector with
 *    the numeric ID of that field.
 *
 *    Return 0 on success or -1 on failure.
 */
static int
parseChangeField(
    const char         *str_argument)
{
    sk_stringmap_entry_t *sm_entry;
    sk_stringmap_status_t sm_err;
    parsed_value_t *pv;
    char *argument;
    char *cp;
    char *eq;
    int rv = -1;

    assert(str_argument);
    argument = strdup(str_argument);
    if (NULL == argument) {
        goto END;
    }

    /* find the '=' */
    eq = strchr(argument, '=');
    if (NULL == eq) {
        skAppPrintErr("Invalid %s '%s': Unable to find '=' character",
                      appOptions[OPT_CHANGE_FIELD].name, argument);
        goto END;
    }

    /* ensure a value is given */
    cp = eq + 1;
    while (*cp && isspace((int)*cp)) {
        ++cp;
    }
    if ('\0' == *cp) {
        skAppPrintErr("Invalid %s '%s': No target type specified for field",
                      appOptions[OPT_CHANGE_FIELD].name, argument);
        goto END;
    }

    /* split into type1 and type2 */
    *eq = '\0';
    cp = eq + 1;

    /* find the field named by type1 */
    sm_err = skStringMapGetByName(field_map, argument, &sm_entry);
    if (sm_err) {
        skAppPrintErr("Invalid %s: Unable to find a field named '%s': %s",
                      appOptions[OPT_CHANGE_FIELD].name, argument,
                      skStringMapStrerror(sm_err));
        goto END;
    }

    assert(sm_entry->id < AGGBAGTOOL_ARRAY_SIZE);
    pv = &parsed_value[sm_entry->id];
    if (pv->pv_is_used) {
        skAppPrintErr("Invalid %s: A value for '%s' is already set",
                      appOptions[OPT_CHANGE_FIELD].name, sm_entry->name);
        goto END;
    }

    /* find the field named by type2 */
    sm_err = skStringMapGetByName(field_map, cp, &sm_entry);
    if (sm_err) {
        skAppPrintErr("Invalid %s: Unable to find a field named '%s': %s",
                      appOptions[OPT_CHANGE_FIELD].name, cp,
                      skStringMapStrerror(sm_err));
        goto END;
    }

    pv->pv_is_used = 1;

    if (NULL == change_field) {
        change_field = skVectorNew(sizeof(uint32_t));
        if (NULL == change_field) {
            skAppPrintOutOfMemory("vector");
            goto END;
        }
    }
    if (skVectorAppendValue(change_field, &sm_entry->id)) {
        skAppPrintOutOfMemory("vector element");
        goto END;
    }

    rv = 0;

  END:
    free(argument);
    return rv;
}
#endif  /* #if 0 */


/*
 *    Parse the list of field names in 'fields' and add them to the
 *    vector 'vec', creating the vector if it does not exist.  Use the
 *    value in 'opt_index' if an error is generated.  Return 0 on
 *    success or -1 on failure.
 */
static int
parseFieldList(
    sk_vector_t       **vec,
    int                 opt_index,
    const char         *fields)
{
    sk_stringmap_iter_t *iter = NULL;
    sk_stringmap_entry_t *entry;
    sk_vector_t *v;
    char *errmsg;
    int rv = -1;

    assert(vec);
    assert(fields);

    /* parse the list */
    if (skStringMapParse(field_map, fields, SKSTRINGMAP_DUPES_ERROR,
                         &iter, &errmsg))
    {
        skAppPrintErr("Invalid %s: %s",
                      appOptions[opt_index].name, errmsg);
        goto END;
    }

    /* create the vector if necessary */
    v = *vec;
    if (NULL == v) {
        v = skVectorNew(sizeof(uint32_t));
        if (NULL == v) {
            skAppPrintOutOfMemory("vector");
            goto END;
        }
        *vec = v;
    }

    /* add IDs to the vector */
    while (skStringMapIterNext(iter, &entry, NULL) == SK_ITERATOR_OK) {
        if (skVectorAppendValue(v, &entry->id)) {
            skAppPrintOutOfMemory("vector element");
            goto END;
        }
    }

    rv = 0;

  END:
    skStringMapIterDestroy(iter);
    return rv;
}


static int
abtoolCheckFields(
    void)
{
    uint32_t k_id;
    uint32_t c_id;
    unsigned int inserted;
    size_t bad_pos;
    uint32_t id;
    size_t j;

    /* check for incompatible options */
    if (((NULL != remove_fields) + (NULL != select_fields) + (NULL != to_bag)
         + (NULL != to_ipset)) > 1)
    {
        skAppPrintErr("May only specify one of --%s, --%s, --%s, and --%s",
                      appOptions[OPT_REMOVE_FIELDS].name,
                      appOptions[OPT_SELECT_FIELDS].name,
                      appOptions[OPT_TO_BAG].name,
                      appOptions[OPT_TO_IPSET].name);
        return -1;
    }

    inserted = 0;
    bad_pos = SIZE_MAX;

    if (to_bag) {
        if (parseFieldList(&select_fields, OPT_TO_BAG, to_bag)) {
            exit(EXIT_FAILURE);
        }
        if (skVectorGetCount(select_fields) != 2) {
            skAppPrintErr(
                "Invalid %s '%s': Exactly two fields must be specified",
                appOptions[OPT_TO_BAG].name, to_bag);
            exit(EXIT_FAILURE);
        }

        if (insert_field) {
            /* check for an insert_field that is not in select_fields;
             * if so, an error is printed and returned below */
            skVectorGetValue(&k_id, select_fields, 0);
            skVectorGetValue(&c_id, select_fields, 1);
            for (j = 0; skVectorGetValue(&id, insert_field, j) == 0; ++j) {
                if (id != k_id && id != c_id) {
                    if (0 == inserted) {
                        bad_pos = j;
                    }
                    ++inserted;
                }
            }
        }
    }
    if (to_ipset) {
        if (parseFieldList(&select_fields, OPT_TO_IPSET, to_ipset)) {
            exit(EXIT_FAILURE);
        }
        if (skVectorGetCount(select_fields) != 1) {
            skAppPrintErr(
                "Invalid %s '%s': Exactly one field must be specified",
                appOptions[OPT_TO_IPSET].name, to_ipset);
            exit(EXIT_FAILURE);
        }

        if (insert_field) {
            /* check for an insert_field that is not in select_fields;
             * if so, an error is printed and returned below */
            skVectorGetValue(&k_id, select_fields, 0);
            for (j = 0; skVectorGetValue(&id, insert_field, j) == 0; ++j) {
                if (0 == inserted) {
                    bad_pos = j;
                }
                ++inserted;
            }
        }
        parseInsertField("record=1");
    }

    if (inserted) {
        /* print and return error for insert_field IDs that are not in
         * either to_bag or to_ipset */
        assert(bad_pos < skVectorGetCount(insert_field));
        skVectorGetValue(&id, insert_field, bad_pos);
        if (1 == inserted) {
            skAppPrintErr("Field %s appears in --%s but not in --%s",
                          skStringMapGetFirstName(field_map, id),
                          appOptions[OPT_INSERT_FIELD].name,
                          appOptions[to_bag ? OPT_TO_BAG : OPT_TO_IPSET].name);
        } else {
            skAppPrintErr(("Multiple fields (%s,..) appear in"
                           " --%s but not in --%s"),
                          skStringMapGetFirstName(field_map, id),
                          appOptions[OPT_INSERT_FIELD].name,
                          appOptions[to_bag ? OPT_TO_BAG : OPT_TO_IPSET].name);
        }
        return -1;
    }

    if (insert_field && remove_fields) {
        /* FIXME: Remove from remove_fields any field that also
         * appears in insert_field.  This is subject to determining
         * whether a field appearing in both add-fields and either
         * select-field or remove-field signifies overwrite vs
         * add-if-not-present.  */
    }

    return 0;
}


/*
 *    Reorder the fields in the minmax_fields and setmask_fields
 *    vectors to be in the same order as the keys and values in the
 *    output aggbag, and remove any fields from the vectors that are
 *    not present in the aggbag.
 */
static void
reorderFilterFields(
    void)
{
    sk_aggbag_field_t f;
    uint32_t pos[AGGBAGTOOL_ARRAY_SIZE];
    size_t i;
    size_t j;

    if (NULL == minmax_fields && NULL == setmask_fields) {
        return;
    }

    /* note the location of each key/counter in the aggbag */
    memset(pos, 0, sizeof(pos));
    i = 0;
    for (j = 0; j < 2; ++j) {
        if (0 == j) {
            skAggBagInitializeKey(out_ab, NULL, &f);
        } else {
            skAggBagInitializeCounter(out_ab, NULL, &f);
        }
        do {
            ++i;
            pos[skAggBagFieldIterGetType(&f)] = i;
        } while (skAggBagFieldIterNext(&f) == SK_ITERATOR_OK);
    }

    if (minmax_fields) {
        /* remove fields not in the aggbag */
        minmax_value_t mmv, mmv2;

        j = 0;
        for (i = 0; i < skVectorGetCount(minmax_fields); ++i) {
            skVectorGetValue(&mmv, minmax_fields, i);
            if (pos[mmv.mmv_field]) {
                if (i != j) {
                    skVectorSetValue(minmax_fields, j, &mmv);
                }
                ++j;
            }
        }
        if (0 == j) {
            skVectorDestroy(minmax_fields);
            minmax_fields = NULL;
        } else {
            /* remove all elements >= j */
            skVectorSetCapacity(minmax_fields, j);

            /* use insertion sort to order the vector's elements */
            for (i = 1; i < skVectorGetCount(minmax_fields); ++i) {
                skVectorGetValue(&mmv, minmax_fields, i);
                for (j = i; j > 0; --j) {
                    skVectorGetValue(&mmv2, minmax_fields, j-1);
                    if (pos[mmv.mmv_field] >= pos[mmv2.mmv_field]) {
                        break;
                    }
                    skVectorSetValue(minmax_fields, j, &mmv2);
                }
                if (i != j) {
                    skVectorSetValue(minmax_fields, j, &mmv);
                }
            }
        }
    }

    if (setmask_fields) {
        /* remove fields not in the aggbag */
        setmask_value_t sv, sv2;

        j = 0;
        for (i = 0; i < skVectorGetCount(setmask_fields); ++i) {
            skVectorGetValue(&sv, setmask_fields, i);
            if (pos[sv.sv_field]) {
                if (i != j) {
                    skVectorGetValue(&sv2, setmask_fields, j);
                    skIPSetDestroy(&sv2.sv_ipset);
                    skVectorSetValue(setmask_fields, j, &sv);
                }
                ++j;
            }
        }
        if (0 == j) {
            skVectorDestroy(setmask_fields);
            setmask_fields = NULL;
        } else {
            skVectorSetCapacity(setmask_fields, j);

            for (i = 1; i < skVectorGetCount(setmask_fields); ++i) {
                skVectorGetValue(&sv, setmask_fields, i);
                for (j = i; j > 0; --j) {
                    skVectorGetValue(&sv2, setmask_fields, j-1);
                    if (pos[sv.sv_field] >= pos[sv2.sv_field]) {
                        break;
                    }
                    skVectorSetValue(setmask_fields, j, &sv2);
                }
                if (i != j) {
                    skVectorSetValue(setmask_fields, j, &sv);
                }
            }
        }
    }
}


/*
 *    Run through the aggbag and zero out any entries not within range or
 *    which aren't in the masking set.
 */
static void
applyFilters(
    void)
{
    sk_aggbag_iter_t iter = SK_AGGBAG_ITER_INITIALIZER;
    sk_aggbag_iter_t *it = &iter;
    size_t minmax_count;
    size_t setmask_count;
    size_t mmv_pos = 0;
    size_t sv_pos = 0;
    const minmax_value_t *mmv = NULL;
    setmask_value_t *sv = NULL;
    sk_aggbag_aggregate_t *kc_value;
    sk_aggbag_field_t *kc_field;
    sk_aggbag_type_t id;
    uint64_t number;
    skipaddr_t ip;
    int zero_row = 0;

    reorderFilterFields();

    minmax_count = (minmax_fields ? skVectorGetCount(minmax_fields) : 0);
    setmask_count = (setmask_fields ? skVectorGetCount(setmask_fields) : 0);
    if (0 == minmax_count && 0 == setmask_count) {
        return;
    }

    skAggBagIteratorBind(it, out_ab);
    while (skAggBagIteratorNext(it) == SK_ITERATOR_OK) {
        if (minmax_count) {
            mmv_pos = 0;
            mmv = ((minmax_value_t *)
                   skVectorGetValuePointer(minmax_fields, mmv_pos));
        }
        if (setmask_count) {
            sv_pos = 0;
            sv = ((setmask_value_t *)
                  skVectorGetValuePointer(setmask_fields, sv_pos));
        }

        kc_field = &it->key_field_iter;
        kc_value = &it->key;
        while (mmv || sv) {
            id = skAggBagFieldIterGetType(kc_field);
            if ((mmv && id == mmv->mmv_field)
                || (sv && id == sv->sv_field))
            {
                switch (id) {
                  case SKAGGBAG_FIELD_SIPv6:
                  case SKAGGBAG_FIELD_SIPv4:
                  case SKAGGBAG_FIELD_DIPv6:
                  case SKAGGBAG_FIELD_DIPv4:
                  case SKAGGBAG_FIELD_NHIPv6:
                  case SKAGGBAG_FIELD_NHIPv4:
                  case SKAGGBAG_FIELD_ANY_IPv6:
                  case SKAGGBAG_FIELD_ANY_IPv4:
                    skAggBagAggregateGetIPAddress(
                        kc_value, kc_field, &ip);
                    while (mmv && id == mmv->mmv_field) {
                        if (mmv->mmv_is_max
                            ? skipaddrCompare(&ip, &mmv->mmv_val.pv.pv_ip) > 0
                            : skipaddrCompare(&ip, &mmv->mmv_val.pv.pv_ip) < 0)
                        {
                            zero_row = 1;
                            sv = NULL;
                            break;
                        }
                        ++mmv_pos;
                        mmv = ((minmax_value_t *)
                               skVectorGetValuePointer(minmax_fields, mmv_pos));
                    }
                    while (sv && id == sv->sv_field) {
                        if (skIPSetCheckAddress(sv->sv_ipset, &ip)
                            == sv->sv_is_complement)
                        {
                            zero_row = 1;
                            break;
                        }
                        ++sv_pos;
                        sv = ((setmask_value_t *)
                              skVectorGetValuePointer(setmask_fields, sv_pos));
                    }
                    break;

                  default:
                    skAggBagAggregateGetUnsigned(
                        kc_value, kc_field, &number);
                    while (mmv && id == mmv->mmv_field) {
                        if (mmv->mmv_is_max
                            ? (number > mmv->mmv_val.pv.pv_int)
                            : (number < mmv->mmv_val.pv.pv_int))
                        {
                            zero_row = 1;
                            break;
                        }
                        ++mmv_pos;
                        mmv = ((minmax_value_t *)
                               skVectorGetValuePointer(minmax_fields, mmv_pos));
                    }
                    break;
                }
            }

            if (skAggBagFieldIterNext(kc_field) != SK_ITERATOR_OK) {
                if (kc_field == &it->key_field_iter) {
                    kc_field = &it->counter_field_iter;
                    kc_value = &it->counter;
                } else {
                    assert(kc_field == &it->counter_field_iter);
                    break;
                }
            }
        }

        if (zero_row) {
            zero_row = 0;
            skAggBagFieldIterReset(&it->counter_field_iter);
            do {
                skAggBagAggregateSetUnsigned(
                    &it->counter, &it->counter_field_iter, 0);
            } while (skAggBagFieldIterNext(&it->counter_field_iter)
                     == SK_ITERATOR_OK);
            skAggBagKeyCounterSet(out_ab, &it->key, &it->counter);
        }
    }
    skAggBagIteratorFree(it);
}


/*
 *    Create a (normal) Bag file from the global AggBag 'out_ab'.
 *    This function expects the AggBag to have two fields that
 *    correspond to the key and the counter of the Bag.  After
 *    creating the Bag, write it to the output streaam.
 */
static int
abtoolToBag(
    void)
{
    sk_aggbag_iter_t iter = SK_AGGBAG_ITER_INITIALIZER;
    sk_aggbag_iter_t *it = &iter;
    sk_aggbag_field_t f;
    skBag_t *bag = NULL;
    skBagFieldType_t k_type;
    skBagFieldType_t c_type;
    skBagTypedKey_t b_key;
    skBagTypedCounter_t b_counter;
    skBagErr_t rv_bag;
    ssize_t rv = -1;
    size_t k_len;
    size_t c_len;

    b_key.type = SKBAG_KEY_U32;
    b_counter.type = SKBAG_COUNTER_U64;

    /* determine the type of the key */
    skAggBagInitializeKey(out_ab, NULL, &f);
    switch (skAggBagFieldIterGetType(&f)) {
      case SKAGGBAG_FIELD_SIPv4:
        k_type = SKBAG_FIELD_SIPv4;
        b_key.type = SKBAG_KEY_IPADDR;
        break;
      case SKAGGBAG_FIELD_DIPv4:
        k_type = SKBAG_FIELD_DIPv4;
        b_key.type = SKBAG_KEY_IPADDR;
        break;
      case SKAGGBAG_FIELD_SPORT:
        k_type = SKBAG_FIELD_SPORT;
        break;
      case SKAGGBAG_FIELD_DPORT:
        k_type = SKBAG_FIELD_DPORT;
        break;
      case SKAGGBAG_FIELD_PROTO:
        k_type = SKBAG_FIELD_PROTO;
        break;
      case SKAGGBAG_FIELD_PACKETS:
        k_type = SKBAG_FIELD_PACKETS;
        break;
      case SKAGGBAG_FIELD_BYTES:
        k_type = SKBAG_FIELD_BYTES;
        break;
      case SKAGGBAG_FIELD_FLAGS:
        k_type = SKBAG_FIELD_FLAGS;
        break;
      case SKAGGBAG_FIELD_STARTTIME:
        k_type = SKBAG_FIELD_STARTTIME;
        break;
      case SKAGGBAG_FIELD_ELAPSED:
        k_type = SKBAG_FIELD_ELAPSED;
        break;
      case SKAGGBAG_FIELD_ENDTIME:
        k_type = SKBAG_FIELD_ENDTIME;
        break;
      case SKAGGBAG_FIELD_SID:
        k_type = SKBAG_FIELD_SID;
        break;
      case SKAGGBAG_FIELD_INPUT:
        k_type = SKBAG_FIELD_INPUT;
        break;
      case SKAGGBAG_FIELD_OUTPUT:
        k_type = SKBAG_FIELD_OUTPUT;
        break;
      case SKAGGBAG_FIELD_NHIPv4:
        k_type = SKBAG_FIELD_NHIPv4;
        b_key.type = SKBAG_KEY_IPADDR;
        break;
      case SKAGGBAG_FIELD_INIT_FLAGS:
        k_type = SKBAG_FIELD_INIT_FLAGS;
        break;
      case SKAGGBAG_FIELD_REST_FLAGS:
        k_type = SKBAG_FIELD_REST_FLAGS;
        break;
      case SKAGGBAG_FIELD_TCP_STATE:
        k_type = SKBAG_FIELD_TCP_STATE;
        break;
      case SKAGGBAG_FIELD_APPLICATION:
        k_type = SKBAG_FIELD_APPLICATION;
        break;
      case SKAGGBAG_FIELD_FTYPE_CLASS:
        k_type = SKBAG_FIELD_FTYPE_CLASS;
        break;
      case SKAGGBAG_FIELD_FTYPE_TYPE:
        k_type = SKBAG_FIELD_FTYPE_TYPE;
        break;
      case SKAGGBAG_FIELD_ICMP_TYPE:
        k_type = SKBAG_FIELD_CUSTOM;
        break;
      case SKAGGBAG_FIELD_ICMP_CODE:
        k_type = SKBAG_FIELD_CUSTOM;
        break;
      case SKAGGBAG_FIELD_SIPv6:
        k_type = SKBAG_FIELD_SIPv6;
        b_key.type = SKBAG_KEY_IPADDR;
        break;
      case SKAGGBAG_FIELD_DIPv6:
        k_type = SKBAG_FIELD_DIPv6;
        b_key.type = SKBAG_KEY_IPADDR;
        break;
      case SKAGGBAG_FIELD_NHIPv6:
        k_type = SKBAG_FIELD_NHIPv6;
        b_key.type = SKBAG_KEY_IPADDR;
        break;
      case SKAGGBAG_FIELD_ANY_IPv4:
        k_type = SKBAG_FIELD_ANY_IPv4;
        b_key.type = SKBAG_KEY_IPADDR;
        break;
      case SKAGGBAG_FIELD_ANY_IPv6:
        k_type = SKBAG_FIELD_ANY_IPv6;
        b_key.type = SKBAG_KEY_IPADDR;
        break;
      case SKAGGBAG_FIELD_ANY_PORT:
        k_type = SKBAG_FIELD_ANY_PORT;
        break;
      case SKAGGBAG_FIELD_ANY_SNMP:
        k_type = SKBAG_FIELD_ANY_SNMP;
        break;
      case SKAGGBAG_FIELD_ANY_TIME:
        k_type = SKBAG_FIELD_ANY_TIME;
        break;
      case SKAGGBAG_FIELD_CUSTOM_KEY:
        k_type = SKBAG_FIELD_CUSTOM;
        break;
      case SKAGGBAG_FIELD_SIP_COUNTRY:
        k_type = SKBAG_FIELD_SIP_COUNTRY;
        break;
      case SKAGGBAG_FIELD_DIP_COUNTRY:
        k_type = SKBAG_FIELD_DIP_COUNTRY;
        break;
      case SKAGGBAG_FIELD_ANY_COUNTRY:
        k_type = SKBAG_FIELD_ANY_COUNTRY;
        break;
      default:
        k_type = SKBAG_FIELD_CUSTOM;
        break;
    }
    k_len = (SKBAG_FIELD_CUSTOM == k_type) ? 4 : SKBAG_OCTETS_FIELD_DEFAULT;

    /* determine the type of the counter */
    skAggBagInitializeCounter(out_ab, NULL, &f);
    switch (skAggBagFieldIterGetType(&f)) {
      case SKAGGBAG_FIELD_RECORDS:
        c_type = SKBAG_FIELD_RECORDS;
        break;
      case SKAGGBAG_FIELD_SUM_PACKETS:
        c_type = SKBAG_FIELD_SUM_PACKETS;
        break;
      case SKAGGBAG_FIELD_SUM_BYTES:
        c_type = SKBAG_FIELD_SUM_BYTES;
        break;
      case SKAGGBAG_FIELD_SUM_ELAPSED:
        c_type = SKBAG_FIELD_SUM_ELAPSED;
        break;
      case SKAGGBAG_FIELD_CUSTOM_COUNTER:
        c_type = SKBAG_FIELD_CUSTOM;
        break;
      default:
        c_type = SKBAG_FIELD_CUSTOM;
        break;
    }
    c_len = (SKBAG_FIELD_CUSTOM == c_type) ? 8 : SKBAG_OCTETS_FIELD_DEFAULT;

    /* Create the bag */
    rv_bag = skBagCreateTyped(&bag, k_type, c_type, k_len, c_len);
    if (rv_bag) {
        skAppPrintErr("Error creating bag: %s", skBagStrerror(rv_bag));
        goto END;
    }

    /* Process the AggBag */
    skAggBagIteratorBind(it, out_ab);

    if (SKBAG_KEY_IPADDR == b_key.type) {
        while (skAggBagIteratorNext(it) == SK_ITERATOR_OK) {
            skAggBagAggregateGetIPAddress(
                &it->key, &it->key_field_iter, &b_key.val.addr);
            skAggBagAggregateGetUnsigned(
                &it->counter, &it->counter_field_iter, &b_counter.val.u64);
            skBagCounterAdd(bag, &b_key, &b_counter, NULL);
        }
    } else {
        uint64_t number;

        while (skAggBagIteratorNext(it) == SK_ITERATOR_OK) {
            skAggBagAggregateGetUnsigned(
                &it->key, &it->key_field_iter, &number);
            b_key.val.u32 = (number > UINT32_MAX ? UINT32_MAX : number);
            skAggBagAggregateGetUnsigned(
                &it->counter, &it->counter_field_iter, &b_counter.val.u64);
            skBagCounterAdd(bag, &b_key, &b_counter, NULL);
        }
    }

    /* Write the bag */
    rv_bag = skBagWrite(bag, out_stream);
    if (rv_bag) {
        if (SKBAG_ERR_OUTPUT == rv_bag) {
            char errbuf[2 * PATH_MAX];
            skStreamLastErrMessage(
                out_stream, skStreamGetLastReturnValue(out_stream),
                errbuf, sizeof(errbuf));
            skAppPrintErr("Error writing bag: %s", errbuf);
        } else {
            skAppPrintErr("Error writing bag to '%s': %s",
                          skStreamGetPathname(out_stream),
                          skBagStrerror(rv_bag));
        }
        goto END;
    }

    /* done */
    rv = 0;

  END:
    skAggBagIteratorFree(it);
    skBagDestroy(&bag);
    return rv;
}


/*
 *    Create an IPset file from the global AggBag 'out_ab'.  This
 *    function expects the AggBag to have two fields, where the first
 *    field is the IP address to write to the IPset.  After creating
 *    the IPset, write it to the output streaam.
 */
static int
abtoolToIPSet(
    void)
{
    sk_aggbag_iter_t iter = SK_AGGBAG_ITER_INITIALIZER;
    sk_aggbag_iter_t *it = &iter;
    sk_aggbag_field_t f;
    skipset_t *set = NULL;
    skipaddr_t ip;
    int is_ipaddr;
    uint64_t number;
    ssize_t rv;

    skAggBagInitializeKey(out_ab, NULL, &f);
    switch (skAggBagFieldIterGetType(&f)) {
      case SKAGGBAG_FIELD_SIPv4:
      case SKAGGBAG_FIELD_DIPv4:
      case SKAGGBAG_FIELD_NHIPv4:
      case SKAGGBAG_FIELD_ANY_IPv4:

      case SKAGGBAG_FIELD_SIPv6:
      case SKAGGBAG_FIELD_DIPv6:
      case SKAGGBAG_FIELD_NHIPv6:
      case SKAGGBAG_FIELD_ANY_IPv6:
        is_ipaddr = 1;
        break;
      default:
        is_ipaddr = 0;
        break;
    }

    /* Create the ipset */
    rv = skIPSetCreate(&set, 0);
    if (rv) {
        skAppPrintErr("Error creating IPset: %s", skIPSetStrerror(rv));
        goto END;
    }
    ipset_options.comp_method = comp_method;
    skIPSetOptionsBind(set, &ipset_options);

    /* Process the AggBag */
    skAggBagIteratorBind(it, out_ab);
    if (is_ipaddr) {
        while (skAggBagIteratorNext(it) == SK_ITERATOR_OK) {
            number = 0;
            do {
                skAggBagAggregateGetUnsigned(
                    &it->counter, &it->counter_field_iter, &number);
            } while (0 == number
                     && (skAggBagFieldIterNext(&it->counter_field_iter)
                         == SK_ITERATOR_OK));
            if (number) {
                skAggBagAggregateGetIPAddress(
                    &it->key, &it->key_field_iter, &ip);
                skIPSetInsertAddress(set, &ip, 0);
            }
        }
    } else {
        uint64_t u64;
        uint32_t u32;

        while (skAggBagIteratorNext(it) == SK_ITERATOR_OK) {
            number = 0;
            do {
                skAggBagAggregateGetUnsigned(
                    &it->counter, &it->counter_field_iter, &number);
            } while (0 == number
                     && (skAggBagFieldIterNext(&it->counter_field_iter)
                         == SK_ITERATOR_OK));
            if (number) {
                skAggBagAggregateGetUnsigned(
                    &it->key, &it->key_field_iter, &u64);
                if (u64 <= UINT32_MAX) {
                    u32 = u64;
                    skipaddrSetV4(&ip, &u32);
                    skIPSetInsertAddress(set, &ip, 0);
                }
            }
        }
    }

    /* Write the set */
    skIPSetClean(set);
    rv = skIPSetWrite(set, out_stream);
    if (rv) {
        if (SKIPSET_ERR_FILEIO == rv) {
            char errbuf[2 * PATH_MAX];
            skStreamLastErrMessage(
                out_stream, skStreamGetLastReturnValue(out_stream),
                errbuf, sizeof(errbuf));
            skAppPrintErr("Error writing IPset: %s", errbuf);
        } else {
            skAppPrintErr("Error writing IPset to '%s': %s",
                          skStreamGetPathname(out_stream),skIPSetStrerror(rv));
        }
        goto END;
    }

    /* done */
    rv = 0;

  END:
    skAggBagIteratorFree(it);
    skIPSetDestroy(&set);
    return rv;
}


static int
mapFields(
    sk_aggbag_t        *ab_dst,
    const sk_aggbag_t  *ab_src)
{
    sk_aggbag_iter_t iter = SK_AGGBAG_ITER_INITIALIZER;
    sk_aggbag_iter_t *it = &iter;
    sk_aggbag_field_t k_it;
    sk_aggbag_field_t c_it;
    sk_aggbag_aggregate_t key;
    sk_aggbag_aggregate_t counter;
    sk_aggbag_type_t id;
    parsed_value_t *pv;
    uint64_t number;
    skipaddr_t ip;
    int rv;

    skAggBagIteratorBind(it, ab_src);

    while (skAggBagIteratorNext(it) == SK_ITERATOR_OK) {
        skAggBagInitializeKey(ab_dst, &key, &k_it);
        do {
            id = skAggBagFieldIterGetType(&k_it);
            /* find the field in ab_src that matches k_it */
            while (skAggBagFieldIterGetType(&it->key_field_iter) < id) {
                skAggBagFieldIterNext(&it->key_field_iter);
            }
            pv = &parsed_value[id];
            if (pv->pv_is_fixed) {
                switch (id) {
                  case SKAGGBAG_FIELD_SIPv4:
                  case SKAGGBAG_FIELD_DIPv4:
                  case SKAGGBAG_FIELD_NHIPv4:
                  case SKAGGBAG_FIELD_ANY_IPv4:
                  case SKAGGBAG_FIELD_SIPv6:
                  case SKAGGBAG_FIELD_DIPv6:
                  case SKAGGBAG_FIELD_NHIPv6:
                  case SKAGGBAG_FIELD_ANY_IPv6:
                    skAggBagAggregateSetIPAddress(&key, &k_it, &pv->pv.pv_ip);
                    break;
                  default:
                    skAggBagAggregateSetUnsigned(&key, &k_it, pv->pv.pv_int);
                    break;
                }
            } else {
                assert(skAggBagFieldIterGetType(&it->key_field_iter) == id);
                switch (skAggBagFieldIterGetType(&k_it)) {
                  case SKAGGBAG_FIELD_SIPv6:
                  case SKAGGBAG_FIELD_SIPv4:
                  case SKAGGBAG_FIELD_DIPv6:
                  case SKAGGBAG_FIELD_DIPv4:
                  case SKAGGBAG_FIELD_NHIPv6:
                  case SKAGGBAG_FIELD_NHIPv4:
                  case SKAGGBAG_FIELD_ANY_IPv6:
                  case SKAGGBAG_FIELD_ANY_IPv4:
                    skAggBagAggregateGetIPAddress(
                        &it->key, &it->key_field_iter, &ip);
                    skAggBagAggregateSetIPAddress(&key, &k_it, &ip);
                    break;

                  default:
                    skAggBagAggregateGetUnsigned(
                        &it->key, &it->key_field_iter, &number);
                    skAggBagAggregateSetUnsigned(&key, &k_it, number);
                    break;
                }
            }
        } while (skAggBagFieldIterNext(&k_it) == SK_ITERATOR_OK);

        skAggBagInitializeCounter(ab_dst, &counter, &c_it);
        do {
            id = skAggBagFieldIterGetType(&c_it);
            /* find the field in ab_src that matches c_it */
            while (skAggBagFieldIterGetType(&it->counter_field_iter) < id) {
                skAggBagFieldIterNext(&it->counter_field_iter);
            }
            pv = &parsed_value[id];
            if (pv->pv_is_fixed) {
                /* if fields do not match, the field must be a new
                 * field */
                pv = &parsed_value[id];
                skAggBagAggregateSetUnsigned(&counter, &c_it, pv->pv.pv_int);
            } else {
                assert(skAggBagFieldIterGetType(&it->counter_field_iter) ==id);
                skAggBagAggregateGetUnsigned(
                    &it->counter, &it->counter_field_iter, &number);
                skAggBagAggregateSetUnsigned(&counter, &c_it, number);
            }
        } while (skAggBagFieldIterNext(&c_it) == SK_ITERATOR_OK);

        rv = skAggBagKeyCounterAdd(ab_dst, &key, &counter, NULL);
        if (rv) {
            skAppPrintErr("Unable to add to key: %s", skAggBagStrerror(rv));
            break;
        }
    }

    skAggBagIteratorFree(it);

    return 0;
}


static int
manipulateFields(
    sk_aggbag_t       **ab_param)
{
    sk_aggbag_type_iter_t iter;
    sk_aggbag_type_t field_type;
    parsed_value_t *pv;
    sk_vector_t *key_vec = NULL;
    sk_vector_t *counter_vec = NULL;
    sk_aggbag_type_t *id_array;
    unsigned int id_count;
    sk_bitmap_t *key_bitmap = NULL;
    sk_bitmap_t *counter_bitmap = NULL;
    sk_aggbag_field_t field;
    sk_aggbag_type_t t;
    sk_vector_t *field_vec = NULL;
    sk_vector_t *tmp_vec = NULL;
    size_t missing_fields;
    uint32_t id;
    uint32_t tmp_id;
    sk_aggbag_t *ab_dst = NULL;
    sk_aggbag_t *ab_src;
    int keep;
    size_t i, j;
    int rv = -1;

    assert(ab_param && *ab_param);

    if (NULL == insert_field && NULL == remove_fields && NULL == select_fields)
    {
        /* no changes */
        return 0;
    }

    ab_src = *ab_param;

    if (skAggBagCreate(&ab_dst)) {
        skAppPrintOutOfMemory("AggBag");
        goto END;
    }

#if 0
    /* ignore fields that are duplicates of constant fields */
    for (i = 0; 0 == skVectorGetValue(&id, field_vec, i); ++i) {
        if (AGGBAGBUILD_FIELD_IGNORED != id) {
            assert(id < AGGBAGBUILD_ARRAY_SIZE);
            pv = &parsed_value[id];
            if (pv->pv_is_const) {
                id = AGGBAGBUILD_FIELD_IGNORED;
                skVectorSetValue(field_vec, i, &id);
            } else {
                assert(0 == pv->pv_is_used);
                pv->pv_is_used = 1;
            }
        }
    }
#endif  /* 0 */

    /* we have a list of fields, but do not yet know which are
     * considered keys and which are counters.  the following code
     * determines that. */

    /* create bitmaps to hold key ids and counter ids */
    if (skBitmapCreate(&key_bitmap, AGGBAGTOOL_ARRAY_SIZE)) {
        skAppPrintOutOfMemory("bitmap");
        goto END;
    }
    if (skBitmapCreate(&counter_bitmap, AGGBAGTOOL_ARRAY_SIZE)) {
        skAppPrintOutOfMemory("bitmap");
        goto END;
    }
    skAggBagFieldTypeIteratorBind(&iter, SK_AGGBAG_KEY);
    while (skAggBagFieldTypeIteratorNext(&iter, &field_type)) {
        assert(AGGBAGTOOL_ARRAY_SIZE > (int)field_type);
        skBitmapSetBit(key_bitmap, field_type);
    }
    skAggBagFieldTypeIteratorBind(&iter, SK_AGGBAG_COUNTER);
    while (skAggBagFieldTypeIteratorNext(&iter, &field_type)) {
        assert(AGGBAGTOOL_ARRAY_SIZE > (int)field_type);
        skBitmapSetBit(counter_bitmap, field_type);
    }

    /* create vectors to hold the IDs that are being used */
    key_vec = skVectorNew(sizeof(sk_aggbag_type_t));
    counter_vec = skVectorNew(sizeof(sk_aggbag_type_t));
    if (!key_vec || !counter_vec) {
        skAppPrintOutOfMemory("vector");
        goto END;
    }

    if (NULL == select_fields && NULL == remove_fields) {
        /* select all fields that are in the source AggBag */
        for (i = 0; i < 2; ++i) {
            if (0 == i) {
                skAggBagInitializeKey(ab_src, NULL, &field);
                field_vec = key_vec;
            } else {
                skAggBagInitializeCounter(ab_src, NULL, &field);
                field_vec = counter_vec;
            }
            do {
                id = skAggBagFieldIterGetType(&field);
                skVectorAppendValue(field_vec, &id);
            } while (skAggBagFieldIterNext(&field) == SK_ITERATOR_OK);
        }
    } else {
        /*
         * Add to the destination AggBag the fields that are in the
         * source AggBag and appear in select_fields.  Fields in
         * select_fields that are not in the AggBag do not appear in
         * the destination AggBag.
         *
         * -- OR --
         *
         * Add to the destination AggBag the fields that are in the
         * source AggBag and do not appear in remove_fields.
         */
        const uint32_t keep_init = (remove_fields ? 1 : 0);

        if (select_fields) {
            assert(NULL == remove_fields);
            tmp_vec = skVectorClone(select_fields);
        } else {
            assert(NULL == select_fields);
            tmp_vec = skVectorClone(remove_fields);
        }
        if (NULL == tmp_vec) {
            skAppPrintOutOfMemory("vector");
            goto END;
        }

        for (i = 0; i < 2; ++i) {
            if (0 == i) {
                skAggBagInitializeKey(ab_src, NULL, &field);
                field_vec = key_vec;
            } else {
                skAggBagInitializeCounter(ab_src, NULL, &field);
                field_vec = counter_vec;
            }
            do {
                keep = keep_init;
                id = skAggBagFieldIterGetType(&field);
                for (j = 0; skVectorGetValue(&tmp_id, tmp_vec, j) == 0; ++j) {
                    if (id == tmp_id) {
                        keep = !keep;
                        skVectorRemoveValue(tmp_vec, j, NULL);
                        break;
                    }
                }
                if (keep) {
                    skVectorAppendValue(field_vec, &id);
                }
            } while (skAggBagFieldIterNext(&field) == SK_ITERATOR_OK);
        }
        skVectorDestroy(tmp_vec);
        tmp_vec = NULL;
    }

    if (insert_field) {
        /* first ensure 'pv_is_fixed' is set for all insert_fields in
         * the parsed_value[] array */
        for (i = 0; 0 == skVectorGetValue(&id, insert_field, i); ++i) {
            pv = &parsed_value[id];
            pv->pv_is_fixed = 1;
        }

        /* for any field in insert_field that is also in field_vec,
         * unset pv_is_fixed and remove from the insert_field copy */
        tmp_vec = skVectorClone(insert_field);
        if (NULL == tmp_vec) {
            skAppPrintOutOfMemory("vector");
            goto END;
        }
        for (i = 0; 0 == skVectorGetValue(&id, field_vec, i); ++i) {
            for (j = 0; skVectorGetValue(&tmp_id, tmp_vec, j) == 0; ++j) {
                if (id == tmp_id) {
                    skVectorRemoveValue(tmp_vec, j, NULL);
                    pv = &parsed_value[id];
                    pv->pv_is_fixed = 0;
                    break;
                }
            }
        }

        /* for any field that remains in tmp_vec, add it to the
         * destination AggBag */
        for (i = 0; 0 == skVectorGetValue(&id, tmp_vec, i); ++i) {
            if (skBitmapGetBit(key_bitmap, id) == 1) {
                t = (sk_aggbag_type_t)id;
                skVectorAppendValue(key_vec, &t);
            } else if (skBitmapGetBit(counter_bitmap, id) == 1) {
                t = (sk_aggbag_type_t)id;
                skVectorAppendValue(counter_vec, &t);
            } else {
                skAppPrintErr("Unknown field id %u", id);
                skAbort();
            }
        }
        skVectorDestroy(tmp_vec);
        tmp_vec = NULL;

        /* FIXME: Be certain to document how inserted-fields work when
         * the field is already present in the aggbag.  I think it
         * should act as an "overwrite" and select adds a 0 field when
         * the field is not present.  (Another option is to have add
         * be "add if not present" and user can get "overwrite" by
         * specifying field in both the insert-field and remove-fields
         * lists; or add is "overwrite" by default and becomes "add if
         * not present" when the field is given in insert-fields and
         * select-fields.)  Purpose is to get a consistent set of
         * fields across all input aggbags (though the remove-fields
         * does not ensure that).  */
    }

    /* ensure key and counter are defined */
    missing_fields = ((0 == skVectorGetCount(key_vec))
                      + 2 * (0 == skVectorGetCount(counter_vec)));
    if (missing_fields) {
        skAppPrintErr(
            "Do not have any %s fields; at least one %s field %s required",
            ((missing_fields == 3)
             ? "key fields or counter"
             : ((missing_fields == 1) ? "key" : "counter")),
            ((missing_fields == 3)
             ? "key field and one counter"
             : ((missing_fields == 1) ? "key" : "counter")),
            ((missing_fields == 3) ? "are" : "is"));
        goto END;
    }

    /* set key and counter */
    id_count = skVectorGetCount(key_vec);
    id_array = (sk_aggbag_type_t *)skVectorToArrayAlloc(key_vec);
    skAggBagSetKeyFields(ab_dst, id_count, id_array);
    free(id_array);

    id_count = skVectorGetCount(counter_vec);
    id_array = (sk_aggbag_type_t *)skVectorToArrayAlloc(counter_vec);
    skAggBagSetCounterFields(ab_dst, id_count, id_array);
    free(id_array);

    if (mapFields(ab_dst, ab_src)) {
        goto END;
    }

    /* Successful; replace the AggBag */
    skAggBagDestroy(&ab_src);
    *ab_param = ab_dst;
    rv = 0;

  END:
    if (0 != rv) {
        skAggBagDestroy(&ab_dst);
    }
    skVectorDestroy(key_vec);
    skVectorDestroy(counter_vec);
    skBitmapDestroy(&key_bitmap);
    skBitmapDestroy(&counter_bitmap);
    return rv;
}


/*
 *  ok = writeOutput();
 *
 *    Generate the output.
 */
static int
writeOutput(
    void)
{
    ssize_t rv;

    /* Remove anything that's not in range or not in the intersecting
     * set (or complement) as appropriate */
    applyFilters();

    /* add any notes (annotations) to the output */
    rv = skOptionsNotesAddToStream(out_stream);
    if (rv) {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        exit(EXIT_FAILURE);
    }

    /* add the invocation to the Bag */

    if (to_bag) {
        return abtoolToBag();
    }
    if (to_ipset) {
        return abtoolToIPSet();
    }

    rv = skAggBagWrite(out_ab, out_stream);
    if (SKAGGBAG_OK != rv) {
        if (SKAGGBAG_E_WRITE == rv) {
            skStreamPrintLastErr(out_stream,
                                 skStreamGetLastReturnValue(out_stream),
                                 &skAppPrintErr);
        } else {
            skAppPrintErr("Error writing Aggregate Bag to '%s': %s",
                          skStreamGetPathname(out_stream),
                          skAggBagStrerror(rv));
        }
        return -1;
    }

    return 0;
}


/*
 *  ok = appNextInput(argc, argv, &aggbag);
 *
 *    Read the next AggBag specified on the command line or the
 *    standard input if no files were given on the command line.  If
 *    field mapping is active, update the fields in the aggbag.
 *
 *    Return 1 if input is available, 0 if all input files have been
 *    processed, and -1 to indicate an error opening a file.
 */
static int
appNextInput(
    int                 argc,
    char              **argv,
    sk_aggbag_t       **ab_param)
{
    static int initialized = 0;
    const char *fname = NULL;
    sk_file_header_t *hdr = NULL;
    skstream_t *stream;
    sk_aggbag_t *ab;
    ssize_t rv;

    assert(argv);
    assert(ab_param);
    *ab_param = NULL;

    if (arg_index < argc) {
        /* get current file and prepare to get next */
        fname = argv[arg_index];
        ++arg_index;
    } else {
        if (initialized) {
            /* no more input */
            return 0;
        }
        /* input is from stdin */
        fname = "-";
    }

    initialized = 1;

    /* open the input stream */
    if ((rv = skStreamCreate(&stream, SK_IO_READ, SK_CONTENT_SILK))
        || (rv = skStreamBind(stream, fname))
        || (rv = skStreamOpen(stream))
        || (rv = skStreamReadSilkHeader(stream, &hdr)))
    {
        skStreamPrintLastErr(stream, rv, &skAppPrintErr);
        skStreamDestroy(&stream);
        return -1;
    }

    /* copy notes (annotations) from the input files to the output file */
    if (!note_strip) {
        rv = skHeaderCopyEntries(skStreamGetSilkHeader(out_stream), hdr,
                                 SK_HENTRY_ANNOTATION_ID);
        if (rv) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            skStreamDestroy(&stream);
            return -1;
        }
    }

    rv = skAggBagRead(&ab, stream);
    if (SKAGGBAG_OK != rv) {
        if (SKAGGBAG_E_READ == rv) {
            skStreamPrintLastErr(stream,
                                 skStreamGetLastReturnValue(stream),
                                 &skAppPrintErr);
        } else {
            skAppPrintErr("Error reading Aggregate Bag from '%s': %s",
                          skStreamGetPathname(stream),
                          skAggBagStrerror(rv));
        }
        skStreamDestroy(&stream);
        return -1;
    }
    skStreamDestroy(&stream);

    /* insert/remove/select columns in the aggbag as specified by the
     * switches */
    if (manipulateFields(&ab)) {
        skAggBagDestroy(&ab);
        return -1;
    }

    *ab_param = ab;

    return 1;
}


int main(int argc, char **argv)
{
    sk_aggbag_t *ab;
    ssize_t rv;

    appSetup(argc, argv);

    /* Read the first aggbag, which is the basis of the output */
    if (appNextInput(argc, argv, &out_ab) != 1) {
        return EXIT_FAILURE;
    }

    /* Open up each remaining aggbag and process it appropriately */
    while (1 == appNextInput(argc, argv, &ab)) {
        switch (action) {
          case AB_ACTION_UNSET:
            skAbortBadCase(action);

          case AB_ACTION_ADD:
            rv = skAggBagAddAggBag(out_ab, ab);
            if (SKAGGBAG_OK != rv) {
                skAppPrintErr("Error when adding aggbags: %s",
                              skAggBagStrerror(rv));
                skAggBagDestroy(&ab);
                return EXIT_FAILURE;
            }
            break;

          case AB_ACTION_SUBTRACT:
            rv = skAggBagSubtractAggBag(out_ab, ab);
            if (SKAGGBAG_OK != rv) {
                skAppPrintErr("Error when subtracting aggbags: %s",
                              skAggBagStrerror(rv));
                skAggBagDestroy(&ab);
                return EXIT_FAILURE;
            }
            break;
        }

        skAggBagDestroy(&ab);
    }

    /* Write the output */
    if (writeOutput()) {
        return EXIT_FAILURE;
    }

    /* done */
    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
