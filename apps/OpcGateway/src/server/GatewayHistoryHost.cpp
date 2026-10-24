#include "GatewayHistoryHost.h"
#include "ServedValues.h"

#define let const auto
namespace Jde::Opc::Gateway{
	using Opc::Server::IHistoryHost;

	GatewayHistoryHost::GatewayHistoryHost( sp<Opc::Hist::Historian> historian, sp<ServedValues> values )ι:
		_historian{ move(historian) },
		_values{ move(values) }
	{}

	α GatewayHistoryHost::Find( const UA_NodeId& node )Ι->optional<Node>{
		rl _{ _mutex };
		auto p = _nodes.find( node );
		return p==_nodes.end() ? optional<Node>{} : p->second.History;
	}
	α GatewayHistoryHost::ArchiveStarts()Ι->vector<ArchiveStart>{
		rl _{ _mutex };
		vector<ArchiveStart> y;
		for( let& [_, served] : _nodes ){
			for( let& property : served.Starts )
				y.push_back( {property, served.History.Group} );
		}
		return y;
	}
	α GatewayHistoryHost::Remap( const UA_NodeId& node, UA_Variant& value )ι->void{ _values->Remap( node, value ); }
	α GatewayHistoryHost::Unmap( const UA_NodeId& node, UA_Variant& value )ι->void{ _values->Unmap( node, value ); }

	α GatewayHistoryHost::Add( NodeId node, sp<Opc::Hist::Group> group, Opc::Hist::NodeIndex index, vector<NodeId> starts )ι->vector<ArchiveStart>{
		vector<ArchiveStart> y;
		for( let& property : starts )
			y.push_back( {property, group} );
		ul _{ _mutex };
		_nodes.insert_or_assign( move(node), Served{ {move(group), index}, move(starts) } );
		return y;
	}
	α GatewayHistoryHost::Remove( const NodeId& node )ι->void{
		ul _{ _mutex };
		_nodes.erase( node );
	}
}