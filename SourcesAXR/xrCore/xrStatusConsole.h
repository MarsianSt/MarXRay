// xrStatusConsole.h
//
// Status console: the xrCore log-receiver component.
//
// Owns the authoritative in-memory log history (the global LogFile vector)
// that the engine consoles (CConsole/CTextConsole) and game 'flush' command
// read. The async logger feeds every log line into this component, so all log
// reception for consoles is centralized here in xrCore. The engine only
// forwards/reads strings and never touches the logger buffer directly.
#pragma once

#include "xrAsyncLogger.h"

class XRCORE_API CXRStatusConsole
{
public:
	static	CXRStatusConsole&	instance			( );

	// Receives a raw log line from the async logger (called on writer thread).
	void	add_line			( const char* line );

	// Secondary listener for log lines (e.g. engine red-text / stats).
	// Returns the previously registered listener.
	LogCallback	set_forward		( LogCallback cb );

	// Accessors used by engine consoles and game commands.
	u32			size			( ) const						{ return (u32)m_lines.size(); }
	LPCSTR		at				( u32 idx ) const				{ return idx < m_lines.size() ? m_lines[idx].c_str() : 0; }
	void		clear			( )								{ m_lines.clear(); }
	xr_vector<shared_str>* get_log_file( )						{ return &m_lines; }

private:
	CXRStatusConsole			( ) = default;
	~CXRStatusConsole			( ) = default;
	CXRStatusConsole			( const CXRStatusConsole& ) = delete;
	CXRStatusConsole& operator=	( const CXRStatusConsole& ) = delete;

	xr_vector<shared_str>	m_lines;
	LogCallback				m_forward = nullptr;
};
