#include "GatewayHistoryHost.h"
#include "ServedValues.h"

namespace Jde::Opc::Gateway{
	GatewayHistoryHost::GatewayHistoryHost( sp<Opc::Hist::Historian> historian, sp<ServedValues> values )ι:
		_historian{ move(historian) },
		_values{ move(values) }
	{}

	α GatewayHistoryHost::Find( const UA_NodeId& node )Ι->optional<Node>{ return _values->History( node ); }
	α GatewayHistoryHost::ArchiveStarts()Ι->vector<ArchiveStart>{ return _values->ArchiveStarts(); }
	α GatewayHistoryHost::Remap( const UA_NodeId& node, std::span<UA_DataValue> values )ι->void{ _values->Remap( node, values ); }
	α GatewayHistoryHost::Unmap( const UA_NodeId& node, UA_Variant& value )ι->uint{ return _values->Unmap( node, value ); }
}