#include "stdafx.h"
#pragma hdrstop

#include "xrMemory.h"
#include "xrMemory_pure.h"

#ifndef	__BORLANDC__

#ifndef DEBUG_MEMORY_MANAGER
#	define	debug_mode 0
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG_MEMORY_MANAGER
	XRCORE_API void*	g_globalCheckAddr = NULL;
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG_MEMORY_MANAGER
	extern void save_stack_trace	();
#endif // DEBUG_MEMORY_MANAGER

#if defined(PURE_ALLOC)
bool	g_use_pure_alloc		= false;
#endif // PURE_ALLOC

namespace
{
	constexpr	u32		stat_calls_batch	= 256;

	thread_local	u32	t_stat_calls_pending	= 0;

	struct			stat_calls_flusher
	{
		~stat_calls_flusher	()
		{
			if (t_stat_calls_pending)
			{
				Memory.stat_calls.fetch_add	(t_stat_calls_pending, std::memory_order_relaxed);
				t_stat_calls_pending		= 0;
			}
		}
	};
	thread_local	stat_calls_flusher	t_stat_calls_flusher;

	inline	void	stat_calls_inc		()
	{
		if (++t_stat_calls_pending >= stat_calls_batch)
		{
			Memory.stat_calls.fetch_add	(t_stat_calls_pending, std::memory_order_relaxed);
			t_stat_calls_pending		= 0;
		}
	}
}

void*	xrMemory::mem_alloc		(size_t size
#	ifdef DEBUG_MEMORY_NAME
								 , const char* _name
#	endif // DEBUG_MEMORY_NAME
								 )
{
	stat_calls_inc();

	return malloc(size);
}

void	xrMemory::mem_free		(void* P)
{
	stat_calls_inc();
	free(P);
}

extern BOOL	g_bDbgFillMemory	;

void*	xrMemory::mem_realloc	(void* P, size_t size
#ifdef DEBUG_MEMORY_NAME
								 , const char* _name
#endif // DEBUG_MEMORY_NAME
								 )
{
	stat_calls_inc();
	return realloc(P, size);
}

#endif // __BORLANDC__