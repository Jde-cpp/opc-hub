#pragma once

namespace Jde::Windows::Service{
	α ReportStatus(unsigned long dwCurrentState, unsigned long dwWin32ExitCode, unsigned long dwWaitHint )ι->void;
	α ReportEvent( sv function )ι->void;
	α Main(unsigned long dwArgc, LPTSTR *lpszArgv )ι->void;
	//Process::AsService:  connect to the SCM before the startup runs, and report start pending while it does.
	α Connect()ι->bool;
	//Process::Pause:  the startup finished - report running and run the loop, until the service stops.
	α Started()ι->void;
}