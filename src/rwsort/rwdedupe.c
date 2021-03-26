/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwdedupe reads SiLK Flow Records from the standard input or from
**  named files, re-orders them, removes duplicate records, and writes
**  the result.
**
**  rwdedupe attempts to sort the records in RAM using a buffer whose
**  maximum size is DEFAULT_BUFFER_SIZE bytes.  The user may choose a
**  different maximum size with the --buffer-size switch.  The buffer
**  rwdedupe initially allocates is 1/NUM_CHUNKS of this size; when it
**  is full, the buffer is reallocated and grown by another
**  1/NUM_CHUNKS.  This continues until all records are read, a
**  realloc() fails, or the maximum buffer size is reached.
**
**  Records are read and stored in this buffer; if the input ends
**  before the buffer is filled, the records are sorted and printed to
**  standard out or to the named output file.
**
**  However, if the buffer fills before the input is completely read,
**  the records in the buffer are sorted and written to a temporary
**  file on disk; the buffer is cleared, and reading of the input
**  resumes, repeating the process as necessary until all records are
**  read.  We then do an N-way merge-sort on the temporary files,
**  where N is either all the temporary files, MAX_MERGE_FILES, or the
**  maximum number that we can open before running out of file
**  descriptors (EMFILE) or memory.  If we cannot open all temporary
**  files, we merge the N files into a new temporary file, then add it
**  to the list of files to merge.
**
**  When the temporary files are written to the same volume (file
**  system) as the final output, the maximum disk usage will be
**  2-times the number of records read (times the size per record);
**  when different volumes are used, the disk space required for the
**  temporary files will be between 1 and 1.5 times the number of
**  records.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwdedupe.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include "rwdedupe.h"
#include <silk/skheap.h>


/* TYPEDEFS AND DEFINES */



/* EXPORTED VARIABLES */

/* number of fields to sort over */
uint32_t num_fields = 0;

/* IDs of the fields to sort over; values are from the
 * rwrec_printable_fields_t enum. */
uint32_t sort_fields[RWREC_PRINTABLE_FIELD_COUNT];

/* output stream */
skstream_t *out_stream = NULL;

/* temp file context */
sk_tempfilectx_t *tmpctx;

/* maximum amount of RAM to attempt to allocate */
size_t buffer_size;

/* differences to allow between flows */
flow_delta_t delta;


/* FUNCTION DEFINITIONS */

/*
 *    Individually invoke 'func' on the rwRec pointers 'rec_a' and
 *    'rec_b' and compare the results for sorting.
 */
#define RETURN_IF_SORTED(func, rec_a, rec_b)    \
    if (func((const rwRec*)(rec_a))             \
        < func((const rwRec*)(rec_b)))          \
    {                                           \
        return -1;                              \
    } else if (func((const rwRec*)(rec_a))      \
               > func((const rwRec*)(rec_b)))   \
    {                                           \
        return 1;                               \
    }

/*
 *    Individually invoke 'func' on the rwRec pointers 'rec_a' and
 *    'rec_b' and compare the results for sorting.  Treat as
 *    equivalent if the difference is less than 'delta'.
 */
#define RETURN_IF_SORTED_DELTA(func, rec_a, rec_b, delta)       \
    if ((delta) == 0) {                                         \
        RETURN_IF_SORTED(func, rec_a, rec_b);                   \
    } else if (func((const rwRec*)(rec_a))                      \
               < func((const rwRec*)(rec_b)))                   \
    {                                                           \
        if ((delta) < (func((const rwRec*)(rec_b))              \
                       - func((const rwRec*)(rec_a))))          \
        {                                                       \
            return -1;                                          \
        }                                                       \
    } else if ((delta) < (func((const rwRec*)(rec_a))           \
                          - func((const rwRec*)(rec_b))))       \
    {                                                           \
        return 1;                                               \
    }

#if !SK_ENABLE_IPV6
#define compareIPs(ipa, ipb)                            \
    ((skipaddrGetV4(ipa) < skipaddrGetV4(ipb))          \
     ? -1                                               \
     : (skipaddrGetV4(ipa) > skipaddrGetV4(ipb)))
#endif

