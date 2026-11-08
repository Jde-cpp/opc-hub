#include <jde/opc/uatypes/BrowseName.h>
#include <jde/opc/uatypes/UAString.h>

#define let const auto
namespace Jde::Opc{
	BrowseName::BrowseName( sv fqBrowseName, NsIndex defaultNs )ε{
		let pos = fqBrowseName.find('~');
		let ns = pos==string::npos ? optional<NsIndex>{ defaultNs } : Str::TryTo<NsIndex>( fqBrowseName.substr(0,pos) );
		THROW_IF( !ns, "Invalid namespace index in browse name '{}'.", fqBrowseName );
		namespaceIndex = *ns;
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