#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include "EAccess.h"

namespace Jde::Opc::Server{
	//The host's answers to what a user may do with a node, which UAAccess's callbacks ask under open62541's service lock:  a
	//lookup each, never a wait or a throw.  The OpcServer's is OpcAuthorize, over its nodeIds resource tree.
	struct INodeAccess{
		virtual ~INodeAccess()=default;
		β NodeRights( const NodeId& nodeId, UserPK executer )ι->Access::ERights=0;
		β EditRights( const NodeId& nodeId, UserPK executer )ι->Access::ERights=0;//for an edit of its history.
		β MayBrowse( const NodeId& nodeId, UserPK executer )ι->bool=0;
		β UserRights( const NodeId& nodeId, UserPK executer )ι->EAccess=0;//the UserAccessLevel.
		//The rights for a call of `method` on `object`, the Call service's question.  The object's, for a host that keeps none per method.
		β CallRights( const NodeId& object, const NodeId& /*method*/, UserPK executer )ι->Access::ERights{ return NodeRights( object, executer ); }
	};
}