#pragma once
#include <jde/opc/uatypes/Owned.h>

namespace Jde::Opc::Gateway{
	struct MonitoredItemCreateResult : Owned<UA_MonitoredItemCreateResult, UA_TYPES_MONITOREDITEMCREATERESULT>{
		MonitoredItemCreateResult()ι=default;
		MonitoredItemCreateResult( UA_UInt32 id )ι{ monitoredItemId=id; }
		MonitoredItemCreateResult( const UA_MonitoredItemCreateResult& x )ι:Owned{ x }{}
	};

	Ξ operator==( const MonitoredItemCreateResult& x, const MonitoredItemCreateResult& y )ι->bool{ return x.monitoredItemId==y.monitoredItemId; }
}