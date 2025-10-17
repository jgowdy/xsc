# Remove CREATE_TRACE_POINTS and trace header
/^#define CREATE_TRACE_POINTS$/d
/^#include "xsc_trace.h"$/d

# Comment out trace calls
s/trace_xsc_complete(ctx, user_data, res);/\/\/ trace_xsc_complete(ctx, user_data, res);/
s/trace_xsc_dispatch(ctx, sqe->opcode, smp_processor_id());/\/\/ trace_xsc_dispatch(ctx, sqe->opcode, smp_processor_id());/
