#include "mallinfo2_compat.h"

struct mallinfo2 mallinfo2(void) {
    struct mallinfo2 mi = {0}; // zero all fields
    return mi;
}