#define RETURN_IF_SORTED_IPS(func, rec_a, rec_b)        \
    {                                                   \
        skipaddr_t ip_a, ip_b;                          \
        int cmp;                                        \
        func((rwRec*)(rec_a), &ip_a);                   \
        func((rwRec*)(rec_b), &ip_b);                   \
        cmp = compareIPs(&ip_a, &ip_b);                 \
        if (cmp != 0) {                                 \
            return cmp;                                 \
        }                                               \
    }


#if SK_ENABLE_IPV6
static int
compareIPs(
    const skipaddr_t   *ipa,
    const skipaddr_t   *ipb)
{
    uint8_t ipa_v6[16];
    uint8_t ipb_v6[16];

    if (skipaddrIsV6(ipa)) {
        if (!skipaddrIsV6(ipb)) {
            /* treat the IPv4 'ipb' as less than IPv6 */
            return 1;
        }
        /* both are IPv6 */
        skipaddrGetV6(ipa, ipa_v6);
        skipaddrGetV6(ipb, ipb_v6);
        return memcmp(ipa_v6, ipb_v6, sizeof(ipa_v6));
    }
    if (skipaddrIsV6(ipb)) {
        return -1;
    }
    /* both are IPv4 */
    return ((skipaddrGetV4(ipa) < skipaddrGetV4(ipb))
            ? -1
            : (skipaddrGetV4(ipa) > skipaddrGetV4(ipb)));
}
#endif  /* SK_ENABLE_IPV6 */


/*
 *  rwrecCompare(a, b);
 *
 *     Returns an ordering on the recs pointed to `a' and `b' by
 *     comparing the fields listed in the sort_fields[] array.
 */
static int
rwrecCompare(
    const void         *a,
    const void         *b)
{
    uint32_t i;

    if (0 == num_fields) {
        return memcmp(a, b, NODE_SIZE);
    }

    for (i = 0; i < num_fields; ++i) {
        switch (sort_fields[i]) {
          case RWREC_FIELD_SIP:
            RETURN_IF_SORTED_IPS(rwRecMemGetSIP, a, b);
            break;

          case RWREC_FIELD_DIP:
            RETURN_IF_SORTED_IPS(rwRecMemGetDIP, a, b);
            break;

          case RWREC_FIELD_NHIP:
            RETURN_IF_SORTED_IPS(rwRecMemGetNhIP, a, b);
            break;

          case RWREC_FIELD_SPORT:
            RETURN_IF_SORTED(rwRecGetSPort, a, b);
            break;

          case RWREC_FIELD_DPORT:
            RETURN_IF_SORTED(rwRecGetDPort, a, b);
            break;

          case RWREC_FIELD_PROTO:
            RETURN_IF_SORTED(rwRecGetProto, a, b);
            break;

          case RWREC_FIELD_PKTS:
            RETURN_IF_SORTED_DELTA(rwRecGetPkts, a, b, delta.d_packets);
            break;

          case RWREC_FIELD_BYTES:
            RETURN_IF_SORTED_DELTA(rwRecGetBytes, a, b, delta.d_bytes);
            break;

          case RWREC_FIELD_FLAGS:
            RETURN_IF_SORTED(rwRecGetFlags, a, b);
            break;

          case RWREC_FIELD_STIME:
          case RWREC_FIELD_STIME_MSEC:
            RETURN_IF_SORTED_DELTA(rwRecGetStartTime, a, b, delta.d_stime);
            break;

          case RWREC_FIELD_ELAPSED:
          case RWREC_FIELD_ELAPSED_MSEC:
            RETURN_IF_SORTED_DELTA(rwRecGetElapsed, a, b, delta.d_elapsed);
            break;

          case RWREC_FIELD_SID:
            RETURN_IF_SORTED(rwRecGetSensor, a, b);
            break;

          case RWREC_FIELD_INPUT:
            RETURN_IF_SORTED(rwRecGetInput, a, b);
            break;

          case RWREC_FIELD_OUTPUT:
            RETURN_IF_SORTED(rwRecGetOutput, a, b);
            break;

          case RWREC_FIELD_INIT_FLAGS:
            RETURN_IF_SORTED(rwRecGetInitFlags, a, b);
            break;

          case RWREC_FIELD_REST_FLAGS:
            RETURN_IF_SORTED(rwRecGetRestFlags, a, b);
            break;

          case RWREC_FIELD_TCP_STATE:
            RETURN_IF_SORTED(rwRecGetTcpState, a, b);
            break;

          case RWREC_FIELD_APPLICATION:
            RETURN_IF_SORTED(rwRecGetApplication, a, b);
            break;

          case RWREC_FIELD_FTYPE_CLASS:
          case RWREC_FIELD_FTYPE_TYPE:
            RETURN_IF_SORTED(rwRecGetFlowType, a, b);
            break;
        }
    }

    return 0;
}


