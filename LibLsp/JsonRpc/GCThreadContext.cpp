#include "GCThreadContext.h"
#include <iostream>

GCThreadContext::GCThreadContext()
{
#ifdef GC_USAGE
    GC_get_stack_base(&gsb);
    GC_register_my_thread(&gsb);
#endif
}

GCThreadContext::~GCThreadContext()
{
#ifdef GC_USAGE
    GC_unregister_my_thread();
#endif
}