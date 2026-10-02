#if _MSC_VER
	#ifndef __INTELLISENSE__
		#include <windows.h>
		#include <sqltypes.h>
		#include <sql.h>
		#include <sqlext.h>
	#endif
#else
	#include <sqltypes.h>
	#include <sql.h>
	#include <sqlext.h>
#endif
#pragma warning( disable : 4245)
#pragma warning( default : 4245)
#include <boost/noncopyable.hpp>
#include <boost/system/error_code.hpp>
#include <jde/fwk.h>
#ifndef __INTELLISENSE__
	#include <spdlog/spdlog.h>
	#include <spdlog/sinks/basic_file_sink.h>
	#include <spdlog/fmt/ostr.h>
#endif
#include <jde/fwk/chrono.h>
