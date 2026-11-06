#pragma once
#include <jde/fwk/utils/Vector.h>
#include "ILogger.h"
#include "jde/fwk/usings.h"
#define Φ Γ auto

namespace Jde::Logging{
	Φ ClearMemory()ι->void;
	Φ Find( StringMd5 entryId )ι->vector<Logging::Entry>;
	Φ Find( function<bool(const Logging::Entry&)> f )ι->vector<Logging::Entry>;

	struct Γ MemoryLog final : ILogger{
		MemoryLog()ι:ILogger{ jobject{{"tags",jobject{{"default", "Trace"}}}} }{}
		α Shutdown( bool /*terminate*/, SL )ι->void override{ _entries.clear(); }
		α Write( const Record& r )ι->void override;
		α Write( ILogger& logger )ι->void;
		α Name()Ι->sv override{ return "MemoryLog"; }
		α SetMinLevel( ELogLevel /*level*/ )ι->void override{}
		α Clear()ι->void{ _entries.clear(); }
		α Find( StringMd5 id )ι->vector<Entry>;
		α Find( string text )ι->vector<Entry>; //the whole class is Γ now - a dllexport member of a dllexport class is an error.
		α Find( function<bool(const Entry&)> f )ι->vector<Entry>;
	private:
		Vector<Entry> _entries;
	};
}
#undef Φ