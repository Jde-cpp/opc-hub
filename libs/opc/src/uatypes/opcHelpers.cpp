#include <jde/opc/uatypes/opcHelpers.h>
#include <open62541/server.h>
#include <jde/opc/UAException.h>

namespace Jde{
	α Opc::NamespaceIndex( UA_Server& server, sv uri, SL sl )ε->NsIndex{
		size_t index{};
		if( const auto sc = UA_Server_getNamespaceByName( &server, ToUV(uri), &index ); sc )
			throw UAException{ sc, Ƒ("namespace '{}' is not loaded", uri), {}, sl };
		return (NsIndex)index;
	}
}