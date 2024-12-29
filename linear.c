#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

#ifndef DEFAULT_ALIGNMENT
#define DEFAULT_ALIGHMENT (2*sizeof(void*))
#endif

// every memory access must be a multiple of some power of two
bool 
is_power_of_two(uintptr_t ptr) {
    return (ptr & (ptr-1)) == 0;
}

uintptr_t 
align_foward(uintptr_t ptr, size_t align) {
    uintptr_t a, modulo;
    a = (uintptr_t) align;

    assert(is_power_of_two(a));

    // since a is a power of two, this does the same as % but does no require costly division
    modulo = ptr & (a-1); 

    if (modulo != 0) {
        ptr += a - modulo;
    }
    return ptr;
}

typedef struct {
    uint8_t *buffer;
    size_t buffer_len;
    size_t prev_offset;
    size_t curr_offset;
} Arena;

void* 
arena_alloc_align(Arena *a, size_t size, size_t align) {
    // first align pointer for next allocation, the get relative offset
    uintptr_t curr_ptr = (uintptr_t)a->buffer + (uintptr_t)a->curr_offset;
    uintptr_t rel_offset = align_foward(curr_ptr, align) - (uintptr_t)a->buffer;

    if (rel_offset + size <= a->buffer_len) {
        void* ptr = &(a->buffer[rel_offset]);
        a->prev_offset = rel_offset;
        a->curr_offset = rel_offset+size;

        memset(ptr, 0, size); // give clean memory
        return ptr;
    }

    return NULL;
}

void*
arena_alloc(Arena *a, size_t size) {
    return arena_alloc_align(a, size, DEFAULT_ALIGHMENT);
}

void*
arena_resize_align(Arena *a, void *old_memory, size_t old_size, size_t new_size, size_t align) {
    uint8_t* old_mem = (uint8_t*) old_memory;

    assert(is_power_of_two(align));

    if (old_mem == NULL || old_size == 0) {
        // just alloc again with new size
        return arena_alloc_align(a, new_size, align);
    } else if (a->buffer <= old_mem && old_mem < a->buffer + a->buffer_len) {
        // if old memory fall within the bounds of the arena buffer
        if (a->buffer + a->prev_offset == old_mem) {
            // old_mem concides with latest allocation, so just resize the allocation
            size_t new_offset = a->prev_offset + new_size;
            if (new_offset > a->buffer_len) {
                return NULL; // asking too big of a resize, allocation failed
            }
            a->curr_offset = new_offset;
            // this could be a bigger alloc or a smaller alloc, so clean memory if needed
            if (new_size > old_size) {
                // clear added memory to avoid giving out trash
                memset(&(a->buffer[a->curr_offset]), 0, new_size - old_size);
            }
            return old_mem;
        } else {
            // old_mem was not the latest allocation
            // do a whole new allocation, then copy the data in
            void *new_mem = arena_alloc_align(a, new_size, align);
            size_t copy_size = old_size < new_size ? old_size : new_size;
            memmove(new_mem, old_mem, copy_size);
            return new_mem;
        }
    } else {
        // if old memory is outside the bounds of the arena buffer
        assert(0 && "Memory out of bounds in arena buffer");
        return NULL;
    }
}

void*
arena_resize(Arena *a, void *old_mem, size_t old_size, size_t new_size) {
    return arena_resize_align(a, old_mem, old_size, new_size, DEFAULT_ALIGHMENT);
}

void
arena_free_all(Arena *a) {
    a->curr_offset = 0;
    a->prev_offset = 0;
}

void
arena_init(Arena *a, void *buffer, size_t buffer_len) {
    a->buffer = (uint8_t*)buffer;
    a->buffer_len = buffer_len;
    a->curr_offset = 0;
    a->prev_offset = 0;
}

int
main() {
    uint8_t* backing_buffer[1024] = {0};
    Arena a = {0};
    arena_init(&a, backing_buffer, 1024);
    size_t aligned_unit = DEFAULT_ALIGHMENT; // should be 16

    // verify basic allocation and resize
    uintptr_t a1 = (uintptr_t)arena_alloc(&a, aligned_unit*5);
    assert((void*)a1 != NULL);
    assert(a1 == (uintptr_t)a.buffer);

    uintptr_t a2 = (uintptr_t)arena_resize(&a, (void*)a1, aligned_unit*5, aligned_unit*10);
    assert((a2 == a1));
    assert(a.curr_offset == aligned_unit*10);

    uintptr_t a22 = (uintptr_t)arena_resize(&a, (void*)a1, aligned_unit*10, aligned_unit*9);
    assert((a2 == a22));
    assert(a.curr_offset == aligned_unit*9);
    
    // verify misaligned behavior
    arena_free_all(&a);
    uintptr_t a3 = (uintptr_t)arena_alloc(&a, (aligned_unit*5)+1);
    assert((void*)a3 != NULL);

    uintptr_t a4 = (uintptr_t)arena_resize(&a, (void*)a3, (aligned_unit*5)+1, aligned_unit*5);
    assert((a4 == a3));
    assert(a.curr_offset == aligned_unit*5);

    // verify multiple allocation
    arena_free_all(&a);
    uintptr_t a5 = (uintptr_t)arena_alloc(&a, (aligned_unit*5)+1);
    uintptr_t a6 = (uintptr_t)arena_alloc(&a, (aligned_unit*5)+1);
    assert(a6-a5 == aligned_unit*6);

    uintptr_t a7 = (uintptr_t)arena_alloc(&a, (aligned_unit*5)+1);
    assert(a.curr_offset == aligned_unit*17+1);

    // verify over allocation
    arena_free_all(&a);
    void* a8 = arena_alloc(&a, 1025);
    assert(NULL == a8);

    void* a9 = arena_alloc(&a, 1024);
    assert(NULL != (void*)a9);

    void* a10 =  arena_resize(&a, (void*)a9, 1024, 1025);
    assert(NULL == a10);

    // verify data copy when resizing
    arena_free_all(&a);
    void* a11 = arena_alloc(&a, 10);
    strcpy_s(a11, 10, "123456789");
    void* a12 = arena_alloc(&a, 1);
    void* a13 = arena_resize(&a, a11, 10, 8);
    assert(0 == memcmp(a13, a11, 8));
    assert(a11 != a12);
    assert(0 == memcmp(a11, "123456789", 10));
}



 