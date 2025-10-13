/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XSC tracepoints
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM xsc

#if !defined(_TRACE_XSC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XSC_H

#include <linux/tracepoint.h>

TRACE_EVENT(xsc_submit,
	TP_PROTO(void *ctx, u8 opcode, u64 user_data),

	TP_ARGS(ctx, opcode, user_data),

	TP_STRUCT__entry(
		__field(void *,		ctx)
		__field(u8,		opcode)
		__field(u64,		user_data)
	),

	TP_fast_assign(
		__entry->ctx		= ctx;
		__entry->opcode		= opcode;
		__entry->user_data	= user_data;
	),

	TP_printk("ctx %p, op %u, user_data 0x%llx",
		  __entry->ctx, __entry->opcode, __entry->user_data)
);

TRACE_EVENT(xsc_dispatch,
	TP_PROTO(void *ctx, u8 opcode, int cpu),

	TP_ARGS(ctx, opcode, cpu),

	TP_STRUCT__entry(
		__field(void *,		ctx)
		__field(u8,		opcode)
		__field(int,		cpu)
	),

	TP_fast_assign(
		__entry->ctx		= ctx;
		__entry->opcode		= opcode;
		__entry->cpu		= cpu;
	),

	TP_printk("ctx %p, op %u, cpu %d",
		  __entry->ctx, __entry->opcode, __entry->cpu)
);

TRACE_EVENT(xsc_complete,
	TP_PROTO(void *ctx, u64 user_data, s32 res),

	TP_ARGS(ctx, user_data, res),

	TP_STRUCT__entry(
		__field(void *,		ctx)
		__field(u64,		user_data)
		__field(s32,		res)
	),

	TP_fast_assign(
		__entry->ctx		= ctx;
		__entry->user_data	= user_data;
		__entry->res		= res;
	),

	TP_printk("ctx %p, user_data 0x%llx, res %d",
		  __entry->ctx, __entry->user_data, __entry->res)
);

TRACE_EVENT(xsc_drop,
	TP_PROTO(void *ctx, u8 opcode, int reason),

	TP_ARGS(ctx, opcode, reason),

	TP_STRUCT__entry(
		__field(void *,		ctx)
		__field(u8,		opcode)
		__field(int,		reason)
	),

	TP_fast_assign(
		__entry->ctx		= ctx;
		__entry->opcode		= opcode;
		__entry->reason		= reason;
	),

	TP_printk("ctx %p, op %u, reason %d",
		  __entry->ctx, __entry->opcode, __entry->reason)
);

TRACE_EVENT(xsc_credit,
	TP_PROTO(void *ctx, int credits, int available),

	TP_ARGS(ctx, credits, available),

	TP_STRUCT__entry(
		__field(void *,		ctx)
		__field(int,		credits)
		__field(int,		available)
	),

	TP_fast_assign(
		__entry->ctx		= ctx;
		__entry->credits	= credits;
		__entry->available	= available;
	),

	TP_printk("ctx %p, credits %d, available %d",
		  __entry->ctx, __entry->credits, __entry->available)
);

#endif /* _TRACE_XSC_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE xsc_trace
#include <trace/define_trace.h>
