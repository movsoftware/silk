/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
** rwroutedio.c
**
** Suresh L Konda
**      routines to do io stuff with routed records.
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwroutedio.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/* #define RWPACK_BYTES_PACKETS          1 */
#define RWPACK_FLAGS_TIMES_VOLUMES    1
/* #define RWPACK_PROTO_FLAGS            1 */
#define RWPACK_SBB_PEF                1
#define RWPACK_TIME_BYTES_PKTS_FLAGS  1
/* #define RWPACK_TIMES_FLAGS_PROTO      1 */
#include "skstream_priv.h"
#include "rwpack.c"


/* Version to use when SK_RECORD_VERSION_ANY is specified */
#define DEFAULT_RECORD_VERSION 5


/* ********************************************************************* */

/*
**  RWROUTED VERSION 5
**
**    uint32_t      stime_bb1;       //  0- 3
**    // uint32_t     stime     :22  //        Start time:msec offset from hour
**    // uint32_t     bPPkt1    :10; //        Whole bytes-per-packet (hi 10)
**
**    uint32_t      bb2_elapsed;     //  4- 7
**    // uint32_t     bPPkt2    : 4; //        Whole bytes-per-packet (low 4)
**    // uint32_t     bPPFrac   : 6; //        Fractional bytes-per-packet
**    // uint32_t     elapsed   :22; //        Duration of flow in msec
**
**    uint32_t      pro_flg_pkts;    //  8-11
**    // uint32_t     prot_flags: 8; //        is_tcp==0: IP protocol
**                                   //        is_tcp==1: TCPflags/All pkts
**    // uint32_t     pflag     : 1; //        'pkts' requires multiplier?
**    // uint32_t     is_tcp    : 1; //        1 if flow is TCP; 0 otherwise
**    // uint32_t     padding   : 2; //
**    // uint32_t     pkts      :20; //        Count of packets
**
**    uint16_t      sPort;           // 12-13  Source port
**    uint16_t      dPort;           // 14-15  Destination port
**
**    uint16_t      input;           // 16-17  Router incoming SNMP interface
**    uint16_t      output;          // 18-19  Router outgoing SNMP interface
**
**    uint32_t      sIP;             // 20-23  Source IP
**    uint32_t      dIP;             // 24-27  Destination IP
**
**    uint32_t      nhIP;            // 28-31  Router Next Hop IP
**
**
**  32 bytes on disk.
*/

#define RECLEN_RWROUTED_V5 32


/*
 *    Byte swap the RWROUTED v5 record 'ar' in place.
 */
#define routedioRecordSwap_V5(ar)                       \
    {                                                   \
        SWAP_DATA32((ar) +  0);   /* stime_bb1 */       \
        SWAP_DATA32((ar) +  4);   /* bb2_elapsed */     \
        SWAP_DATA32((ar) +  8);   /* pro_flg_pkts */    \
        SWAP_DATA16((ar) + 12);   /* sPort */           \
        SWAP_DATA16((ar) + 14);   /* dPort */           \
        SWAP_DATA16((ar) + 16);   /* input */           \
        SWAP_DATA16((ar) + 18);   /* output */          \
        SWAP_DATA32((ar) + 20);   /* sIP */             \
        SWAP_DATA32((ar) + 24);   /* dIP */             \
        SWAP_DATA32((ar) + 28);   /* nhIP */            \
    }


/*
 *  Unpack the array of bytes 'ar' into a record 'rwrec'
 */
