/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  ipfixreader.c
**
**    Helper file for rwflowpack.
**
**    Specify the functions that are used to read IPFIX or NetFlow V9
**    flow records from a TCP or UDP Berkeley socket.
**
**    This input_mode_type is used by the 'stream' input-mode.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: ipfixreader.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include "rwflowpack_priv.h"


/* MACROS and DATA TYPES */

#define INPUT_MODE_TYPE_NAME "IPFIX/NetFlowV9 Reader"

/*
 *  Specify the maximum size (in terms of RECORDS) of the buffer used
 *  to hold records that have been read from the flow-source but not
 *  yet processed.  This value is the number of records (RWRECs) per
 *  PROBE.  The maximum memory per probe will be BUF_REC_COUNT * 52
 *  (or BUF_REC_COUNT * 88 for IPv6-enabled SiLK).  If records are
 *  processed as quickly as they are read, the normal memory use per
 *  probe will be CIRCBUF_CHUNK_MAX_SIZE bytes.
 */
#define BUF_REC_COUNT 60000


/* FUNCTION DECLARATIONS */

static int
readerSetup(
    fp_daemon_mode_t   *is_daemon,
    const sk_vector_t  *probe_vec,
    reader_options_t   *options);
static int  readerStart(flow_proc_t *fproc);
static void readerStop(flow_proc_t *fproc);
static fp_get_record_result_t
readerGetRecord(
    rwRec                  *out_rec,
    const skpc_probe_t    **out_probe,
    flow_proc_t            *fproc);
static int  readerWantProbe(skpc_probe_t *probe);


/* FUNCTION DEFINITIONS */

/*
 *  status = readerGetRecord(&out_rwrec, &out_probe, flow_processor);
 *
 *    Invoked by input_mode_type->get_record_fn();
 */
static fp_get_record_result_t
readerGetRecord(
    rwRec                  *out_rwrec,
    const skpc_probe_t    **out_probe,
    flow_proc_t            *fproc)
{
    skIPFIXSource_t *ipfix_src = (skIPFIXSource_t*)fproc->flow_src;

    if (0 == skIPFIXSourceGetGeneric(ipfix_src, out_rwrec)) {
        *out_probe = fproc->probe;

        /* When reading from a socket, any point is a valid stopping
         * point */
        return FP_BREAK_POINT;
    }

    return FP_GET_ERROR;
}


/*
 *  status = readerStart(flow_processor)
 *
 *    Invoked by input_mode_type->start_fn();
 */
static int
readerStart(
    flow_proc_t        *fproc)
{
    const sk_sockaddr_array_t *bind_addr = NULL;
    const char *reader_type = "";
    skIPFIXSource_t *ipfix_src;
    skFlowSourceParams_t params;

    /* if a ipfix_src already exists, just return. */
    if (fproc->flow_src != NULL) {
        return 0;
    }

    switch (skpcProbeGetType(fproc->probe)) {
      case PROBE_ENUM_IPFIX:
        reader_type = "IPFIX";
        break;
      case PROBE_ENUM_NETFLOW_V9:
        reader_type = "NetFlowV9";
        break;
      case PROBE_ENUM_SFLOW:
        reader_type = "sFlow";
        break;
      default:
        skAbortBadCase(skpcProbeGetType(fproc->probe));
    }

    /* although we don't need the connection information to make the
     * connection, get it for logging */
    if (skpcProbeGetListenOnSockaddr(fproc->probe, &bind_addr)) {
        CRITMSG("Unable to get socket address for probe %s",
                skpcProbeGetName(fproc->probe));
        skAbort();
    }

    INFOMSG("Creating %s Reader for probe '%s' on %s",
            reader_type, skpcProbeGetName(fproc->probe),
            skSockaddrArrayGetHostPortPair(bind_addr));

    /* create the source */
    params.max_pkts = BUF_REC_COUNT;
    ipfix_src = skIPFIXSourceCreate(fproc->probe, &params);
    if (ipfix_src) {
        /* success.  return */
        fproc->flow_src = ipfix_src;
        return 0;
    }

    /* failed.  print error */
    ERRMSG("Could not create %s Reader for '%s' on %s",
           reader_type, skpcProbeGetName(fproc->probe),
           skSockaddrArrayGetHostPortPair(bind_addr));

    return -1;
}


