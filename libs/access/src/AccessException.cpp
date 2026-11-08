#include <jde/access/AccessException.h>

namespace Jde::Access{
	//The base's log-once guard, honoured:  Move() hands the args to the new object and marks the source logged, so without it
	//the source's destructor logged the format with its `{}` unsubstituted, next to the real entry (access-review3 #28).
	α AccessException::Log()Ι->void{
		if( _logged || Level()==ELogLevel::NoLog || Process::LoggersGone() )
			return;
		_logged = true;
		if( auto sv = Format(); sv.size() )
			Logging::Log( Logging::Entry{_sl, Level(), Tags, Executer, FormatName(), _args} );
		else
			Logging::Log( Logging::Entry{_sl, Level(), Tags, Executer, string{what()}} );
	}
}