#include <jde/opc/uatypes/UAPtr.h>
#include <open62541/server.h>

namespace Jde::Opc{
	α ClientDelete::operator()( UA_Client* p )Ι->void{ UA_Client_delete( p ); }
	α ServerDelete::operator()( UA_Server* p )Ι->void{ UA_Server_delete( p ); }
}