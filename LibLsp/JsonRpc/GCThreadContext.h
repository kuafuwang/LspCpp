#pragma once

#if defined(GC_USAGE)
#define GC_THREADS
#include <gc.h>
#endif

class GCThreadContext
{
public:
    GCThreadContext();
    ~GCThreadContext();

private:
#if defined(GC_USAGE)
    GC_stack_base gsb;
#endif

};