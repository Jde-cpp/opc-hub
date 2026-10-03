#include <jde/fwk/macros.h>
#include <jde/fwk/process/cpu.h>

//Process::CheckCpu as a process of its own, for Setup (OpcHubSetup.nsi, .onInit):  on Windows the products' own call comes
//after their dlls' initializers.  1 with the missing features named on stderr, else 0.  target, as CheckCpu has:  cpuFlags
//compile main too.
[[gnu::target("arch=x86-64")]] α main()->int{
	return Jde::Process::CheckCpu() ? 0 : 1;
}