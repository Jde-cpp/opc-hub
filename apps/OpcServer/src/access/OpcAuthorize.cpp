#include "OpcAuthorize.h"
#include <set>
#include <jde/fwk/utils/Stopwatch.h>
#include "../UAServer.h"

#define let const auto

namespace Jde::Opc::Server{
	constexpr ELogTags _tags{ ( ELogTags )( (EOpcLogTags)ELogTags::Access | EOpcLogTags::Opc ) };

	α OpcAuthorize::AssignRights( const NodeId& nodeId, UA_Server& server, Access::ResourcePK resourcePK, const std::map<NodeId, Access::ResourcePK>& baseResources, std::map<NodeId, Access::ResourcePK>& nodeResources, std::set<NodeId>& visited )ι->void{
		if( auto it = baseResources.find(nodeId); it!=baseResources.end() )
			resourcePK = it->second;
		UA_BrowseDescription bd{
			nodeId,
			UA_BROWSEDIRECTION_FORWARD,
			UA_NODEID_NUMERIC( 0, UA_NS0ID_HIERARCHICALREFERENCES ),//Organizes/HasComponent/HasProperty/... — all hierarchical subtypes, so variables & properties under typed objects are covered, not just Organizes children.
			UA_TRUE,
			UA_UINT32_MAX,
			UA_NODECLASS_OBJECT | UA_NODECLASS_VARIABLE | UA_NODECLASS_METHOD
		};
		auto br = UA_Server_browse( &server, UA_UINT32_MAX, &bd );
		if( br.statusCode ){
			UAException{ br.statusCode, Ƒ("Could not browse node {}", nodeId.ToString()) };
			return;
		}
		for( uint i=0; i<br.referencesSize; ++i ){
			auto ref = br.references[i];
			NodeId childNodeId{ ref.nodeId.nodeId };
			if( !visited.insert(childNodeId).second )
				continue;//already visited: shared component or reference cycle.
			auto childResourcePK = resourcePK;//per-child: siblings must inherit this node's resource, not whatever a previous sibling overrode it to.
			if( let it = baseResources.find(childNodeId); it!=baseResources.end() ){
				childResourcePK = it->second;
				TRACE( "[{}]resource:{}", childNodeId.ToString(), it->second );
			}
			if( childResourcePK )
				nodeResources.insert_or_assign( childNodeId, childResourcePK );
			if( ref.nodeClass & (UA_NODECLASS_OBJECT|UA_NODECLASS_VARIABLE|UA_NODECLASS_METHOD) )
				AssignRights( childNodeId, server, childResourcePK, baseResources, nodeResources, visited );
		}
		UA_BrowseResult_clear(&br);
	}

	//Neither lock is held across the browse.  UA_Server_browse takes the server's serviceMutex, and open62541 calls the
	//access-control plugin *with* that mutex held - so a client Read on the UA thread lands in NodeRights, which needs
	//_nodeResourcesMutex, while this thread holds it exclusively and waits for serviceMutex:  a permanent hang, and the
	//shared `Mutex` was the same shape once a writer queued behind it (opcserver-review3 #10).  So: scan Resources under
	//`Mutex`, browse into a local map with nothing held, publish under `_nodeResourcesMutex`.  Startup calls this before
	//UAServer::Run() now, which closes the window on its own, but the swap is what makes a later re-run safe (L22).
	α OpcAuthorize::AssignRights( UA_Server& server )ι->void{
		Stopwatch sw{ "OpcAuthorize::AssignRights", _tags };
		std::map<NodeId, Access::ResourcePK> baseResources;
		const NodeId root=NodeId::ObjectsFolder();
		{
			sl _{ Mutex };
			for( let& [pk,resource] : Resources ){
				if( resource.IsDeleted || resource.Slug!="nodeIds" || resource.Schema!=_app )
					continue;
				try{
					baseResources.emplace( resource.Criteria.empty() ? root : NodeId::DecodeJson(resource.Criteria), pk );
				}
				catch( runtime_error& e ){
					ERR( "Invalid NodeId '{}' for permission {}: {}", resource.Criteria, pk, e.what() );
					if( auto jde = dynamic_cast<Exception*>(&e); jde )
						jde->SetLevel( ELogLevel::NoLog );
				}
			}
		}
		Access::ResourcePK rootResourcePK{};
		std::map<NodeId, Access::ResourcePK> nodeResources;
		//An empty scan publishes the empty state anyway: on a re-run the operator has just deleted the last one, and a
		//stale map would keep enforcing it.
		if( !baseResources.empty() ){
			if( let it = baseResources.find(root); it!=baseResources.end() ){
				rootResourcePK = it->second;
				nodeResources.emplace( root, rootResourcePK );
				TRACE( "[{}]resource: {}", root.ToString(), rootResourcePK );
			}
			std::set<NodeId> visited{ root };
			AssignRights( root, server, rootResourcePK, baseResources, nodeResources, visited );
		}
		let nodeCount = nodeResources.size();
		{
			ul _{ _nodeResourcesMutex };//_enabled last of the three: it is what opens NodeRights' lookup of the other two.
			_nodeResources = move( nodeResources );
			_rootResourcePK = rootResourcePK;
			_enabled = !baseResources.empty();
		}
		_assigned = true;
		//Which branch this took decides open-vs-enforcing for the process lifetime, and nothing said so: a run whose
		//writes were authorized could not be told from one that was never enforcing (soak-findings #4).
		INFOT( _tags, "[{}]Node rights assigned - {}: {} nodeIds resource(s), {} node(s) mapped, root resource {}.", _app,
			baseResources.empty() ? "OPEN, every node unprotected" : "ENFORCING", baseResources.size(), nodeCount, rootResourcePK );
	}

