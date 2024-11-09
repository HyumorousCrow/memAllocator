#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

#define NEXT_FITx

#define WSIZE       4       
#define DSIZE       8       
#define CHUNKSIZE  (1<<12) 

#define MAX(x, y) ((x) > (y)? (x) : (y))  

#define PACK(size, alloc)  ((size) | (alloc)) 

#define GET(p)       (*(unsigned int *)(p))   
#define PUT(p, val)  (*(unsigned int *)(p) = (val))

#define GET_SIZE(p)  (GET(p) & ~0x7)               
#define GET_ALLOC(p) (GET(p) & 0x1)                    

#define HDRP(bp)       ((char *)(bp) - WSIZE)          
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 

#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) 

#define SUCC(bp) (*(void**)(bp))
#define PRED(bp) (*(void**)(bp + WSIZE))

#define MIN_BLOCK_SIZE (4 * WSIZE)
#define LIST_NUM 20

static char *heap_listp = 0; 
void *segFreeLists[LIST_NUM];

#ifdef NEXT_FIT
#endif

static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *coalesce(void *bp);
static void insert_node(void *bp, size_t size);
static void delete_node(void *bp);
static int get_list_index(size_t size);



int mm_init(void)
{
    int i;

    for (i = 0; i < LIST_NUM; i++) {
        segFreeLists[i] = NULL;
    }

    if ((long)(heap_listp = mem_sbrk(4 * WSIZE)) == -1)
        return -1;

    PUT(heap_listp, 0);                                 
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));      
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));      
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));          
    heap_listp += (2 * WSIZE);

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

void *mm_malloc(size_t size) {
    size_t asize;      
    size_t extendsize;
    void *bp = NULL;
    int index = 0;

    if (size == 0)
        return NULL;

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    for (index = get_list_index(asize); index < LIST_NUM; index++) {
        bp = segFreeLists[index];
        while (bp != NULL && (asize > GET_SIZE(HDRP(bp)))) {
            bp = SUCC(bp);
        }
        if (bp != NULL)
            break;
    }

    if (bp != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;

    place(bp, asize);
    return bp;
}
 

void mm_free(void *bp)
{
    if (bp == 0) 
        return;

    size_t size = GET_SIZE(HDRP(bp));
    if (heap_listp == 0){
        mm_init();
    }

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

static int get_list_index(size_t size) {
    int index = 0;
    size >>= 4; 
    while (size > 1 && index < LIST_NUM - 1) {
        size >>= 1;
        index++;
    }
    return index;
}


static void insert_node(void *bp, size_t size) {
    int index = get_list_index(size);
    void *curr = segFreeLists[index];

    SUCC(bp) = curr;
    if (curr != NULL) {
        PRED(curr) = bp;
    }

    PRED(bp) = NULL;
    segFreeLists[index] = bp;
}

static void delete_node(void *bp) {
    int index = get_list_index(GET_SIZE(HDRP(bp)));

    if (PRED(bp) != NULL) {
        SUCC(PRED(bp)) = SUCC(bp);
    } else {
        segFreeLists[index] = SUCC(bp);
    }
    if (SUCC(bp) != NULL) {
        PRED(SUCC(bp)) = PRED(bp);
    }
}


static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {            
    } else if (prev_alloc && !next_alloc) {  
        delete_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {   
        delete_node(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else {                                   
        delete_node(PREV_BLKP(bp));
        delete_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) +
                GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    insert_node(bp, size);

    return bp;
}


void *mm_realloc(void *ptr, size_t size)
{
    size_t oldsize;
    void *newptr;

    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    if(ptr == NULL) {
        return mm_malloc(size);
    }

    newptr = mm_malloc(size);

    if(!newptr) {
        return 0;
    }

    oldsize = GET_SIZE(HDRP(ptr));
    if(size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    mm_free(ptr);

    return newptr;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));         
    PUT(FTRP(bp), PACK(size, 0));         
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); 

    return coalesce(bp);
}


static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    size_t remainder = csize - asize;

    delete_node(bp); 

    if (remainder >= MIN_BLOCK_SIZE) {
        PUT(HDRP(bp), PACK(asize, 1)); 
        PUT(FTRP(bp), PACK(asize, 1)); 
        void *next_bp = NEXT_BLKP(bp);
        PUT(HDRP(next_bp), PACK(remainder, 0)); 
        PUT(FTRP(next_bp), PACK(remainder, 0)); 
        insert_node(next_bp, remainder); 
    } else {
        PUT(HDRP(bp), PACK(csize, 1)); 
        PUT(FTRP(bp), PACK(csize, 1)); 
    }
}



