#include <jde/web/client/http/ClientHttpResException.h>

namespace Jde::Web::Client{
	α ClientHttpResException::Throw()->void{ throw move(*this); }
}