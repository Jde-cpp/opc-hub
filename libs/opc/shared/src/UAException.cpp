#include <jde/opc/UAException.h>

namespace Jde::Opc{
	α UAException::Throw()->void{ throw move(*this); }
}