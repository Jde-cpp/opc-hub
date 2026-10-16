#pragma once
#include <open62541/server.h>
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
		//The rights this user holds on this node, in the generic vocabulary the acl rows are written in:  what its governing
		//resource - its own, else the nearest configured ancestor's, else root - and every configured ancestor of that one up to
		//root allow, less what any of them denies, enforced or not;  All where none of them is enforced.  A grant on a node adds
		//to the ones above it and never takes them away (reviews-issue/252-effective-rights-group.md #1).  ERights::All when the
		//server enforces no base resource at all (unauthorized:  every node open).  The node-scoped answer every access-control
		//callback owes;  a flat Authorize::Rights/Test on a resource *name* answers All for a name nothing created,
		//which is what left writeMask, browse and AddReferences ungated (opcserver-review3 #8).
		α NodeRights( const NodeId& nodeId, UserPK executer )ι->Access::ERights;
		//The rights this user holds on this node for an edit of its history:  NodeRights where a resource that is enforced
		//governs the node, and None where none does, though reads then answer All.  An open server knows who asked but holds
		//no policy that says they may change the record, and the audit trail needs one (historian spec *Authorization*, #237).
		α EditRights( const NodeId& nodeId, UserPK executer )ι->Access::ERights;
		//Whether the user may browse the node - list it, as a folder:  Read on the node, or on any resource configured beneath
		//it in the Objects tree, so the path down to a granted branch lists as r-x on each parent directory does
		//(open62541-1.5.9 review #2, #4).  Outside that tree - Root, Types, Views, the type nodes - namespace 0 and the type
		//nodes stay open whatever the root resource says:  a branch-restricted user decodes the values it may read with them.
		//Reads nothing from the server - since 1.5.9 it answers for every attribute read but Value (review #12).
		α MayBrowse( const NodeId& nodeId, UserPK executer )ι->bool;
		α UserRights( NodeId nodeId, UserPK executer )ι->EAccess;//NodeRights in UA access-level bits, for getUserAccessLevel, but HistoryWrite, which is EditRights'.
		α AssignRights( UA_Server& server )ι->void;
		//The AppServer's delegated admin check (ServerSocketSession::TestAdminAwait → OpcServerQL's adminCheck):  who may grant on a node is
		//whoever NodeRights gives Administer there - the same resolution, which only this server can make.  Other targets take the generic flat rule.  Throws AccessException
		//on denial;  a plain Exception before AssignRights has run, so a check that races startup (the socket registers before
		//Configure and AssignRights) is a denial, never a guess.
		α TestAdminNode( str slug, str criteria, UserPK user, SRCE )ε->void;

		β CreateResource( Access::Resource&& resource )ε->void override;
		β UpdateResourceDeleted( Access::ResourcePK pk, sv schemaName, const jobject& args, bool restored )ε->void override;
		//A grant on a node mints its resource in the db, and the roleAdded event is all the cache hears of it - no resourcesCreated,
		//so CreateResource's re-map never ran and the grant waited for a restart (reviews/m3-closing.md #14's open end).
		β AddRolePermission( Access::RolePK rolePK, const Access::Permission& permission, const jobject& resource )ι->void override;
	private:
		α ReassignRights( sv why )ι->void;
		α IsNodeResource( Access::ResourcePK pk, sv schemaName, const jobject& args )ι->bool;
		//Fills `nodeResources` - a local map the public overload swaps in afterwards, never the member, so no lock is held
		//across UA_Server_browse (opcserver-review3 #10).
		//`path` is nodeId's ancestors and nodeId itself;  each base node the walk meets adds its resource to `beneath` for all of them.
		//`enclosing` gets each base resource met below the root, keyed to the resource governing the node above it.
		α AssignRights( const NodeId& nodeId, UA_Server& server, Access::ResourcePK resourcePK, const std::map<NodeId, Access::ResourcePK>& baseResources, std::map<NodeId, Access::ResourcePK>& nodeResources, std::set<NodeId>& visited, vector<NodeId>& path, std::map<NodeId, vector<Access::ResourcePK>>& beneath, std::map<Access::ResourcePK, Access::ResourcePK>& enclosing )ι->void;
		struct NodeAccess final{ Access::ERights Rights; Access::ERights Edits; };//NodeRights and EditRights, from one look at the governing resources.
		α Resolve( const NodeId& nodeId, UserPK executer )ι->NodeAccess;
		//The user's rights under the chain:  the OR of what its resources allow, less the OR of what they deny, enforced or not.
		//All where none of them is loaded and enforced, and the edit rights None there:  an edit needs a resource that is enforced.
		ABSL_SHARED_LOCKS_REQUIRED(Mutex) α RightsOnLocked( const vector<Access::ResourcePK>& chain, UserPK executer )ι->NodeAccess;
		struct Governing final{ vector<Access::ResourcePK> Chain; bool InTree; };//InTree:  AssignRights' walk under Objects mapped the node.
		//The resources governing the node - its own, else the nearest configured ancestor's, else root, then each one enclosing
		//that up to root - or nullopt when it is open:  no base resources at all, or none over it and no root.
		ABSL_SHARED_LOCKS_REQUIRED(_nodeResourcesMutex) α GoverningLocked( const NodeId& nodeId )Ι->optional<Governing>;
		ABSL_SHARED_LOCKS_REQUIRED(_nodeResourcesMutex) α ChainLocked( Access::ResourcePK resourcePK )Ι->vector<Access::ResourcePK>;//resourcePK, then up _enclosing.
		absl::Mutex _nodeResourcesMutex;
		std::map<NodeId, Access::ResourcePK> _nodeResources ABSL_GUARDED_BY(_nodeResourcesMutex);
		std::map<NodeId, vector<Access::ResourcePK>> _beneath ABSL_GUARDED_BY(_nodeResourcesMutex);//the resources configured below each node in the Objects tree that has any.
		std::map<Access::ResourcePK, Access::ResourcePK> _enclosing ABSL_GUARDED_BY(_nodeResourcesMutex);//each criteria resource's nearest configured ancestor's, the root's for one with no other.
		std::set<NodeId> _typeNodes ABSL_GUARDED_BY(_nodeResourcesMutex);//the ObjectTypes, VariableTypes, DataTypes and ReferenceTypes outside namespace 0, for MayBrowse.
		bool _enabled ABSL_GUARDED_BY(_nodeResourcesMutex){};//true once a base resource is enforced; when false the server is unauthorized and every node is fully accessible, but for edits of its history.
		std::atomic<bool> _assigned{};//AssignRights has run (with or without base resources) - TestAdminNode denies until then.
		Access::ResourcePK _rootResourcePK ABSL_GUARDED_BY(_nodeResourcesMutex){};//resource covering the ObjectsFolder root; unmapped nodes inherit it rather than being granted all access.
	};
}