static int
routedioRecordUnpack_V5(
    skstream_t         *stream,
    rwGenericRec_V5    *rwrec,
    uint8_t            *ar)
{
    /* swap if required */
    if (stream->swapFlag) {
        routedioRecordSwap_V5(ar);
    }

    /* sTime, elapsed, pkts, bytes, proto, tcp-flags */
    rwpackUnpackFlagsTimesVolumes(rwrec, ar, stream->hdr_starttime, 12, 0);

    /* sPort, dPort */
    rwRecMemSetSPort(rwrec, &ar[12]);
    rwRecMemSetDPort(rwrec, &ar[14]);

    /* input, output */
    rwRecMemSetInput(rwrec, &ar[16]);
    rwRecMemSetOutput(rwrec, &ar[18]);

    /* sIP, dIP, nhIP */
    rwRecMemSetSIPv4(rwrec, &ar[20]);
    rwRecMemSetDIPv4(rwrec, &ar[24]);
    rwRecMemSetNhIPv4(rwrec, &ar[28]);

    /* sensor, flow_type from file name/header */
    rwRecSetSensor(rwrec, stream->hdr_sensor);
    rwRecSetFlowType(rwrec, stream->hdr_flowtype);

    return SKSTREAM_OK;
}


/*
 *  Pack the record 'rwrec' into an array of bytes 'ar'
 */
static int
routedioRecordPack_V5(
    skstream_t             *stream,
    const rwGenericRec_V5  *rwrec,
    uint8_t                *ar)
{
    int rv = SKSTREAM_OK; /* return value */

    /* sTime, elapsed, pkts, bytes, proto, tcp-flags */
    rv = rwpackPackFlagsTimesVolumes(ar, rwrec, stream->hdr_starttime, 12);
    if (rv) {
        return rv;
    }

    /* sPort, dPort */
    rwRecMemGetSPort(rwrec, &ar[12]);
    rwRecMemGetDPort(rwrec, &ar[14]);

    /* input, output */
    rwRecMemGetInput(rwrec, &ar[16]);
    rwRecMemGetOutput(rwrec, &ar[18]);

    /* sIP, dIP, nhIP */
    rwRecMemGetSIPv4(rwrec, &ar[20]);
    rwRecMemGetDIPv4(rwrec, &ar[24]);
    rwRecMemGetNhIPv4(rwrec, &ar[28]);

    /* swap if required */
    if (stream->swapFlag) {
        routedioRecordSwap_V5(ar);
    }

    return SKSTREAM_OK;
}


/* ********************************************************************* */

/*
**  RWROUTED VERSION 3
**  RWROUTED VERSION 4
**
**    uint32_t      sIP;             //  0- 3  Source IP
**    uint32_t      dIP;             //  4- 7  Destination IP
**
**    uint16_t      sPort;           //  8- 9  Source port
**    uint16_t      dPort;           // 10-11  Destination port
**
**    uint32_t      pkts_stime;      // 12-15
**    // uint32_t     pkts      :20; //        Count of packets
**    // uint32_t     sTime     :12; //        Start time--offset from hour
**
**    uint32_t      bbe;             // 16-19
**    // uint32_t     bPPkt     :14; //        Whole bytes-per-packet
**    // uint32_t     bPPFrac   : 6; //        Fractional bytes-per-packet
**    // uint32_t     elapsed   :12; //        Duration of flow
**
**    uint32_t      msec_flags       // 20-23
**    // uint32_t     sTime_msec:10; //        Fractional sTime (millisec)
**    // uint32_t     elaps_msec:10; //        Fractional elapsed (millisec)
**    // uint32_t     pflag     : 1; //        'pkts' requires multiplier?
**    // uint32_t     is_tcp    : 1; //        1 if flow is TCP; 0 otherwise
**    // uint32_t     padding   : 2; //        padding/reserved
**    // uint32_t     prot_flags: 8; //        is_tcp==0: IP protocol
**                                   //        is_tcp==1: TCP flags
**
**    uint32_t      nhIP;            // 24-27  Router Next Hop IP
**
**    uint16_t      input;           // 28-29  Router incoming SNMP interface
**    uint16_t      output;          // 30-31  Router outgoing SNMP interface
**
**
**  32 bytes on disk.
*/

#define RECLEN_RWROUTED_V3 32
#define RECLEN_RWROUTED_V4 32


/*
 *    Byte swap the RWROUTED v3 record 'ar' in place.
 */