/*
 *  status = compHeapNodes(b, a, v_recs);
 *
 *    Callback function used by the heap to compare two heapnodes,
 *    which are just indexes into an array of records.  'v_recs' is
 *    the array of records, where each record is NODE_SIZE bytes.
 *
 *    Note the order of arguments is 'b', 'a'.
 */
static int
compHeapNodes(
    const skheapnode_t  b,
    const skheapnode_t  a,
    void               *v_recs)
{
    uint8_t *recs = (uint8_t*)v_recs;

    return rwrecCompare(&recs[*(uint16_t*)a * NODE_SIZE],
                        &recs[*(uint16_t*)b * NODE_SIZE]);
}


/*
 *  mergeFiles(temp_file_idx)
 *
 *    Merge the temporary files numbered from 0 to 'temp_file_idx'
 *    inclusive into the output file 'out_stream', maintaining sorted
 *    order.  Exits the application if an error occurs.
 */
static void
mergeFiles(
    int                 temp_file_idx)
{
    char errbuf[2 * PATH_MAX];
    skstream_t *fps[MAX_MERGE_FILES];
    uint8_t recs[MAX_MERGE_FILES][NODE_SIZE];
    uint8_t lowest_rec[NODE_SIZE];
    int j;
    uint16_t open_count;
    uint16_t i;
    uint16_t lowest;
    uint16_t *top_heap;
    int tmp_idx_a;
    int tmp_idx_b;
    skstream_t *fp_intermediate = NULL;
    int tmp_idx_intermediate;
    skheap_t *heap;
    uint32_t heap_count;
    int opened_all_temps = 0;
    ssize_t rv;

    assert(temp_file_idx > 0);

    /* the index of the first temp file to the merge */
    tmp_idx_a = 0;

    TRACEMSG(("Merging #%d through #%d into '%s'",
              tmp_idx_a, temp_file_idx, skStreamGetPathname(out_stream)));

    heap = skHeapCreate2(compHeapNodes, MAX_MERGE_FILES, sizeof(uint16_t),
                         NULL, recs);
    if (NULL == heap) {
        skAppPrintOutOfMemory("heap");
        appExit(EXIT_FAILURE);
    }

    /* This loop repeats as long as we haven't read all of the temp
     * files generated in the sorting stage. */
    do {
        assert(SKHEAP_ERR_EMPTY==skHeapPeekTop(heap,(skheapnode_t*)&top_heap));

        /* the index of the last temp file to merge */
        tmp_idx_b = temp_file_idx;

        /* open an intermediate temp file.  The merge-sort will have
         * to write records here if there are not enough file handles
         * available to open all the existing tempoary files. */
        fp_intermediate = skTempFileCreateStream(tmpctx,&tmp_idx_intermediate);
        if (fp_intermediate == NULL) {
            skAppPrintSyserror("Error creating new temporary file");
            appExit(EXIT_FAILURE);
        }

        /* count number of files we open */
        open_count = 0;

        /* Attempt to open up to MAX_MERGE_FILES, though we an open
         * may fail due to lack of resources (EMFILE or ENOMEM) */
        for (j = tmp_idx_a; j <= tmp_idx_b; ++j) {
            fps[open_count] = skTempFileOpenStream(tmpctx, j);
            if (fps[open_count] == NULL) {
                if ((open_count > 0)
                    && ((errno == EMFILE) || (errno == ENOMEM)))
                {
                    /* We cannot open any more files.  Rewind counter
                     * by one to catch this file on the next merge */
                    tmp_idx_b = j - 1;
                    TRACEMSG((("FILE limit hit--"
                               "merging #%d through #%d into #%d: %s"),
                              tmp_idx_a, tmp_idx_b, tmp_idx_intermediate,
                              strerror(errno)));
                    break;
                } else {
                    skAppPrintSyserror(("Error opening existing"
                                        " temporary file '%s'"),
                                       skTempFileGetName(tmpctx, j));
                    appExit(EXIT_FAILURE);
                }
            }

            /* read the first record */
            rv = skStreamRead(fps[open_count], recs[open_count], NODE_SIZE);
            if (NODE_SIZE == rv) {
                /* insert the file index into the heap */
                skHeapInsert(heap, &open_count);
                ++open_count;
                if (open_count == MAX_MERGE_FILES) {
                    /* We've reached the limit for this pass.  Set
                     * tmp_idx_b to the file we just opened. */
                    tmp_idx_b = j;
                    TRACEMSG((("MAX_MERGE_FILES limit hit--"
                               "merging #%d through #%d into #%d"),
                              tmp_idx_a, tmp_idx_b, tmp_idx_intermediate));
                    break;
                }
            } else if (0 == rv) {
                TRACEMSG(("Ignoring empty temporary file '%s'",
                          skTempFileGetName(tmpctx, j)));
                skStreamDestroy(&fps[open_count]);
            } else {
                if (rv > 0) {
                    snprintf(errbuf, sizeof(errbuf),
                             "Short read %" SK_PRIdZ "/%" SK_PRIuZ " from '%s'",
                             rv, NODE_SIZE,
                             skStreamGetPathname(fps[open_count]));
                } else {
                    skStreamLastErrMessage(
                        fps[open_count], rv, errbuf, sizeof(errbuf));
                }
                skAppPrintErr(
                    "Error reading first record from temporary file: %s",
                    errbuf);
                appExit(EXIT_FAILURE);
            }
        }

        /* Here, we check to see if we've opened all temp files.  If
         * so, set a flag so we write data to final destination and
         * break out of the loop after we're done. */
        if (tmp_idx_b == temp_file_idx) {
            opened_all_temps = 1;
            /* no longer need the intermediate temp file */
            skStreamDestroy(&fp_intermediate);
        } else {
            /* we could not open all temp files, so merge all opened
             * temp files into the intermediate file.  Add the
             * intermediate file to the list of files to merge */
            temp_file_idx = tmp_idx_intermediate;
        }

        TRACEMSG((("Merging %" PRIu16 " temporary files"), open_count));

        heap_count = skHeapGetNumberEntries(heap);
        assert(heap_count == open_count);

        /* get the index of the file with the lowest record; which is
         * at the top of the heap */
        if (skHeapPeekTop(heap, (skheapnode_t*)&top_heap) != SKHEAP_OK) {
            skAppPrintErr("Unable to open and read any temporary files.");
            appExit(EXIT_FAILURE);
        }
        lowest = *top_heap;

        /* exit this do...while() once all records for all opened
         * files have been read */
        do {
            /* lowest_rec is the record pointed to by the index at the
             * top of the heap */
            memcpy(lowest_rec, recs[lowest], NODE_SIZE);

            /* write the record */
            if (fp_intermediate) {
                /* write the record to intermediate tmp file */
                rv = skStreamWrite(fp_intermediate, lowest_rec, NODE_SIZE);
                if (NODE_SIZE != rv) {
                    if (rv > 0) {
                        snprintf(
                            errbuf, sizeof(errbuf),
                            "Short write %" SK_PRIdZ "/%" SK_PRIuZ " to '%s'",
                            rv,NODE_SIZE,skStreamGetPathname(fp_intermediate));
                    } else {
                        skStreamLastErrMessage(
                            fps[open_count], rv, errbuf, sizeof(errbuf));
                    }
                    skAppPrintErr("Error writing to temporary file: %s",
                                  errbuf);
                    skStreamDestroy(&fp_intermediate);
                    skAppPrintSyserror(
                        "Error writing record to temporary file '%s'",
                        skTempFileGetName(tmpctx, tmp_idx_intermediate));
                    appExit(EXIT_FAILURE);
                }
            } else {
                /* we successfully opened all (remaining) temp files,
                 * write to record to the final destination */
                rv = skStreamWriteRecord(out_stream, (rwRec*)lowest_rec);
                if (0 != rv) {
                    skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
                    if (SKSTREAM_ERROR_IS_FATAL(rv)) {
                        appExit(EXIT_FAILURE);
                    }
                }
            }

            /* replace the record we just processed and loop over all
             * files until we get a record that is not a duplicate */
            do {
                if ((rv = skStreamRead(fps[lowest], recs[lowest], NODE_SIZE))
                    != NODE_SIZE)
                {
                    /* read failed.  there is no more data for this
                     * file; remove it from the heap; if the heap is
                     * empty, exit the loop */
                    skHeapExtractTop(heap, NULL);
                    --heap_count;
#if TRACEMSG_LEVEL > 0
                    if (rv == 0) {
                        TRACEMSG(
                            ("Finished reading file #%u: EOF; %u files remain",
                             (tmp_idx_a + lowest), heap_count));
                    } else if (rv > 0) {
                        TRACEMSG(
                            ("Finished reading file #%u: Short read "
                             "%" SK_PRIdZ "/%" SK_PRIuZ "; %u files remain",
                             tmp_idx_a + lowest, rv, NODE_SIZE, heap_count));
                    } else {
                        skStreamLastErrMessage(
                            fps[open_count], rv, errbuf, sizeof(errbuf));
                        TRACEMSG(
                            ("Finished reading file #%u: %s; %u files remain",
                             (tmp_idx_a + lowest), errbuf, heap_count));
                    }
#endif  /* TRACEMSG_LEVEL */
                    if (0 == heap_count) {
                        break;
                    }

                } else if (rwrecCompare(lowest_rec, recs[lowest])) {
                    /* read succeeded.  new record is not a
                     * duplicate and we insert it into the heap */
                    /* FIXME: This comparison reduces work when the
                     * keys are the same, but it adds another
                     * comparison when the keys are different; is this
                     * an overall win or lose? */
                    skHeapReplaceTop(heap, &lowest, NULL);

                } else {
                    /* read succeeded.  record is a duplicate; ignore
                     * the record and leave the heap unchanged */
                    continue;
                }

                /* get the record at the top of the heap and see if it
                 * is a duplicate; if it is, ignore it. */
                skHeapPeekTop(heap, (skheapnode_t*)&top_heap);
                lowest = *top_heap;
            } while (0 == rwrecCompare(lowest_rec, recs[lowest]));
        } while (heap_count > 0);

        TRACEMSG((("Finished processing #%d through #%d"),
                  tmp_idx_a, tmp_idx_b));

        /* Close all open temp files */
        for (i = 0; i < open_count; ++i) {
            skStreamDestroy(&fps[i]);
        }
        /* Delete all temp files we opened (or attempted to open) this
         * time */
        for (j = tmp_idx_a; j <= tmp_idx_b; ++j) {
            skTempFileRemove(tmpctx, j);
        }

        /* Close the intermediate temp file. */
        if (fp_intermediate) {
            rv = skStreamClose(fp_intermediate);
            if (rv) {
                skStreamLastErrMessage(
                    fp_intermediate, rv, errbuf, sizeof(errbuf));
                skAppPrintErr("Error closing temporary file: %s", errbuf);
                skStreamDestroy(&fp_intermediate);
                appExit(EXIT_FAILURE);
            }
            skStreamDestroy(&fp_intermediate);
        }

        /* Start the next merge with the next input temp file */
        tmp_idx_a = tmp_idx_b + 1;

    } while (!opened_all_temps);

    skHeapFree(heap);
}


