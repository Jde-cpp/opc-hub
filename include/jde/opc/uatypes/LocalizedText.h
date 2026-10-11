#pragma once
#include "Owned.h"
#include "UAString.h"

namespace Jde::Opc{
	Ξ ToJson( const UA_LocalizedText& lt )ι->jobject{
		jobject o;
		if( lt.text.length ){
			if( lt.locale.length )
				o["locale"] = Opc::ToString( lt.locale );
			o["text"] = Opc::ToString( lt.text );
		}
		return o;
	}
	struct LocalizedText final : Owned<UA_LocalizedText, UA_TYPES_LOCALIZEDTEXT>{
		LocalizedText()ι=default;
		LocalizedText( sv text, sv locale={} )ι:Owned{ UA_LocalizedText{AllocUAString(locale), AllocUAString(text)} }{}

		α ToJson()Ι->jobject{ return Opc::ToJson( *this ); }
	};
}