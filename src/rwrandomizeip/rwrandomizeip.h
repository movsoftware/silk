/*
** Copyright (C) 2007-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWRANDOMIZEIP_H
#define _RWRANDOMIZEIP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWRANDOMIZEIP_H, "$SiLK: rwrandomizeip.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/*
**  rwrandomizeip.h
**
**  An interface to allow additional randomization routines
**  (back-ends) to be added easily (I hope).
**
*/

#include <silk/utils.h>
#include <silk/skstream.h>
#include <silk/skipset.h>


/* Invent our own value for maximum returned by random(), since
 * RAND_MAX on some OSes is for random(), and on others (yes, that
 * means you Solaris) its for rand(). */
#define SK_MAX_RANDOM 0x7fffffff


/*
 *    The main rwrandomizeip application calls this function to
 *    initialize a randomizer back-end.  This function should in turn
 *    call rwrandomizerRegister() and rwrandomizerRegisterOption() so
 *    the back-end can register the functions and options it provides.
 *
 *    Currently the list of these functions is maintained in an array
 *    in rwrandomizeip.c; whenever we add plug-in support, this would
 *    be a function that all plug-ins would supply.
 */
typedef int (*randomizer_load_fn_t)(void);

/*
 *    Once main has determined which randomizer back-end to use, only
 *    that back-end's activate function is called to activate the
 *    back-end.  This function should do any initialization and checks
 *    required prior to reading the data.
 *
 *    The 'back_end_data' is whatever 'back_end_data' value was passed
 *    to the rwrandomizerRegister() function.
 */
typedef int (*randomizer_activate_fn_t)(void *back_end_data);

/*
 *    For the active randomizer back-end, this function will be called
 *    with each source and destination address to be changed.  This
 *    function should modify the value in place.
 */
typedef void (*randomizer_modifyip_fn_t)(uint32_t *ip);


/*
 *    Once processing of input is complete, the deactivate function is
 *    called.  This function is only called for the back-end that is
 *    active.
 *
 *    The 'back_end_data' is whatever 'back_end_data' value was passed
 *    to the rwrandomizerRegister() function.
 */
typedef int (*randomizer_deactivate_fn_t)(void *back_end_data);

/*
 *    The unload function is called for all back-ends, regardless or
 *    whether they were active.  This function should do any final
 *    cleanup, undoing anything that the 'load' function did.
 *
 *    The 'back_end_data' is whatever 'back_end_data' value was passed
 *    to the rwrandomizerRegister() function.
 */
typedef void (*randomizer_unload_fn_t)(void *back_end_data);

/*
 *    The randomizer back-end registers options with the main
 *    rwrandomizeip application.  When the user specifies one of those
 *    options, this function will be invoked with the option's
 *    parameter as the 'opt_arg' value---or NULL for no value---and
 *    the same 'back_end_data' that was passed to the
 *    rwrandomizerRegisterOption() function.
 */
typedef int (*randomizer_optioncb_fn_t)(char *opt_arg, void *back_end_data);


/*
 *    Each randomization back-end calls this function to register the
 *    back-end with the main rwrandomizeip application.  For a
 *    description of each parameter, see the descriptions of the
 *    typedefs above.
 */
int
rwrandomizerRegister(
    randomizer_activate_fn_t    activate_fn,
    randomizer_modifyip_fn_t    modifyip_fn,
    randomizer_deactivate_fn_t  deactivate_fn,
    randomizer_unload_fn_t      unload_fn,
    void                       *back_end_data);


/*
 *    Any options that the back-end accepts must be registered with
 *    the main rwrandomizeip application by calling this function.
 *
 *    Each back-end will need to register each one option which allows
 *    that back-end to be used; otherwise, rwrandomizeip will fallback
 *    to its default randomization function.
 *
 *    'option_name' is the name of the option; 'option_help' is its
 *    help (usage) string; 'callback_fn' is the function to call with
 *    the user enters the option; the 'callback_fn' will be given the
 *    option's value and whatever the back-end has specified in the
 *    'back_end_data' value' 'has_arg' says whether the option takes
 *    an argument---it should be one of REQUIRED_ARG, OPTIONAL_ARG,
 *    NO_ARG.
 */
int
rwrandomizerRegisterOption(
    const char                 *option_name,
    const char                 *option_help,
    randomizer_optioncb_fn_t    callback_fn,
    void                       *back_end_data,
    int                         has_arg);


/*
 *   Function prototypes for the 'load' functions from any
 *   randomization back-ends.
 */
int
rwrandShuffleLoad(
    void);


#ifdef __cplusplus
}
#endif
#endif /* _RWRANDOMIZEIP_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
