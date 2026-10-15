#pragma once

namespace Jde::Access{
	struct AccessException : Exception{
		AccessException( AccessException&& from )ι=default;
		//Forbidden by default:  every throw site is an authorization check against an already-authenticated executer, and the
		//web client treats a 401 as a stale credential - re-auth prompt, anonymous retry, logout (access-review3 #17).  Pass
		//Unauthorized explicitly for the one case that is about who the executer is rather than what they may do.
		template<class... Args> AccessException( SL sl, UserPK executer, ELogLevel level, fmt::format_string<Args...> m, Args&& ...args )ι;
		template<class... Args> AccessException( SL sl, UserPK executer, ELogLevel level, EHttpStatus status, fmt::format_string<Args...> m, Args&& ...args )ι;
		~AccessException(){ Log(); SetLevel( ELogLevel::NoLog ); }
		α Log()Ι->void override;
		α Move()ι->up<Exception> override{ return mu<AccessException>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
		UserPK Executer;
	};
	template<class... Args> AccessException::AccessException( SL sl, UserPK executer, ELogLevel level, fmt::format_string<Args...> m, Args&& ...args )ι:
		AccessException{ sl, executer, level, EHttpStatus::Forbidden, m, FWD(args)... }
	{}
	template<class... Args> AccessException::AccessException( SL sl, UserPK executer, ELogLevel level, EHttpStatus status, fmt::format_string<Args...> m, Args&& ...args )ι:
		Exception{ sl, {level, ELogTags::Access | ELogTags::Exception, 0, status}, m, FWD(args)... },
		Executer{ executer }
	{}
}