#pragma once
#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>
#include <jde/access/Authorize.h>
#include <jde/opc/server/INodeAccess.h>

namespace Jde::Opc::Gateway{
	//The endpoint's INodeAccess (docs/servers/spec.md *Sessions and rights*, plan Phase 3a):  a served node's rights are its group's
	//access row, keyed by the group's guid as HistHost::Gate resolves it - the row of its own when there is one, else the hist_groups
	//table's, neither active open - through Authorize::Rights' criteria overload, a lookup, never Test, which throws, since UAAccess
	//asks under open62541's service lock (spec *Threading*).  Edit rights, a history edit's, by CheckEnforced:  a grant on an enforced
	//row, None where nothing enforces the group (#237).  The mirror's map, node to group guid, is written by GatewayServer as it adds
	//and removes nodes, under this lock, the innermost.  Every other node - the connection folders, `_Connection` and its variables,
	//the path folders, an HA Configuration and its properties - is open to Read and browse by any session;  a served node a session
	//may not read is not browsable to it:  open62541 refuses a Browse of the node, a path translated through or to it and every
	//attribute read, Bad_UserAccessDenied, though its parent's listing still names it (ua_services_view.c, browseWithContinuation).
	struct GatewayNodeAccess final : Opc::Server::INodeAccess{
		GatewayNodeAccess( sp<Access::Authorize> acl )ι:_acl{ move(acl) }{}
		α Add( NodeId node, string guid )ι->void;
		α Remove( const NodeId& node )ι->void;
		α Guid( const NodeId& node )Ι->optional<string>;//the group's, for a served node.

		α NodeRights( const NodeId& node, UserPK executer )ι->Access::ERights override;
		α EditRights( const NodeId& node, UserPK executer )ι->Access::ERights override;
		α MayBrowse( const NodeId& node, UserPK executer )ι->bool override;
		α UserRights( const NodeId& node, UserPK executer )ι->Opc::Server::EAccess override;//NodeRights in access-level bits, HistoryWrite EditRights'.
	private:
		static constexpr sv Schema{ "gateway" }, Resource{ "histGroups" };
		α Rights( str guid, UserPK executer )Ι->Access::ERights;
		α Edits( str guid, UserPK executer )Ι->Access::ERights;
		sp<Access::Authorize> _acl;
		mutable absl::Mutex _mutex;
		absl::flat_hash_map<NodeId,string> _guids ABSL_GUARDED_BY(_mutex);
	};
}