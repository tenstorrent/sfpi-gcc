#ifndef MALLINFO2_COMPAT_H
#define MALLINFO2_COMPAT_H

#include <stddef.h>  /* for size_t */

/* Same layout as glibc's struct mallinfo2 */
struct mallinfo2 {
    size_t arena;    /* Non-mmapped space allocated (bytes) */
    size_t ordblks;  /* Number of free chunks */
    size_t smblks;   /* Number of free fastbin blocks */
    size_t hblks;    /* Number of mmap regions */
    size_t hblkhd;   /* Space in mmap regions (bytes) */
    size_t usmblks;  /* Maximum total allocated space */
    size_t fsmblks;  /* Space in free fastbin blocks (bytes) */
    size_t uordblks; /* Total allocated space (bytes) */
    size_t fordblks; /* Total free space (bytes) */
    size_t keepcost; /* Top-most, releasable space (bytes) */
};

/* glibc-compatible function prototype */
#ifdef __cplusplus
extern "C" {
#endif

struct mallinfo2 mallinfo2(void);

#ifdef __cplusplus
}
#endif

#endif /* MALLINFO2_COMPAT_H */
