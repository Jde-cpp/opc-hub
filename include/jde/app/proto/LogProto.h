#pragma once
#include <jde/app/usings.h>

namespace Jde::App::LogProto{
	α LogEntryClient( Logging::Entry&& m )ι->Log::Proto::LogEntryClient;
	struct SourceFields final{ bool File{true}; bool Function{true}; };//File carries the line:  a line without its file says nothing.
	α LogEntryFile( const Logging::Entry& m, App::ProgramPK appPK=0, App::ProgInstPK instancePK=0, SourceFields source={} )ι->Log::Proto::LogEntryFile;//non-zero pks: forwarded from that instance.
	α FromLogEntry( Log::Proto::LogEntryClient&& m )ι->Logging::Entry;
	α ToString( uuid id, string&& value )ι->Log::Proto::String;
	//Debugger-facing; see LogProto.cpp for why these are unconditional.
	α DebugString( const Log::Proto::FileEntry& f )ι->string;
	α DebugString( const Log::Proto::LogEntryFile& f )ι->string;

}