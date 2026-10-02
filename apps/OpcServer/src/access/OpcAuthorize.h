#pragma once
#include <jde/access/Authorize.h>
#include <absl/synchronization/mutex.h>

namespace Jde::Opc::Server{
//	struct Listener; struct Loader; struct Permission;
	enum class EAccess : uint8{
		None			= 0,
		Read			= UA_ACCESSLEVELMASK_READ,
		Write			= UA_ACCESSLEVELMASK_WRITE,
		HistoryRead		= UA_ACCESSLEVELMASK_HISTORYREAD,
		HistoryWrite	= UA_ACCESSLEVELMASK_HISTORYWRITE,
		SemanticChange	= UA_ACCESSLEVELMASK_SEMANTICCHANGE,
		StatusWrite		= UA_ACCESSLEVELMASK_STATUSWRITE,
		TimestampWrite	= UA_ACCESSLEVELMASK_TIMESTAMPWRITE,
		All				= Read | Write | HistoryRead | HistoryWrite | SemanticChange | StatusWrite | TimestampWrite
	};

	//Node ACLs are stored in the generic Access::ERights vocabulary - the same columns as every other resource, and what the
	//node-access page writes (node-access.ts: "NOT EAccess/EWriteAccess").  EAccess is open62541's UA_ACCESSLEVELMASK layout and
	//the two share no bit positions (ERights::Read=0x2 is UA WRITE), so the only way across is this translation - never a cast.
	//Policy:  Read covers the history read, Update the value and history writes, Delete the history write, Administer the
	//status/timestamp/semantic-change bits;  Create, Purge, Subscribe and Execute have no node-level bit (methods go through
	//GetUserExecutable, a subscription needs Read).  ERights::All comes out as EAccess::All.  access-review3 #4.
	constexpr α ToAccess( Access::ERights rights )ι->EAccess{
		using enum Access::ERights;
		uint8 y{};
		auto add = [&]( Access::ERights right, EAccess access ){ if( !empty(rights & right) ) y |= underlying(access); };
		add( Read, EAccess::Read | EAccess::HistoryRead );
		add( Update, EAccess::Write | EAccess::HistoryWrite );
		add( Delete, EAccess::HistoryWrite );
		add( Administer, EAccess::StatusWrite | EAccess::TimestampWrite | EAccess::SemanticChange );
		return (EAccess)y;
	}

	struct OpcAuthorize final: Access::Authorize{
		OpcAuthorize( string app )ι:Access::Authorize{move(app)}{}
		//The rights this user holds on this node, in the generic vocabulary the acl rows are written in:  the node's own
		//resource when it has one, else the nearest configured ancestor's, else root - and ERights::All when the server
		//has no base resources at all (unauthorized:  every node open).  The node-scoped answer every access-control
		//callback owes;  a flat Authorize::Rights/Test on a resource *name* answers All for a name nothing created,
		//which is what left writeMask, browse and AddReferences ungated (opcserver-review3 #8).
		α NodeRights( const NodeId& nodeId, UserPK executer )ι->Access::ERights;
		α UserRights( NodeId nodeId, UserPK executer )ι->EAccess;//NodeRights in UA access-level bits, for getUserAccessLevel.
		α AssignRights( UA_Server& server )ι->void;
		//The AppServer's delegated admin check (ServerSocketSession::TestAdminAwait → OpcServerQL's adminCheck):  who may grant on a node is
		//whoever administers the resource governing it - the nearest configured ancestor's, else root - the same resolution
		//UserRights applies, which only this server can make.  Other targets take the generic flat rule.  Throws AccessException
		//on denial;  a plain Exception before AssignRights has run, so a check that races startup (the socket registers before
		//Configure and AssignRights) is a denial, never a guess.
		α TestAdminNode( str slug, str criteria, UserPK user, SRCE )ε->void;

		β CreateResource( Access::Resource&& resource )ε->void override;
		β UpdateResourceDeleted( Access::ResourcePK pk, sv schemaName, const jobject& args, bool restored )ε->void override;
	private:
		α ReassignRights( sv why )ι->void;
		α IsNodeResource( Access::ResourcePK pk, sv schemaName, const jobject& args )ι->bool;
		//Fills `nodeResources` - a local map the public overload swaps in afterwards, never the member, so no lock is held
		//across UA_Server_browse (opcserver-review3 #10).
		α AssignRights( const NodeId& nodeId, UA_Server& server, Access::ResourcePK resourcePK, const std::map<NodeId, Access::ResourcePK>& baseResources, std::map<NodeId, Access::ResourcePK>& nodeResources, std::set<NodeId>& visited )ι->void;
		absl::Mutex _nodeResourcesMutex;
		std::map<NodeId, Access::ResourcePK> _nodeResources ABSL_GUARDED_BY(_nodeResourcesMutex);
		bool _enabled ABSL_GUARDED_BY(_nodeResourcesMutex){};//true once base resources are configured; when false the server is unauthorized and every node is fully accessible.
		std::atomic<bool> _assigned{};//AssignRights has run (with or without base resources) - TestAdminNode denies until then.
		Access::ResourcePK _rootResourcePK ABSL_GUARDED_BY(_nodeResourcesMutex){};//resource covering the ObjectsFolder root; unmapped nodes inherit it rather than being granted all access.
	};
}