/*
 *  sortRandom();
 *
 *    Don't make any assumptions about the input.  Store the input
 *    records in a large buffer, and sort those in-core records once
 *    all records are processed or the buffer is full.  If the buffer
 *    fills up, store the sorted records into temporary files.  Once
 *    all records are read, use mergeFiles() above to merge-sort the
 *    temporary files.
 *
 *    Exits the application if an error occurs.
 */
static void
sortRandom(
    void)
{
    int temp_file_idx = -1;
    skstream_t *input_stream = NULL;/* input stream */
    uint8_t *record_buffer = NULL;  /* Region of memory for records */
    uint8_t *cur_node = NULL;       /* Ptr into record_buffer */
    uint8_t *next_node = NULL;      /* Ptr into record_buffer */
    size_t buffer_max_recs;         /* max buffer size (in number of recs) */
    size_t buffer_recs;             /* current buffer size (# records) */
    size_t buffer_chunk_recs;       /* how to grow from current to max buf */
    size_t num_chunks;              /* how quickly to grow buffer */
    size_t record_count = 0;        /* Number of records read */
    ssize_t rv;

    /* Determine the maximum number of records that will fit into the
     * buffer if it grows the maximum size */
    buffer_max_recs = buffer_size / NODE_SIZE;
    TRACEMSG((("buffer_size = %" SK_PRIuZ
               "\nnode_size = %" SK_PRIuZ
               "\nbuffer_max_recs = %" SK_PRIuZ),
              buffer_size, NODE_SIZE, buffer_max_recs));

    /* We will grow to the maximum size in chunks; do not allocate
     * more than MAX_CHUNK_SIZE at any time */
    num_chunks = NUM_CHUNKS;
    if (num_chunks < 1) {
        num_chunks = 1;
    }
    if (buffer_size / num_chunks > MAX_CHUNK_SIZE) {
        num_chunks = buffer_size / MAX_CHUNK_SIZE;
    }

    /* Attempt to allocate the initial chunk.  If we fail, increment
     * the number of chunks---which will decrease the amount we
     * attempt to allocate at once---and try again. */
    for (;;) {
        buffer_chunk_recs = buffer_max_recs / num_chunks;
        TRACEMSG((("num_chunks = %" SK_PRIuZ
                   "\nbuffer_chunk_recs = %" SK_PRIuZ),
                  num_chunks, buffer_chunk_recs));

        record_buffer = (uint8_t*)malloc(NODE_SIZE * buffer_chunk_recs);
        if (record_buffer) {
            /* malloc was successful */
            break;
        } else if (buffer_chunk_recs < MIN_IN_CORE_RECORDS) {
            /* give up at this point */
            skAppPrintErr("Error allocating space for %d records",
                          MIN_IN_CORE_RECORDS);
            appExit(EXIT_FAILURE);
        } else {
            /* reduce the amount we allocate at once by increasing the
             * number of chunks and try again */
            TRACEMSG(("malloc() failed"));
            ++num_chunks;
        }
    }

    buffer_recs = buffer_chunk_recs;
    TRACEMSG((("buffer_recs = %" SK_PRIuZ), buffer_recs));

    /* open first file */
    rv = appNextInput(&input_stream);
    if (rv < 0) {
        free(record_buffer);
        appExit(EXIT_FAILURE);
    }

    record_count = 0;
    cur_node = record_buffer;
    while (input_stream != NULL) {
        /* read record */
        if ((rv = skStreamReadRecord(input_stream, (rwRec*)cur_node))
            != SKSTREAM_OK)
        {
            if (rv != SKSTREAM_ERR_EOF) {
                skStreamPrintLastErr(input_stream, rv, &skAppPrintErr);
            }
            /* end of file: close current and open next */
            skStreamDestroy(&input_stream);
            rv = appNextInput(&input_stream);
            if (rv < 0) {
                free(record_buffer);
                appExit(EXIT_FAILURE);
            }
            continue;
        }

        ++record_count;
        cur_node += NODE_SIZE;

        if (record_count == buffer_recs) {
            /* Filled the current buffer */

            /* If buffer not at max size, see if we can grow it */
            if (buffer_recs < buffer_max_recs) {
                uint8_t *old_buf = record_buffer;

                /* add a chunk of records.  if we are near the max,
                 * set the size to the max */
                buffer_recs += buffer_chunk_recs;
                if (buffer_recs + buffer_chunk_recs > buffer_max_recs) {
                    buffer_recs = buffer_max_recs;
                }
                TRACEMSG((("Buffer full---attempt to grow to %" SK_PRIuZ
                           " records, %" SK_PRIuZ " bytes"),
                          buffer_recs, NODE_SIZE * buffer_recs));

                /* attempt to grow */
                record_buffer = (uint8_t*)realloc(record_buffer,
                                                  NODE_SIZE * buffer_recs);
                if (record_buffer) {
                    /* Success, make certain cur_node points into the
                     * new buffer */
                    cur_node = (record_buffer + (record_count * NODE_SIZE));
                } else {
                    /* Unable to grow it */
                    TRACEMSG(("realloc() failed"));
                    record_buffer = old_buf;
                    buffer_max_recs = buffer_recs = record_count;
                }
            }

            /* Either buffer at maximum size or attempt to grow it
             * failed. */
            if (record_count == buffer_max_recs) {
                /* Sort */
                TRACEMSG(("Sorting %" SK_PRIuZ " records...", record_count));
                skQSort(record_buffer, record_count, NODE_SIZE, &rwrecCompare);
                TRACEMSG(("Sorting %" SK_PRIuZ " records...done",
                          record_count));

                /* Write to temp file */
                if (skTempFileWriteBufferStream(
                        tmpctx, &temp_file_idx,
                        record_buffer, NODE_SIZE, record_count))
                {
                    skAppPrintSyserror(
                        "Error writing sorted buffer to temporary file");
                    free(record_buffer);
                    appExit(EXIT_FAILURE);
                }

                /* Reset record buffer to 'empty' */
                record_count = 0;
                cur_node = record_buffer;
            }
        }
    }

    /* Sort (and maybe store) last batch of records */
    if (record_count > 0) {
        TRACEMSG(("Sorting %" SK_PRIuZ " records...", record_count));
        skQSort(record_buffer, record_count, NODE_SIZE, &rwrecCompare);
        TRACEMSG(("Sorting %" SK_PRIuZ " records...done", record_count));

        if (temp_file_idx >= 0) {
            /* Write last batch to temp file */
            if (skTempFileWriteBufferStream(
                    tmpctx, &temp_file_idx,
                    record_buffer, NODE_SIZE, record_count))
            {
                skAppPrintSyserror(
                    "Error writing sorted buffer to temporary file");
                free(record_buffer);
                appExit(EXIT_FAILURE);
            }
        }
    }

    /* Generate the output */

    if (record_count == 0 && temp_file_idx == -1) {
        /* No records were read at all; write the header to the output
         * file */
        rv = skStreamWriteSilkHeader(out_stream);
        if (0 != rv) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        }
    } else if (temp_file_idx == -1) {
        /* No temp files written, just output batch of records */
        size_t c;

        TRACEMSG((("Deduplicating %" SK_PRIuZ " records and writing"
                   " the result to '%s'"),
                  record_count, skStreamGetPathname(out_stream)));
        /* get first two records from the sorted buffer */
        cur_node = record_buffer;
        next_node = record_buffer + NODE_SIZE;
        for (c = 1; c < record_count; ++c, next_node += NODE_SIZE) {
            if (0 != rwrecCompare(cur_node, next_node)) {
                /* records differ. print earlier record */
                rv = skStreamWriteRecord(out_stream, (rwRec*)cur_node);
                if (0 != rv) {
                    skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
                    if (SKSTREAM_ERROR_IS_FATAL(rv)) {
                        free(record_buffer);
                        appExit(EXIT_FAILURE);
                    }
                }
                cur_node = next_node;
            }
            /* else records are duplicates: ignore latter record */
        }
        /* print remaining record */
        rv = skStreamWriteRecord(out_stream, (rwRec*)cur_node);
        if (0 != rv) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            if (SKSTREAM_ERROR_IS_FATAL(rv)) {
                free(record_buffer);
                appExit(EXIT_FAILURE);
            }
        }
    } else {
        /* no longer have a need for the record buffer */
        free(record_buffer);
        record_buffer = NULL;

        /* now merge all the temp files */
        mergeFiles(temp_file_idx);
    }

    if (record_buffer) {
        free(record_buffer);
    }
}


int main(int argc, char **argv)
{
    int rv;

    appSetup(argc, argv);                 /* never returns on error */

    sortRandom();

    /* close the file */
    if ((rv = skStreamClose(out_stream))
        || (rv = skStreamDestroy(&out_stream)))
    {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        appExit(EXIT_FAILURE);
    }
    out_stream = NULL;

    appExit(EXIT_SUCCESS);
    return 0; /* NOTREACHED */
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
