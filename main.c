#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

typedef char ALIGH[16]; /* ALIGH is 16 bits */

union header {
    struct {
        size_t size;
        unsigned is_free;   /* shorthand for unsigned int */
        union header *next; /* linked list for our heap */
    } s;
    ALIGH stub;
};

typedef union header header_t;  /* so header_t will have a fixed 16 bytes size */

/* a head and tail pointer to track our heap */
header_t *head, *tail;

/* set a mutex lock to prevent concurrently accessing memory */
pthread_mutex_t global_malloc_lock;

header_t *get_free_block(size_t size);

/* sbrk(0) => return the current address of program break
 * sbrk(x) => increase brk by x bytes
 * sbrk(-x) => decrease brk by x bytes
 * if sbrk() fails, it will return (void *) -1 / 0xFFFFFFFF
 * */
void *malloc(size_t size) {
    if (!size)  /* if the requested size is zero, return */
        return NULL;
    size_t total_size;  /* header size + block size */
    void *block;
    header_t *header;

    pthread_mutex_lock(&global_malloc_lock);    /* acquire the lock */
    header = get_free_block(size);  /* check if there have available free block that has a size of bigger than 'size' */
    if (header) {   /* exists */
        header -> s.is_free = 0;    /* this block is not free */
        pthread_mutex_unlock(&global_malloc_lock);  /* release the lock */
        return (void *)(header + 1);    /* return the address of the block instead of the header */
    }

    total_size = sizeof(header_t) + size;   /* not exists, we have to create one */
    block = sbrk(total_size);
    if (block == (void *) -1) { /* malloc failed */
        pthread_mutex_unlock(&global_malloc_lock);  /* release the lock */
        return NULL;
    }

    header = block;
    header -> s.size = size;
    header -> s.is_free = 0;
    header -> s.next = NULL;

    if (!head)  /* if the head pointer of the heap not exists, make header be the head */
        head = header;
    if (tail)   /* if the tail pointer of the heap exists, */
        tail -> s.next = header;
    tail = header; /* make this header be the tail (or the new tail) */
    pthread_mutex_unlock(&global_malloc_lock);  /* release the lock */
    return (void *)(header + 1);    /* return the address of the block instead of the header */
}

header_t *get_free_block(size_t size) {
    header_t *curr = head;
    while (curr) {
        if (curr -> s.is_free == 1 && curr -> s.size >= size)
            return curr;
        curr = curr -> s.next;
    }
    return NULL;
}

/* if the block-to-be-freed is at the end of the heap, then we return it to the OS.
 * otherwise we simply mark it as 'free'
 * */
void free(void *block){
    header_t *header, *tmp;
    void *programbreak;

    if (!block) /* if this block not exists */
        return;

    pthread_mutex_lock(&global_malloc_lock);
    header = (header_t *)block - 1; /* header address */

    programbreak = sbrk(0); /* get current brk location */

    if ((char *)block + header -> s.size == programbreak) { /* this is the end of the heap */
        if (head == tail) { /* currently there is only one block in the heap, we will erase it */
            head = tail = NULL;
        } else {    /* otherwise we will remove the tail */
            tmp = head;
            while (tmp) {
                if (tmp -> s.next == tail) {
                    tmp -> s.next = NULL;
                    tail = tmp;
                }
                tmp = tmp -> s.next;
            }
        }
        /* release the space */
        sbrk(0 - sizeof(header_t) - header -> s.size);
        pthread_mutex_unlock(&global_malloc_lock);
        return;
    }

    /* if this block is in the middle of our heap, we mark it as 'free' */
    header -> s.is_free = 1;
    pthread_mutex_unlock(&global_malloc_lock);
}


/* calloc allocates 'num' elements with size of 'nsize'.
 * */
void *calloc(size_t num, size_t nsize) {
    if (!num || !nsize)
        return NULL;    /* num and nsize must be valid */

    size_t size;
    void *block;
    size = num * nsize;
    if (nsize != size / num)
        return NULL;    /* multiply overflow */
    block = malloc(size);
    if (!block)
        return NULL;    /* malloc failed */
    memset(block, 0, size); /* reset all value in this block */
    return block;
}

/* change the given block's size
 * */
void *realloc(void *block, size_t size){
    if (!block || !size)    /* block and size must be valid */
        return NULL;

    header_t *header;
    void *ret;
    header = (header_t *)block - 1;
    if (header -> s.size >= size)   /* if the original block already has enough space for the data */
        return block;
    ret = malloc(size); /* allocate a new space */
    if (ret) {
        memcpy(ret, block, header -> s.size);   /* move data in block to ret */
        free(block);    /* free original block */
    }
    return ret;
}