#define routedioRecordSwap_V3(ar)                       \
    {                                                   \
        SWAP_DATA32((ar) +  0);   /* sIP */             \
        SWAP_DATA32((ar) +  4);   /* dIP */             \
        SWAP_DATA16((ar) +  8);   /* sPort */           \
        SWAP_DATA16((ar) + 10);   /* dPort */           \
        SWAP_DATA32((ar) + 12);   /* pkts_stime */      \
        SWAP_DATA32((ar) + 16);   /* bbe */             \
        SWAP_DATA32((ar) + 20);   /* msec_flags */      \
        SWAP_DATA32((ar) + 24);   /* nhIP */            \
        SWAP_DATA16((ar) + 28);   /* input */           \
        SWAP_DATA16((ar) + 30);   /* output */          \
    }


/*
 *  Unpack the array of bytes 'ar' into a record 'rwrec'
 */
static int
routedioRecordUnpack_V3(
    skstream_t         *stream,
    rwGenericRec_V5    *rwrec,
    uint8_t            *ar)
{
    /* swap if required */
    if (stream->swapFlag) {
        routedioRecordSwap_V3(ar);
    }

    /* sIP, dIP, sPort, dPort */
    rwRecMemSetSIPv4(rwrec, &ar[0]);
    rwRecMemSetDIPv4(rwrec, &ar[4]);
    rwRecMemSetSPort(rwrec, &ar[8]);
    rwRecMemSetDPort(rwrec, &ar[10]);

    /* sTime, pkts, bytes, elapsed, proto, tcp-flags, bpp */
    rwpackUnpackTimeBytesPktsFlags(rwrec, stream->hdr_starttime,
                                   (uint32_t*)&ar[12], (uint32_t*)&ar[16],
                                   (uint32_t*)&ar[20]);

    /* next hop, input & output interfaces */
    rwRecMemSetNhIPv4(rwrec, &ar[24]);
    rwRecMemSetInput(rwrec, &ar[28]);
    rwRecMemSetOutput(rwrec, &ar[30]);

    /* sensor, flow_type from file name/header */
    rwRecSetSensor(rwrec, stream->hdr_sensor);
    rwRecSetFlowType(rwrec, stream->hdr_flowtype);

    return SKSTREAM_OK;
}


/*
 *  Pack the record 'rwrec' into an array of bytes 'ar'
 */
static int
routedioRecordPack_V3(
    skstream_t             *stream,
    const rwGenericRec_V5  *rwrec,
    uint8_t                *ar)
{
    int rv = SKSTREAM_OK; /* return value */

    /* sTime, pkts, bytes, elapsed, proto, tcp-flags, bpp */
    rv = rwpackPackTimeBytesPktsFlags((uint32_t*)&ar[12], (uint32_t*)&ar[16],
                                      (uint32_t*)&ar[20],
                                      rwrec, stream->hdr_starttime);
    if (rv) {
        return rv;
    }

    /* sIP, dIP, sPort, dPort */
    rwRecMemGetSIPv4(rwrec, &ar[0]);
    rwRecMemGetDIPv4(rwrec, &ar[4]);
    rwRecMemGetSPort(rwrec, &ar[8]);
    rwRecMemGetDPort(rwrec, &ar[10]);

    /* next hop, input & output interfaces */
    rwRecMemGetNhIPv4(rwrec, &ar[24]);
    rwRecMemGetInput(rwrec, &ar[28]);
    rwRecMemGetOutput(rwrec, &ar[30]);

    /* swap if required */
    if (stream->swapFlag) {
        routedioRecordSwap_V3(ar);
    }

    return SKSTREAM_OK;
}


/* ********************************************************************* */

