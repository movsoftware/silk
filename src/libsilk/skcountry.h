/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skcountry.h
**
**    Functions for getting the two letter country code value for an
**    IP address.
**
**    Based on ccfilter_priv.h by Katherine Prevost, December 2004
**
**    Mark Thomas
**
*/
#ifndef _SKCOUNTRY_H
#define _SKCOUNTRY_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKCOUNTRY_H, "$SiLK: skcountry.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/silk_types.h>
#include <silk/skplugin.h>
#include <silk/skprefixmap.h>

/**
 *  @file
 *
 *    Functions for processing a specially designed binary prefix map
 *    file whose entries have a two-later country code as their value.
 *
 *    This file is part of libsilk.
 */


#define SK_COUNTRYCODE_INVALID      ((sk_countrycode_t)32383)

/**
 *    This contains the name of an environment variable.  If that
 *    variable is set, it should name the country code file to use.
 */
#define SK_COUNTRY_MAP_ENVAR        "SILK_COUNTRY_CODES"

/**
 *    If a country code data file name is not provided (neither in the
 *    environment nor via command line switches where
 *    supported/required) this is the name of mapping file.
 */
#define SK_COUNTRY_DEFAULT_MAP      "country_codes.pmap"


/**
 *    Abstract type for country code values
 */
typedef uint16_t sk_countrycode_t;


/**
 *    Return the maximum possible country code value.
 */
sk_countrycode_t
skCountryGetMaxCode(
    void);


/**
 *    Given a two letter Country Code in 'name', return the numerical
 *    value.  Returns SK_COUNTRYCODE_INVALID if 'name' is too long to
 *    be Country Code or contains illegal characters.  Returns
 *    SK_COUNTRYCODE_INVALID unless 'name' is ASCII and contains two
 *    letters, a letter followed by a number, or the string "--".  The
 *    'name' is not compared with the country code mapping file, so
 *    returned value may not reflect a known Country Code.
 */
sk_countrycode_t
skCountryNameToCode(
    const char         *name);


/**
 *    Given a numeric Country Code in 'code', fill 'name' with the two
 *    letter representation of the code, where 'name_len' is the
 *    number of characters in 'name'.
 *
 *    Return NULL if 'name' is NULL or 'name_len' is zero.  If 'code'
 *    is not a possible Country Code, writes "??" to name.  The 'code'
 *    is not compared with the country code mapping file, so the
 *    returned name may not reflect a known country.
 */
char *
skCountryCodeToName(
    sk_countrycode_t    code,
    char               *name,
    size_t              name_len);


/**
 *    Return a handle to the prefix map supporting the Country Codes.
 */
const skPrefixMap_t *
skCountryGetPrefixMap(
    void);


/**
 *    Return 1 if the Country Code map contains IPv6 addresses.
 *    Return 0 if the Country Code map contains only IPv4 addresses.
 *    Return -1 if the Country Code map is not available.
 */
int
skCountryIsV6(
    void);


/**
 *    Find the Country Code for the IP address 'ipaddr' in the prefix
 *    map file and return the numerical value.  The caller must invoke
 *    skCountrySetup() prior to calling this function.
 *
 *    Return SK_INVALID_COUNTRY_CODE if the Country Code map has not
 *    been loaded or if the Country Code map contains only IPv4
 *    addresses and 'ipaddr' is IPv6.
 *
 *    See also skCountryLookupName(), skCountryLookupCodeAndRange().
 */
sk_countrycode_t
skCountryLookupCode(
    const skipaddr_t   *ipaddr);


/**
 *    Find the Country Code for the IP address 'ipaddr' in the prefix
 *    map file and return the numerical value.  The caller must invoke
 *    skCountrySetup() prior to calling this function.
 *
 *    In addition, set the referents of 'start_range' and 'end_range'
 *    to the starting and ending IP addresses of the CIDR block in the
 *    Country Code mapping file that contains 'ipaddr'.
 *
 *    Return SK_INVALID_COUNTRY_CODE and leave 'start_range' and
 *    'end_range' unchanged if the Country Code map has not been
 *    loaded or if the Country Code map contains only IPv4 addresses
 *    and 'ipaddr' is IPv6.
 *
 *    See also skCountryLookupCode(), skCountryLookupName().
 */
sk_countrycode_t
skCountryLookupCodeAndRange(
    const skipaddr_t   *ipaddr,
    skipaddr_t         *start_range,
    skipaddr_t         *end_range);


/**
 *    Find the Country Code for the IP address 'ipaddr' in the prefix
 *    map file and fill the buffer 'name' with the two letter Country
 *    Code.  The caller must invoke skCountrySetup() prior to calling
 *    this function.
 *
 *    Return NULL if 'name' is NULL or 'name_len' is zero.  If the
 *    Country Code map contains only IPv4 addresses and 'ipaddr' is
 *    IPv6 or if the address cannot be mapped for any other reason,
 *    write "??" to name.
 *
 *    See also skCountryLookupCode(), skCountryLookupCodeAndRange().
 */
char *
skCountryLookupName(
    const skipaddr_t   *ipaddr,
    char               *name,
    size_t              name_len);


/**
 *    Load the Country Code map for use by the skCountryLookupCode()
 *    and skCountryLookupName() functions.
 *
 *    Use the Country Code map name in 'map_name' if that value is
 *    provided.  If not, the environment variable named by
 *    SK_COUNTRY_ENVAR is used.  If that is empty, the
 *    SK_COUNTRY_DEFAULT_MAP is used.
 *
 *    Return 0 on success or non-zero if the map cannot be found or
 *    there is a problem reading the file.  On error, a messages will
 *    be printed using 'errfn' if non-NULL.
 *
 *    If the Country Code map was previously initialized, this
 *    function returns 0.  To load a different map, first destroy the
 *    current mapping by calling skCountryTeardown().
 */
int
skCountrySetup(
    const char         *map_name,
    sk_msg_fn_t         errfn);


/**
 *    Remove the Country Code mapping file from memory.
 */
void
skCountryTeardown(
    void);


/**
 *    Add support for the --scc and --dcc switches in rwfilter, and
 *    the 'scc' and 'dcc' fields in rwcut, rwgroup, rwsort, rwuniq,
 *    and rwstats.
 */
skplugin_err_t
skCountryAddFields(
    uint16_t            major_version,
    uint16_t            minor_version,
    void        UNUSED(*pi_data));


#ifdef __cplusplus
}
#endif
#endif /* _SKCOUNTRY_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
