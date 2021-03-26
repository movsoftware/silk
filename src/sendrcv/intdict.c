/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Integer dictionaries
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: intdict.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/redblack.h>
#include "intdict.h"
#include "libsendrcv.h"

/* SENDRCV_DEBUG is defined in libsendrcv.h */
#if (SENDRCV_DEBUG) & DEBUG_INTDICT_MUTEX
#  define SKTHREAD_DEBUG_MUTEX 1
#endif
#include <silk/skthread.h>


/* LOCAL DEFINES AND TYPEDEFS */

typedef struct int_dict_node_st {
    /* Integer key */
    intkey_t key;

    /* 'value' is used as an address of the contained data */
    void *   value[1];
    /* Making 'value' an array of void * forces 'value' to be aligned
     * on a pointer boundary, which can be important if a struct
     * contained within contains pointers. */
} int_dict_node_t;

struct int_dict_st {
    struct rbtree   *tree;
    int_dict_node_t *tnode;
    size_t           value_size;
    unsigned int     count;
    RWMUTEX          mutex;
};

struct int_dict_iter_st {
    int_dict_t *dict;
    RBLIST     *list;
};


static int
int_node_compare(
    const void         *va,
    const void         *vb,
    const void  UNUSED(*dummy))
{
    int_dict_node_t *a = (int_dict_node_t *)va;
    int_dict_node_t *b = (int_dict_node_t *)vb;

    if (a->key > b->key) {
        return 1;
    }
    if (a->key < b->key) {
        return -1;
    }
    return 0;
}


int_dict_t *
int_dict_create(
    size_t              value_size)
{
    int_dict_t *d;

    d = (int_dict_t*)malloc(sizeof(int_dict_t));
    if (d == NULL) {
        return NULL;
    }
    d->tree = rbinit(int_node_compare, NULL);
    if (d->tree == NULL) {
        free(d);
        return NULL;
    }
    d->tnode = NULL;
    d->value_size = value_size;
    d->count = 0;
    RW_MUTEX_INIT(&d->mutex);
    return d;
}


static void *
int_dict_loookup(
    int                 mode,
    int_dict_t         *dict,
    intkey_t           *key,
    void               *value)
{
    int_dict_node_t target;
    int_dict_node_t *node;

    assert(dict);
    assert(dict->tree);

    if (key != NULL) {
        target.key = *key;
    }

    READ_LOCK(&dict->mutex);
    node = (int_dict_node_t *)rblookup(mode, &target, dict->tree);
    if (node == NULL) {
        RW_MUTEX_UNLOCK(&dict->mutex);
        return NULL;
    }

    if (key != NULL) {
        *key = node->key;
    }
    if (value != NULL) {
        memcpy(value, node->value, dict->value_size);
    }
    RW_MUTEX_UNLOCK(&dict->mutex);

    return node->value;
}

void *
int_dict_get(
    int_dict_t         *dict,
    intkey_t            key,
    void               *value)
{
    return int_dict_loookup(RB_LUEQUAL, dict, &key, value);
}

void *
int_dict_get_first(
    int_dict_t         *dict,
    intkey_t           *key,
    void               *value)
{
    return int_dict_loookup(RB_LUFIRST, dict, key, value);
}

void *
int_dict_get_last(
    int_dict_t         *dict,
    intkey_t           *key,
    void               *value)
{
    return int_dict_loookup(RB_LULAST, dict, key, value);
}

void *
int_dict_get_next(
    int_dict_t         *dict,
    intkey_t           *key,
    void               *value)
{
    return int_dict_loookup(RB_LUGREAT, dict, key, value);
}

void *
int_dict_get_prev(
    int_dict_t         *dict,
    intkey_t           *key,
    void               *value)
{
    return int_dict_loookup(RB_LULESS, dict, key, value);
}


int
int_dict_set(
    int_dict_t         *dict,
    intkey_t            key,
    void               *value)
{
    int_dict_node_t *node;
    int retval = -1;

    assert(dict);
    assert(dict->tree);

    WRITE_LOCK(&dict->mutex);

    if (dict->tnode == NULL) {
        dict->tnode = (int_dict_node_t*)malloc(offsetof(int_dict_node_t, value)
                                               + dict->value_size);
        if (dict->tnode == NULL) {
            goto end;
        }
    }

    dict->tnode->key = key;

    node = (int_dict_node_t *)rbsearch(dict->tnode, dict->tree);
    if (node == NULL) {
        goto end;
    }
    memcpy(node->value, value, dict->value_size);

    if (node == dict->tnode) {
        dict->count++;
        dict->tnode = NULL;
    }

    retval = 0;

  end:
    RW_MUTEX_UNLOCK(&dict->mutex);
    return retval;
}


int
int_dict_del(
    int_dict_t         *dict,
    intkey_t            key)
{
    int_dict_node_t target;
    int_dict_node_t *node;

    assert(dict);
    assert(dict->tree);

    target.key = key;

    WRITE_LOCK(&dict->mutex);
    node = (int_dict_node_t *)rbdelete(&target, dict->tree);
    RW_MUTEX_UNLOCK(&dict->mutex);

    if (node == NULL) {
        return 1;
    }
    dict->count--;

    free(node);
    return 0;
}


unsigned int
int_dict_get_count(
    int_dict_t         *dict)
{
    assert(dict);
    return dict->count;
}


static void
destroy_node(
    const void         *node,
    const VISIT         order,
    const int    UNUSED(depth),
    void        UNUSED(*dummy))
{
    if (order == leaf || order == preorder) {
        free((void *)node);
    }
}


void
int_dict_destroy(
    int_dict_t         *dict)
{
    assert(dict);
    assert(dict->tree);

    WRITE_LOCK(&dict->mutex);
    rbwalk(dict->tree, destroy_node, NULL);
    rbdestroy(dict->tree);
    if (dict->tnode != NULL) {
        free(dict->tnode);
    }
    RW_MUTEX_UNLOCK(&dict->mutex);
    RW_MUTEX_DESTROY(&dict->mutex);
    free(dict);
}


int_dict_iter_t *
int_dict_open(
    int_dict_t         *dict)
{
    int_dict_iter_t *iter;

    assert(dict);

    iter = (int_dict_iter_t*)malloc(sizeof(int_dict_iter_t));
    if (iter == NULL) {
        return NULL;
    }
    iter->dict = dict;
    READ_LOCK(&dict->mutex);
    iter->list = rbopenlist(dict->tree);
    RW_MUTEX_UNLOCK(&dict->mutex);
    if (iter->list == NULL) {
        free(iter);
        return NULL;
    }
    return iter;
}


void *
int_dict_next(
    int_dict_iter_t    *iter,
    intkey_t           *key,
    void               *value)
{
    int_dict_node_t *node;

    assert(iter);
    READ_LOCK(&iter->dict->mutex);
    node = (int_dict_node_t *)rbreadlist(iter->list);
    if (node == NULL) {
        RW_MUTEX_UNLOCK(&iter->dict->mutex);
        return NULL;
    }
    if (key != NULL) {
        *key = node->key;
    }

    if (value) {
        memcpy(value, node->value, iter->dict->value_size);
    }
    RW_MUTEX_UNLOCK(&iter->dict->mutex);

    return node->value;
}


void
int_dict_close(
    int_dict_iter_t    *iter)
{
    assert(iter);
    rbcloselist(iter->list);
    free(iter);
}



/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
