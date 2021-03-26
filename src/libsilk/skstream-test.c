/*
** Copyright (C) 2007-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skstream-test.c
**
**    Test the binary capability of the skstream functions.
**
**  Mark Thomas
**  June 2007
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: skstream-test.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/sksite.h>
#include <silk/utils.h>
#include <silk/skstream.h>


int main(int argc, char **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    uint8_t buffer[1 << 15];
    skstream_t *s_in = NULL;
    skstream_t *s_out = NULL;
    int rv;
    ssize_t got;
    ssize_t put;
    off_t len;
    int i;

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <source> <dest>\n", skAppName());
        exit(EXIT_FAILURE);
    }

    if ((rv = (skStreamCreate(&s_in, SK_IO_READ, SK_CONTENT_OTHERBINARY)))
        || (rv = skStreamBind(s_in, argv[1]))
        || (rv = skStreamOpen(s_in)))
    {
        skStreamPrintLastErr(s_in, rv, &skAppPrintErr);
        goto END;
    }

    if ((rv = (skStreamCreate(&s_out, SK_IO_WRITE, SK_CONTENT_OTHERBINARY)))
        || (rv = skStreamBind(s_out, argv[2]))
        || (rv = skStreamOpen(s_out)))
    {
        skStreamPrintLastErr(s_out, rv, &skAppPrintErr);
        goto END;
    }

    do {
        got = skStreamRead(s_in, buffer, sizeof(buffer));
        if (got > 0) {
            put = skStreamWrite(s_out, buffer, got);
            if (got != put) {
                if (put < 0) {
                    skStreamPrintLastErr(s_out, put, &skAppPrintErr);
                } else {
                    skAppPrintErr("Warning: read %ld bytes and wrote %ld bytes",
                                  (long)got, (long)put);
                }
            }
        }
    } while (got > 0);

    if (got < 0) {
        skStreamPrintLastErr(s_in, got, &skAppPrintErr);
    }

    if (skStreamIsSeekable(s_out)) {
        /* get the current position in the output, write the buffer to
         * the output a couple of times, then truncate the output to
         * the current position */
        if ((rv = skStreamFlush(s_out))
            || ((len = skStreamTell(s_out)) == (off_t)-1))
        {
            skStreamPrintLastErr(s_out, rv, &skAppPrintErr);
            goto END;
        }

        memset(buffer, 0x55, sizeof(buffer));
        got = sizeof(buffer);

        for (i = 0; i < 2; ++i) {
            put = skStreamWrite(s_out, buffer, got);
            if (got != put) {
                skStreamPrintLastErr(s_out, rv, &skAppPrintErr);
                skAppPrintErr("Warning: have %ld bytes and wrote %ld bytes",
                              (long)got, (long)put);
            }
        }

        rv = skStreamTruncate(s_out, len);
        if (rv) {
            skStreamPrintLastErr(s_out, rv, &skAppPrintErr);
        }
    }

  END:
    rv = skStreamDestroy(&s_in);
    if (rv) {
        skStreamPrintLastErr(s_in, rv, &skAppPrintErr);
    }
    rv = skStreamClose(s_out);
    if (rv) {
        skStreamPrintLastErr(s_out, rv, &skAppPrintErr);
    }
    rv = skStreamDestroy(&s_out);
    if (rv) {
        skStreamPrintLastErr(s_out, rv, &skAppPrintErr);
    }

    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
