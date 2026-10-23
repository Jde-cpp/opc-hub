#include <jde/opc/uatypes/BrowseName.h>
#include <jde/opc/uatypes/UAString.h>

namespace Jde::Opc{
	BrowseName::BrowseName( sv fqBrowseName, NsIndex defaultNs )ε{
		auto pos = fqBrowseName.find('~');
		namespaceIndex = pos==string::npos ? defaultNs : To<NsIndex>( fqBrowseName.substr(0,pos) );
		name = AllocUAString( pos==string::npos ? fqBrowseName : fqBrowseName.substr( pos+1 ) );
	}
	α ToJson( const UA_QualifiedName& qn )ι->jobject{
		jobject o;
		if( qn.name.length ){
			o["ns"] = qn.namespaceIndex;
			o["name"] = Opc::ToString( qn.name );
		}
		return o;
	}
}