// xrStatusConsole.cpp
#include "stdafx.h"
#include "xrStatusConsole.h"

// Global log history pointer consumed by engine consoles and game commands.
// Owned here by the status console (xrCore log-receiver component).
xr_vector<shared_str>* LogFile = nullptr;

CXRStatusConsole& CXRStatusConsole::instance()
{
	static CXRStatusConsole s;
	LogFile = &s.m_lines;
	return s;
}

void CXRStatusConsole::add_line(const char* line)
{
	if (!line || !line[0])
		return;
	m_lines.push_back(shared_str(line));
}

LogCallback CXRStatusConsole::set_forward(LogCallback cb)
{
	LogCallback prev = m_forward;
	m_forward = cb;
	return prev;
}
