#include "OpcServerHistoryHost.h"

#define let const auto
namespace Jde::Opc::Server{
	OpcServerHistoryHost::OpcServerHistoryHost( const optional<Hist::Settings>& settings )ε{
		if( settings )
			_historian = ms<Hist::Historian>( *settings, Hist::SystemClock() );
	}

	α OpcServerHistoryHost::Find( const UA_NodeId& node )Ι->optional<Node>{
		let p = _indexes.find( node );
		return p==_indexes.end() ? optional<Node>{} : Node{ _group, p->second };
	}
	α OpcServerHistoryHost::ArchiveStarts()Ι->vector<ArchiveStart>{
		vector<ArchiveStart> y;
		y.reserve( _archiveStarts.size() );
		for( let& property : _archiveStarts )
			y.push_back( {property, _group} );
		return y;
	}
	α OpcServerHistoryHost::Serve( sp<Hist::Group> group, Indexes indexes, vector<NodeId> archiveStarts )ι->void{
		_group = move( group );
		_indexes.clear();
		for( auto& [node, index] : indexes )
			_indexes.emplace( move(node), index );
		_archiveStarts = move( archiveStarts );
	}
}