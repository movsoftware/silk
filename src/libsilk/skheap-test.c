/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skheap-test.c
**
**  a simple testing harness for the skheap data structure library
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: skheap-test.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/skheap.h>


static int cmpfun_data = 0x55555555;


static int
compare(
    const skheapnode_t  node1,
    const skheapnode_t  node2)
{
    int a;
    int b;

    a = *(((int*)node1)+1);
    b = *(((int*)node2)+1);
    if (a > b) {
        return -1;
    }
    if (a < b) {
        return 1;
    }
    return 0;
}


static int
compare2(
    const skheapnode_t  node1,
    const skheapnode_t  node2,
    void               *cb_data)
{
    int a;
    int b;

    if (cmpfun_data != *(int*)cb_data) {
        fprintf(stderr, "Invalid cb_data passed to compare2().\n");
        exit(EXIT_FAILURE);
    }

    a = *(int*)node1;
    b = *(int*)node2;
    if (b > a) {
        return -1;
    }
    return (b < a);
}


int main(void)
{
#define DATA_SIZE 15
    skheap_t *heap;
    int data[2*DATA_SIZE] = {
        201, 34, 202, 56, 203,  2,
        204, 65, 205,  3, 206,  5,
        207,  8, 208, 74, 209, 32,
        210, 78, 211, 79, 212, 80,
        213,  5, 214,  5, 215,  1};
    int heaps_data[2*DATA_SIZE];
    int i;
    int j;
    int *iptr;
    uint32_t k;
    int* top;
    int top_value[2];
    int status;
    int replace_tested = 0;
    int heap_init_size = 10;
    skheapiterator_t *iter_down;
    skheapiterator_t *iter_up;


    /* first run uses a heap where caller provides the memory */

    heap = skHeapCreate(&compare, heap_init_size, 2*sizeof(int),
                        (skheapnode_t*)heaps_data);
    if (NULL == heap) {
        printf("Cannot create heap\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0, iptr = data; i < DATA_SIZE; ++i, iptr += 2) {
        if (*iptr == 206) {
            continue;
        }
        printf("\n** adding %d/%d...", data[2*i], data[2*i+1]);
        status = skHeapInsert(heap, (skheapnode_t)iptr);
        if (SKHEAP_OK == status) {
            printf("OK\n");
        } else if (SKHEAP_ERR_FULL != status) {
            printf("NOPE. Got wierd error status %d\n", status);
        } else {
            printf("NOPE. Heap full.  Comparing with the top.\n");
            skHeapPeekTop(heap, (skheapnode_t*)&top);
            if (0 >= compare(top, iptr)) {
                printf("Not added to heap since <= top (%d/%d) [%d]\n",
                       *top, *(top+1), compare(top, iptr));
            } else if (!replace_tested) {
                replace_tested = 1;
                printf("Replacing top of heap (%d/%d)...", *top, *(top+1));
                if (skHeapReplaceTop(heap, (skheapnode_t)iptr, NULL)
                    == SKHEAP_OK)
                {
                    printf("OK\n");
                } else {
                    printf("Problem adding '%d/%d' to heap\n",
                           data[2*i], data[2*i+1]);
                }
            } else {
                printf("Removing top of heap (%d/%d)...", *top, *(top+1));
                skHeapExtractTop(heap, NULL);
                if (SKHEAP_OK == skHeapInsert(heap, (skheapnode_t)iptr)) {
                    printf("OK\n");
                } else {
                    printf("Problem adding '%d/%d' to heap\n",
                           data[2*i], data[2*i+1]);
                }
            }
        }
        printf("heap %d/%d\n",
               skHeapGetNumberEntries(heap),
               skHeapGetCapacity(heap));
        for (k = 0; k < skHeapGetNumberEntries(heap); ++k) {
            printf("%5d  %d/%d\n", k,
                   heaps_data[2*k], heaps_data[2*k+1]);
        }

        if (i == 0) {
            printf("\n** Sorting the heap...");
            if (SKHEAP_OK == skHeapSortEntries(heap)) {
                printf("OK\n");
            }
        }
    }

    printf("\n** Sorting the heap...");
    if (SKHEAP_OK == skHeapSortEntries(heap)) {
        printf("OK\n");
    } else {
        printf("Got error\n");
    }
    printf("heap %d/%d\n",
           skHeapGetNumberEntries(heap), skHeapGetCapacity(heap));
    for (k = 0; k < skHeapGetNumberEntries(heap); ++k) {
        printf("%5d  %d/%d\n", k,
               heaps_data[2*k], heaps_data[2*k+1]);
    }

    printf("\n** Iterating over the heap...\n");
    iter_down = skHeapIteratorCreate(heap, 1);
    iter_up = skHeapIteratorCreate(heap, -1);
    while (skHeapIteratorNext(iter_down, (skheapnode_t*)&iptr) == SKHEAP_OK) {
        printf("Down: %d/%d\t\t", *iptr, *(iptr+1));
        skHeapIteratorNext(iter_up, (skheapnode_t*)&iptr);
        printf("Up: %d/%d\n", *iptr, *(iptr+1));
    }
    skHeapIteratorFree(iter_down);
    skHeapIteratorFree(iter_up);

    printf("\n** Removing sorted data from the heap:\n");
    while (SKHEAP_OK == skHeapExtractTop(heap, (skheapnode_t)top_value)) {
        printf("%d/%d\n", top_value[0], top_value[1]);
    }

    skHeapFree(heap);


    /* second run uses a heap where heap manages its own memory */

    printf("\n** Creating growable heap with initial size %d...",
           heap_init_size);
    heap = skHeapCreate2(&compare2, heap_init_size, sizeof(int),
                         NULL, &cmpfun_data);
    if (NULL == heap) {
        printf("Cannot create heap\n");
        exit(EXIT_FAILURE);
    }
    printf("OK\n");

#define REPEATS  4

    for (j = 0; j < REPEATS; ++j) {
        printf("\n** Inserting %d entries...",
               (int)(sizeof(data)/sizeof(data[0])));
        for (i = 0, iptr = data;
             i < (int)(sizeof(data)/sizeof(data[0]));
             ++i, ++iptr)
        {
            status = skHeapInsert(heap, (skheapnode_t)iptr);
            if (SKHEAP_OK == status) {
                /* okay */
            } else if (SKHEAP_ERR_FULL != status) {
                printf("NOPE. Got wierd error status %d\n", status);
            } else {
                printf("NOPE. Heap full.  Contains %d entries\n",
                       skHeapGetCapacity(heap));
            }
        }
        printf("OK\n");
        printf("heap %d/%d\n",
               skHeapGetNumberEntries(heap),
               skHeapGetCapacity(heap));
    }

    skHeapSortEntries(heap);

    printf("\n** Removing data from the heap...");
    j = skHeapGetNumberEntries(heap);
    i = 0;
    while (SKHEAP_OK == skHeapExtractTop(heap, (skheapnode_t)top_value)) {
        /* printf("%d\n", top_value[0]); */
        ++i;
    }
    printf("got %d entries\n", i);

    if (i != j) {
        printf("error extracting from heap: expected %d; got %d\n", j, i);
    }
    i = skHeapGetNumberEntries(heap);
    if (i != 0) {
        printf("error in heap contents: expected 0; got %d\n", i);
    }

    skHeapFree(heap);

    exit(0);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
