#include "stdafx.h"
#pragma hdrstop

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"

CSoundRender_Source::CSoundRender_Source	()
{
	m_fMinDist		= 1.f;
	m_fMaxDist		= 300.f;
	m_fMaxAIDist	= 300.f;
	m_fBaseVolume	= 1.f;
	m_uGameType		= 0;
	fname			= 0;
    CAT.table		= 0;
	CAT.size		= 0;
}

CSoundRender_Source::~CSoundRender_Source	()
{
	unload			();
}

bool ov_error(int res)
{
    switch (res){
    case 0:				return false;
// info
    case OV_HOLE:		LogInfo("Vorbisfile encoutered missing or corrupt data in the bitstream. Recovery is normally automatic and this return code is for informational purposes only."); return true;
    case OV_EBADLINK:	LogInfo("The given link exists in the Vorbis data stream, but is not decipherable due to garbacge or corruption."); return true;
// error
    case OV_FALSE: 		LogInfo("Not true, or no data available"); return false;
    case OV_EREAD:		LogInfo("Read error while fetching compressed data for decode"); return false;
    case OV_EFAULT:		LogInfo("Internal inconsistency in decode state. Continuing is likely not possible."); return false;
    case OV_EIMPL:		LogInfo("Feature not implemented"); return false; 
    case OV_EINVAL:		LogInfo("Either an invalid argument, or incompletely initialized argument passed to libvorbisfile call"); return false;
    case OV_ENOTVORBIS:	LogInfo("The given file/data was not recognized as Ogg Vorbis data."); return false;
    case OV_EBADHEADER:	LogInfo("The file/data is apparently an Ogg Vorbis stream, but contains a corrupted or undecipherable header."); return false;
    case OV_EVERSION:	LogInfo("The bitstream format revision of the given stream is not supported."); return false;
    case OV_ENOSEEK:	LogInfo("The given stream is not seekable"); return false;
    }
    return false;

}

void CSoundRender_Source::i_decompress_fr(OggVorbis_File* ovf, char* _dest, u32 left)
{
	// vars
	int			current_section;
	long		TotalRet = 0, ret;

	// Read loop
	while (TotalRet < (long)left)
	{
		ret = ov_read(ovf, /*PCM*/ _dest + TotalRet, left - TotalRet, 0, 2, 1, &current_section);

		// if end of file or read limit exceeded
		if (ret == 0) break;
		else if (ret < 0) 		// Error in bitstream
		{
			//
		}
		else
		{
			TotalRet += ret;
		}
	}
}