/*
**  RWROUTED VERSION 1
**  RWROUTED VERSION 2
**
**    uint32_t      sIP;             //  0- 3  Source IP
**    uint32_t      dIP;             //  4- 7  Destination IP
**
**    uint32_t      nhIP;            //  8-11  Router Next Hop IP
**
**    uint16_t      sPort;           // 12-23  Source port
**    uint16_t      dPort;           // 14-15  Destination port
**
**    uint32_t      pef;             // 16-19
**    // uint32_t     pkts      :20; //        Count of packets
**    // uint32_t     elapsed   :11; //        Duration of flow
**    // uint32_t     pflag     : 1; //        'pkts' requires multiplier?
**
**    uint32_t      sbb;             // 20-23
**    // uint32_t     sTime     :12; //        Start time--offset from hour
**    // uint32_t     bPPkt     :14; //        Whole bytes-per-packet
**    // uint32_t     bPPFrac   : 6; //        Fractional bytes-per-packet
**
**    uint8_t       proto;           // 24     IP protocol
**    uint8_t       flags;           // 25     OR of all TCP flags on all pkts
**    uint8_t       input;           // 26     Router incoming SNMP interface
**    uint8_t       output;          // 27     Router outgoing SNMP interface
**
**
**  28 bytes on disk.
*/

#define RECLEN_RWROUTED_V1 28
#define RECLEN_RWROUTED_V2 28


/*
 *    Byte swap the RWROUTED v1 record 'ar' in place.
 */
#define routedioRecordSwap_V1(ar)                                       \
    {                                                                   \
        SWAP_DATA32((ar) +  0);   /* sIP */                             \
        SWAP_DATA32((ar) +  4);   /* dIP */                             \
        SWAP_DATA32((ar) +  8);   /* nhIP */                            \
        SWAP_DATA16((ar) + 12);   /* sPort */                           \
        SWAP_DATA16((ar) + 14);   /* dPort */                           \
        SWAP_DATA32((ar) + 16);   /* pef */                             \
        SWAP_DATA32((ar) + 20);   /* sbb */                             \
        /* Four single bytes: (24)proto, (25)flags, (26)input, (27)output */ \
    }


/*
 *  Unpack the array of bytes 'ar' into a record 'rwrec'
 */
static int
routedioRecordUnpack_V1(
    skstream_t         *stream,
    rwGenericRec_V5    *rwrec,
    uint8_t            *ar)
{
    /* swap if required */
    if (stream->swapFlag) {
        routedioRecordSwap_V1(ar);
    }

    /* sIP, dIP, nhIP, sPort, dPort */
    rwRecMemSetSIPv4(rwrec, &ar[0]);
    rwRecMemSetDIPv4(rwrec, &ar[4]);
    rwRecMemSetNhIPv4(rwrec, &ar[8]);
    rwRecMemSetSPort(rwrec, &ar[12]);
    rwRecMemSetDPort(rwrec, &ar[14]);

    /* pkts, elapsed, sTime, bytes, bpp */
    rwpackUnpackSbbPef(rwrec, stream->hdr_starttime,
                       (uint32_t*)&ar[20], (uint32_t*)&ar[16]);

    /* proto, flags, input&output interfaces */
    rwRecSetProto(rwrec,  ar[24]);
    rwRecSetFlags(rwrec,  ar[25]);
    rwRecSetInput(rwrec,  ar[26]);
    rwRecSetOutput(rwrec, ar[27]);

    /* sensor, flow_type from file name/header */
    rwRecSetSensor(rwrec, stream->hdr_sensor);
    rwRecSetFlowType(rwrec, stream->hdr_flowtype);

    return SKSTREAM_OK;
}


/*
 *  Pack the record 'rwrec' into an array of bytes 'ar'
 */
static int
routedioRecordPack_V1(
    skstream_t             *stream,
    const rwGenericRec_V5  *rwrec,
    uint8_t                *ar)
{
    int rv = SKSTREAM_OK; /* return value */

    /* Check sizes of fields we've expanded in later versions */
    if (rwRecGetInput(rwrec) > 255 || rwRecGetOutput(rwrec) > 255) {
        return SKSTREAM_ERR_SNMP_OVRFLO;
    }

    /* pkts, elapsed, sTime, bytes, bpp */
    rv = rwpackPackSbbPef((uint32_t*)&ar[20], (uint32_t*)&ar[16],
                          rwrec, stream->hdr_starttime);
    if (rv) {
        return rv;
    }

    /* sIP, dIP, nhIP, sPort, dPort */
    rwRecMemGetSIPv4(rwrec, &ar[0]);
    rwRecMemGetDIPv4(rwrec, &ar[4]);
    rwRecMemGetNhIPv4(rwrec, &ar[8]);
    rwRecMemGetSPort(rwrec, &ar[12]);
    rwRecMemGetDPort(rwrec, &ar[14]);

    /* proto, flags, input interface, output interface */
    ar[24] = rwRecGetProto(rwrec);
    ar[25] = rwRecGetFlags(rwrec);
    ar[26] = (uint8_t)rwRecGetInput(rwrec);
    ar[27] = (uint8_t)rwRecGetOutput(rwrec);

    /* swap if required */
    if (stream->swapFlag) {
        routedioRecordSwap_V1(ar);
    }

    return SKSTREAM_OK;
}