/*
 *  readerStop(flow_processor);
 *
 *    Invoked by input_mode_type->stop_fn();
 */
static void
readerStop(
    flow_proc_t        *fproc)
{
    if (fproc->flow_src) {
        skIPFIXSourceStop((skIPFIXSource_t*)fproc->flow_src);
    }
}


/*
 *  readerFree(flow_processor);
 *
 *    Invoked by input_mode_type->free_fn();
 */
static void
readerFree(
    flow_proc_t        *fproc)
{
    if (fproc->flow_src) {
        skIPFIXSourceDestroy((skIPFIXSource_t*)fproc->flow_src);
        fproc->flow_src = NULL;
    }
}


/*
 *  readerPrintStats(flow_processor);
 *
 *    Invoked by input_mode_type->print_stats_fn();
 */
static void
readerPrintStats(
    flow_proc_t        *fproc)
{
    skIPFIXSource_t *ipfix_source = (skIPFIXSource_t *)fproc->flow_src;

    if (ipfix_source) {
        skIPFIXSourceLogStatsAndClear(ipfix_source);
    }
    if (fproc->rec_count_bad) {
        INFOMSG(("'%s': Records categorized %" PRIu64 ", dropped %" PRIu64),
                skpcProbeGetName(fproc->probe),
                (fproc->rec_count_total - fproc->rec_count_bad),
                fproc->rec_count_bad);
    }
    /* clear local counts */
    fproc->rec_count_total = 0;
    fproc->rec_count_bad = 0;
}


/*
 *  status = readerSetup(&out_daemon_mode, probe_vector, options);
 *
 *    Invoked by input_mode_type->setup_fn();
 */
static int
readerSetup(
    fp_daemon_mode_t           *is_daemon,
    const sk_vector_t          *probe_vec,
    reader_options_t    UNUSED(*options))
{
    /* this function should only be called if we actually have probes
     * to process */
    if (0 == skVectorGetCount(probe_vec)) {
        skAppPrintErr("readerSetup() called with zero length probe vector");
        return 1;
    }

    /* We are a daemon */
    *is_daemon = FP_DAEMON_ON;

    return 0;
}


/*
 *  yes_or_no = readerWantProbe(probe);
 *
 *    Invoked by input_mode_type->want_probe_fn();
 */
static int
readerWantProbe(
    skpc_probe_t       *probe)
{
    /* This is what we expect: a socket based IPFIX/NetFlowV9
     * listener */
    if ((-1 != skpcProbeGetListenOnSockaddr(probe, NULL))
        && ((PROBE_ENUM_IPFIX == skpcProbeGetType(probe))
            || (PROBE_ENUM_SFLOW == skpcProbeGetType(probe))
            || (PROBE_ENUM_NETFLOW_V9 == skpcProbeGetType(probe))))
    {
        return 1;
    }

    return 0;
}


/*
 *  status = ipfixReaderInitialize(input_mode_type);
 *
 *    Fill in the name and the function pointers for the input_mode_type.
 */
int
ipfixReaderInitialize(
    input_mode_type_t  *input_mode_type)
{
    /* Set my name */
    input_mode_type->reader_name = INPUT_MODE_TYPE_NAME;

    /* Set function pointers */
    input_mode_type->free_fn        = &readerFree;
    input_mode_type->get_record_fn  = &readerGetRecord;
    input_mode_type->print_stats_fn = &readerPrintStats;
    input_mode_type->setup_fn       = &readerSetup;
    input_mode_type->start_fn       = &readerStart;
    input_mode_type->stop_fn        = &readerStop;
    input_mode_type->want_probe_fn  = &readerWantProbe;

    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
