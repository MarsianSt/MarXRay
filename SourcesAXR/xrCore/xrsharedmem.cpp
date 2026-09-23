#include "stdafx.h"
#pragma hdrstop

using namespace std;

XRCORE_API	smem_container*	g_pSharedMemoryContainer	= NULL;

smem_value*			smem_container::dock			(u32 dwCRC, u32 dwLength, void* ptr)
{
	VERIFY						(dwCRC && dwLength && ptr);

	const u32					bid			= dwCRC % bucket_count;
	cdb&						bucket		= container[bid];
	xrCriticalSection&			lk			= locks[bid];

	lk.Enter					();
	smem_value*		result		= 0;

	// search a place to insert
	u8				storage		[4*sizeof(u32)];
	smem_value*		value		= (smem_value*)storage;
	value->dwReference			= 0;
	value->dwCRC				= dwCRC;
	value->dwLength				= dwLength;
	cdb::iterator	it			= std::lower_bound	(bucket.begin(),bucket.end(),value,smem_search);
	cdb::iterator	saved_place	= it;
	if (bucket.end() != it)	{
		// supposedly found
		for (;;	it++)	{
			if (it==bucket.end())				break;
			if ((*it)->dwCRC	!= dwCRC)		break;
			if ((*it)->dwLength != dwLength)	break;
			if (0==memcmp((*it)->value,ptr,dwLength))
			{
				// really found
				result			= *it;
				break;
			}
		}
	}

	// if not found - create new entry
	if (0==result)
	{
		result					= (smem_value*)	Memory.mem_alloc	(4*sizeof(u32) + dwLength
#ifdef DEBUG_MEMORY_NAME
			, "storage: smem"
#endif // DEBUG_MEMORY_NAME
			);
		result->dwReference		= 0;
		result->dwCRC			= dwCRC;
		result->dwLength		= dwLength;
		CopyMemory			(result->value,ptr,dwLength);
		bucket.insert		(saved_place,result);
	}

	// exit
	lk.Leave					();
	return						result;
}

void				smem_container::clean			()
{
	for (u32 b=0; b<bucket_count; ++b)	{
		locks[b].Enter	();
		cdb&			bucket	= container[b];
		cdb::iterator	it		= bucket.begin	();
		cdb::iterator	end		= bucket.end		();
		for (; it!=end; it++)	if (0==(*it)->dwReference)	xr_free	(*it);
		bucket.erase	(remove(bucket.begin(),bucket.end(),(smem_value*)0),bucket.end());
		if (bucket.empty())	bucket.clear	();
		locks[b].Leave	();
	}
}

void				smem_container::dump			()
{
	FILE* F			= fopen("x:\\$smem_dump$.txt","w");
	for (u32 b=0; b<bucket_count; ++b)	{
		locks[b].Enter	();
		cdb&			bucket	= container[b];
		cdb::iterator	it		= bucket.begin	();
		cdb::iterator	end		= bucket.end		();
		for (; it!=end; it++)
			fprintf		(F,"%4d : crc[%6x], %d bytes\n",(*it)->dwReference,(*it)->dwCRC,(*it)->dwLength);
		locks[b].Leave	();
	}
	fclose			(F);
}

u32					smem_container::stat_economy	()
{
	s64				counter	= 0;
	counter			-= sizeof(*this);
	counter			-= sizeof(cdb::allocator_type)*bucket_count;
	const int		node_size = 20;
	for (u32 b=0; b<bucket_count; ++b)	{
		locks[b].Enter	();
		cdb&			bucket	= container[b];
		cdb::iterator	it		= bucket.begin	();
		cdb::iterator	end		= bucket.end		();
		for (; it!=end; it++)	{
			counter		-= 16;
			counter		-= node_size;
			counter		+= s64((s64((*it)->dwReference) - 1)*s64((*it)->dwLength));
		}
		locks[b].Leave	();
	}

	return			u32(s64(counter)/s64(1024));
}

smem_container::~smem_container	()
{
	clean			();
}