/* ********************************************************************* */

/*
 *  Return length of record of specified version, or 0 if no such
 *  version exists.  See skstream_priv.h for details.
 */
uint16_t
routedioGetRecLen(
    sk_file_version_t   vers)
{
    switch (vers) {
      case 1:
        return RECLEN_RWROUTED_V1;
      case 2:
        return RECLEN_RWROUTED_V2;
      case 3:
        return RECLEN_RWROUTED_V3;
      case 4:
        return RECLEN_RWROUTED_V4;
      case 5:
        return RECLEN_RWROUTED_V5;
      default:
        return 0;
    }
}


/*
 *  status = routedioPrepare(&stream);
 *
 *    Sets the record version to the default if it is unspecified,
 *    checks that the record format supports the requested record
 *    version, sets the record length, and sets the pack and unpack
 *    functions for this record format and version.
 */
int
routedioPrepare(
    skstream_t         *stream)
{
#define FILE_FORMAT "FT_RWROUTED"
    sk_file_header_t *hdr = stream->silk_hdr;
    int rv = SKSTREAM_OK; /* return value */

    assert(skHeaderGetFileFormat(hdr) == FT_RWROUTED);

    /* Set version if none was selected by caller */
    if ((stream->io_mode == SK_IO_WRITE)
        && (skHeaderGetRecordVersion(hdr) == SK_RECORD_VERSION_ANY))
    {
        skHeaderSetRecordVersion(hdr, DEFAULT_RECORD_VERSION);
    }

    /* version check; set values based on version */
    switch (skHeaderGetRecordVersion(hdr)) {
      case 5:
        stream->rwUnpackFn = &routedioRecordUnpack_V5;
        stream->rwPackFn   = &routedioRecordPack_V5;
        break;
      case 4:
      case 3:
        /* V3 and V4 differ only in that V4 supports compression on
         * read and write; V3 supports compression only on read */
        stream->rwUnpackFn = &routedioRecordUnpack_V3;
        stream->rwPackFn   = &routedioRecordPack_V3;
        break;
      case 2:
      case 1:
        /* V1 and V2 differ only in the padding of the header */
        stream->rwUnpackFn = &routedioRecordUnpack_V1;
        stream->rwPackFn   = &routedioRecordPack_V1;
        break;
      case 0:
      default:
        rv = SKSTREAM_ERR_UNSUPPORT_VERSION;
        goto END;
    }

    stream->recLen = routedioGetRecLen(skHeaderGetRecordVersion(hdr));

    /* verify lengths */
    if (stream->recLen == 0) {
        skAppPrintErr("Record length not set for %s version %u",
                      FILE_FORMAT, (unsigned)skHeaderGetRecordVersion(hdr));
        skAbort();
    }
    if (stream->recLen != skHeaderGetRecordLength(hdr)) {
        if (0 == skHeaderGetRecordLength(hdr)) {
            skHeaderSetRecordLength(hdr, stream->recLen);
        } else {
            skAppPrintErr(("Record length mismatch for %s version %u\n"
                           "\tcode = %" PRIu16 " bytes;  header = %lu bytes"),
                          FILE_FORMAT, (unsigned)skHeaderGetRecordVersion(hdr),
                          stream->recLen,
                          (unsigned long)skHeaderGetRecordLength(hdr));
            skAbort();
        }
    }

  END:
    return rv;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
