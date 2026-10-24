#pragma once
#include <absl/container/flat_hash_map.h>
#include <jde/opc/server/IHistoryHost.h>

namespace Jde::Opc::Server{
	//The OpcServer's IHistoryHost (historian spec *OpcServer*):  one group, `server`, of what its nodesets mark
	//Historizing, whose values its clients and its PubSub reader write, so setValue stores them.  Owns the historian, made
	//from /opcServer/hist (ReadHistSettings):  with none, or when another process holds the lock on its path, the server
	//keeps no history.  StartHistory fills it before Run, and the callbacks only read it after.
	struct OpcServerHistoryHost final : IHistoryHost{
		OpcServerHistoryHost( const optional<Hist::Settings>& settings )ε;
		α Historian()Ι->sp<Hist::Historian> override{ return _historian; }
		α Collects()Ι->bool override{ return true; }
		α Find( const UA_NodeId& node )Ι->optional<Node> override;
		α ArchiveStarts()Ι->vector<ArchiveStart> override;
		α Group()Ι->sp<Hist::Group>{ return _group; }//the one group, once StartHistory has made it.
		//StartHistory's:  the group, each historized node's index in it, and the HA Configuration properties that take the
		//archive's start, StartOfArchive and StartOfOnlineArchive.
		using Indexes = vector<std::pair<NodeId,Hist::NodeIndex>>;
		α Serve( sp<Hist::Group> group, Indexes indexes, vector<NodeId> archiveStarts )ι->void;
	private:
		sp<Hist::Historian> _historian;
		sp<Hist::Group> _group;
		absl::flat_hash_map<NodeId,Hist::NodeIndex,NodeHash,NodeEqual> _indexes;
		vector<NodeId> _archiveStarts;
	};
}