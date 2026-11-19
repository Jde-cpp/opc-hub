#include <boost/asio/error.hpp>//MySqlException.h reaches for `mysql::` without including boost:  the suite that compiles this
#include <boost/mysql/client_errc.hpp>//file shares none of the driver's pc.h (tests/ExceptionTests.cpp).
#include <boost/mysql/common_server_errc.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/error_with_diagnostics.hpp>
#include <boost/mysql/mariadb_server_errc.hpp>
#include <boost/mysql/mysql_server_errc.hpp>
#include <jde/db/generators/Sql.h>
#include "MySqlException.h"

namespace Jde::DB::MySql{
	α MySqlException::Throw()->void{ throw move(*this); }
}