#include <jde/web/server/RestException.h>

namespace Jde::Web::Server{
	α RestException::Throw()->void{ throw move(*this); }
}