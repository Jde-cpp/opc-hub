#pragma once
#include <jde/opc/server/IHistoryHost.h>

namespace Jde::Opc::Gateway{
	struct ServedValues;
	//The endpoint's IHistoryHost (docs/servers/spec.md *History*, plan Phase 4):  the gateway's historian, HistHost's, and each served
	//variable's group and index in it, which UAHistory asks under open62541's service lock - answered from the mirror's cache, whose
	//map of served nodes carries each one's history beside its member, so the two come and go together.  Collects nothing:  the
	//collector stores every value as it arrives, and setValue would store each twice.  A page's values are remapped into the
	//endpoint's namespaces by the group's NamespaceArray as it is at read time, since the library stores no URI inside a value, and a
	//HistoryUpdate's values are mapped back the same way (spec *Namespaces*).  The archive starts are each served variable's
	//StartOfArchive and StartOfOnlineArchive, which UAHistory writes at Start and after each midnight, and GatewayServer has it write
	//as a node is served while the endpoint runs.
	struct GatewayHistoryHost final : Opc::Server::IHistoryHost{
		GatewayHistoryHost( sp<Opc::Hist::Historian> historian, sp<ServedValues> values )ι;
		α Historian()Ι->sp<Opc::Hist::Historian> override{ return _historian; }
		α Collects()Ι->bool override{ return false; }
		α Find( const UA_NodeId& node )Ι->optional<Node> override;
		α ArchiveStarts()Ι->vector<ArchiveStart> override;
		α Remap( const UA_NodeId& node, std::span<UA_DataValue> values )ι->void override;
		α Unmap( const UA_NodeId& node, UA_Variant& value )ι->uint override;
	private:
		const sp<Opc::Hist::Historian> _historian;
		const sp<ServedValues> _values;
	};
}