#pragma once
#include <boost/mysql/ssl_mode.hpp>
#include <jde/fwk/exceptions/Exception.h>

namespace Jde::DB::MySql{
	//#233: the server block's `ssl` key.  `enable` is the default - TLS when the server offers it, plaintext when it does not.
	Ξ ToSslMode( sv mode, SRCE )ε->boost::mysql::ssl_mode{
		using boost::mysql::ssl_mode;
		if( mode=="enable" ) return ssl_mode::enable;
		if( mode=="require" ) return ssl_mode::require;
		if( mode=="disable" ) return ssl_mode::disable;
		throw Exception{ sl, {ELogLevel::Error, ELogTags::DBDriver}, "dbServers ssl:'{}' - expected disable, enable or require.", mode };
	}
	Ξ SslModeName( boost::mysql::ssl_mode mode )ι->sv{
		using boost::mysql::ssl_mode;
		return mode==ssl_mode::disable ? "disable" : mode==ssl_mode::require ? "require" : "enable";
	}
}