	α OpcAuthorize::CreateResource( Access::Resource&& resource )ε->void{
		let mine = resource.Slug=="nodeIds" && resource.Schema==_app;
		Access::Authorize::CreateResource( move(resource) );
		if( mine )
			ReassignRights( "a nodeIds resource was created" );
	}
	α OpcAuthorize::UpdateResourceDeleted( Access::ResourcePK pk, sv schemaName, const jobject& args, bool restored )ε->void{
		let mine = IsNodeResource( pk, schemaName, args );
		Access::Authorize::UpdateResourceDeleted( pk, schemaName, args, restored );//throws when it matched nothing - then there was nothing to re-map either.
		if( mine )
			ReassignRights( restored ? "a nodeIds resource was restored" : "a nodeIds resource was deleted" );
	}

	//The base resolves a missing pk from the args the same way;  a by-slug change carries the slug outright.
	α OpcAuthorize::IsNodeResource( Access::ResourcePK pk, sv schemaName, const jobject& args )ι->bool{
		if( let slug = Json::FindSV(args, "slug"); slug )
			return *slug=="nodeIds" && (schemaName.empty() || schemaName==_app);
		if( !pk )
			pk = Json::FindNumber<Access::ResourcePK>( args, "id" ).value_or( 0 );
		sl _{ Mutex };
		let p = Resources.find( pk );
		return p!=Resources.end() && p->second.Slug=="nodeIds" && p->second.Schema==_app;
	}

	α OpcAuthorize::ReassignRights( sv why )ι->void{
		if( !_assigned )
			return;//startup's own AssignRights has not run yet - it will read the row from Resources when it does.
		auto ua = Server::FindUAServer();
		if( !ua )
			return;//Access::Client::Configure installs this listener before Initialize creates the server, and the resource sync fires through it.
		INFOT( _tags, "[{}]Re-assigning node rights: {}.", _app, why );
		AssignRights( *ua );//UAServer's operator UA_Server&.
	}

	α OpcAuthorize::TestAdminNode( str slug, str criteria, UserPK user, SL sl )ε->void{
		if( slug!="nodeIds" )
			return TestAdminLocal( _app, slug, criteria, user, sl );
		THROW_IFSL( !_assigned, "[{}]admin check before AssignRights - not ready.", _app );
		Access::ResourcePK pk{};
		{
			Jde::sl _{ _nodeResourcesMutex };
			if( !_enabled )
				return;//no base resources: the server is unauthorized and every node open, as UserRights answers.
			let node = criteria.empty() ? NodeId::ObjectsFolder() : NodeId::DecodeJson( criteria );//an undecodable criteria throws - a denial.
			pk = Find( _nodeResources, node ).value_or( _rootResourcePK );//the governing resource - the nearest configured ancestor's, else root - exactly UserRights' resolution, so who may grant on a node is who administers what already protects it.
		}
		if( pk )//outside every configured branch - open, as UserRights leaves it.
			TestAdmin( pk, user, sl );//no-op on a deleted row, as UserRights opens a deleted resource.
	}

	α OpcAuthorize::NodeRights( const NodeId& nodeId, UserPK executer )ι->Access::ERights{
		using enum Access::ERights;
		optional<Access::ResourcePK> resourcePK;
		{
			sl _{ _nodeResourcesMutex };
			if( !_enabled )
				return All; //authorization not configured for this server: all nodes open.
			resourcePK = Find( _nodeResources, nodeId );
			if( !resourcePK ){
				if( !_rootResourcePK )
					return All; //no root resource: nodes outside a configured branch stay open (protect-specific-branches config).
				resourcePK = _rootResourcePK; //unmapped node (e.g. created after startup) inherits the root resource instead of granting all access.
			}
		}


		sl _{ Mutex };
		//Both resource checks precede the user lookup:  whether a node is protected is a property of the resource, not of
		//who is asking.  The other way round, an unprotected tree answered None to a user with no acl row and All to one
		//with any - which is what denied a gateway session every read, and (once browse and the write mask were routed
		//here too) every browse, on a server nobody had configured rights on (opcserver-review3 #8).
		auto resource = Resources.find( *resourcePK );
		if( resource==Resources.end() ){
			//AssignRights captured this pk from a row that has since gone.  It is the ordinary state, not a corruption:
			//ResourceSyncAwait creates the installation row soft-deleted, AssignRights does not filter deleted rows
			//(access-review3 L22), and the row is dropped from Resources afterwards.  Nothing is protecting these nodes,
			//which is the same answer Authorize::Rights gives for a resource nothing configured.
			static std::atomic_flag logged;//once:  stable for the life of the process, and this runs per read and per browse.
			if( !logged.test_and_set() )
				WARNT( _tags, "Resource {} is no longer loaded - the nodes it covered are unprotected.  AssignRights took it as a base resource when it was still present.", *resourcePK );
			return All;
		}
		if( resource->second.IsDeleted )
			return All; //resource deleted: node no longer protected.
		auto user = Users.find( executer );
		if( user==Users.end() || user->second.IsDeleted )
			return None;
		return user->second.ResourceRights( *resourcePK ).Effective();
	}

	α OpcAuthorize::UserRights( NodeId nodeId, UserPK executer )ι->EAccess{
		return ToAccess( NodeRights(nodeId, executer) );//generic rights in, UA access-level bits out - a cast put every right one bit off.
	}
}