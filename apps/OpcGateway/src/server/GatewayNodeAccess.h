#pragma once
#include <jde/opc/server/INodeAccess.h>

namespace Jde::Opc::Gateway{
	//The endpoint's INodeAccess at Phase 2 (docs/servers/plan.md):  every node open to Read and browse, since the address space is
	//the connection folders and their `_Connection` objects alone, which any session may read (spec *Sessions and rights*).  The
	//served nodes' rights, through their group's access row, are Phase 3a's.  No history yet, so nothing is editable.
	struct GatewayNodeAccess final : Opc::Server::INodeAccess{
		α NodeRights( const NodeId&, UserPK )ι->Access::ERights override{ return Access::ERights::Read; }
		α EditRights( const NodeId&, UserPK )ι->Access::ERights override{ return Access::ERights::None; }
		α MayBrowse( const NodeId&, UserPK )ι->bool override{ return true; }
		α UserRights( const NodeId&, UserPK )ι->Opc::Server::EAccess override{ return Opc::Server::EAccess::Read; }
	};
}