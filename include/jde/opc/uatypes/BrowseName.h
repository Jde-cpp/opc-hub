#pragma once
#include "Owned.h"
#include "UAString.h"

namespace Jde::Opc{
	α ToJson( const UA_QualifiedName& qn )ι->jobject;
	struct BrowseName final : Owned<UA_QualifiedName, UA_TYPES_QUALIFIEDNAME>{
		BrowseName()ι=default;
		BrowseName( sv fqBrowseName, NsIndex defaultNs )ε;
		BrowseName( UA_QualifiedName&& qn )ι:Owned{ move(qn) }{}

		α ToJson()Ι->jobject{ return Opc::ToJson( *this ); }
		α ToString()Ι->string{ return serialize( ToJson() ); }
	};
	Ξ operator==( const BrowseName& x, const UA_QualifiedName& y )ι->bool{ return x.namespaceIndex==y.namespaceIndex && ToSV(x.name)==ToSV(y.name); }
	Ξ operator==( const BrowseName& x, const BrowseName& y )ι->bool{ return x==static_cast<const UA_QualifiedName&>(y); }
}