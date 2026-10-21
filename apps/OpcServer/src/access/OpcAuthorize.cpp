#include "OpcAuthorize.h"
#include <set>
#include <jde/fwk/utils/Stopwatch.h>
#include "../UAServer.h"

#define let const auto

namespace Jde::Opc::Server{
	constexpr ELogTags _tags{ ( ELogTags )( (EOpcLogTags)ELogTags::Access | EOpcLogTags::Opc ) };

	α OpcAuthorize::Walk::Descend( const NodeId& nodeId, UA_Server& server, Access::ResourcePK resourcePK )ι->void{
		if( auto it = Base.find(nodeId); it!=Base.end() )
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
			if( !Visited.insert(childNodeId).second )
				continue;//already visited: shared component or reference cycle.
			auto childResourcePK = resourcePK;//per-child: siblings must inherit this node's resource, not whatever a previous sibling overrode it to.
			if( let it = Base.find(childNodeId); it!=Base.end() ){
				childResourcePK = it->second;
				TRACE( "[{}]resource:{}", childNodeId.ToString(), it->second.Value );
				if( resourcePK && resourcePK!=it->second )
					Enclosing.try_emplace( it->second, resourcePK );
				for( let& ancestor : Path )
					Beneath[ancestor].push_back( it->second );
			}
			if( childResourcePK )
				NodeResources.insert_or_assign( childNodeId, childResourcePK );
			if( ref.nodeClass & (UA_NODECLASS_OBJECT|UA_NODECLASS_VARIABLE|UA_NODECLASS_METHOD) ){
				Path.push_back( childNodeId );
				Descend( childNodeId, server, childResourcePK );
				Path.pop_back();
			}
		}
		UA_BrowseResult_clear(&br);
	}

	//Neither lock is held across the browse.  UA_Server_browse takes the server's serviceMutex, and open62541 calls the
	//access-control plugin *with* that mutex held - so a client Read on the UA thread lands in NodeRights, which needs
	//_nodeResourcesMutex, while this thread holds it exclusively and waits for serviceMutex:  a permanent hang, and the
	//shared `Mutex` was the same shape once a writer queued behind it (opcserver-review3 #10).  So: scan Resources under
	//`Mutex`, walk into a local Walk with nothing held, publish under `_nodeResourcesMutex`.  Startup calls this before
	//UAServer::Run() now, which closes the window on its own, but the swap is what makes a later re-run safe (L22).
	α OpcAuthorize::AssignRights( UA_Server& server )ι->void{
		Stopwatch sw{ "OpcAuthorize::AssignRights", _tags };
		//Unenforced rows are mapped too:  their grants still count under an enforced resource above or below them - the root ships
		//unenforced, and its grants hold under a node someone was granted on (reviews-issue/252-effective-rights-group.md #1).
		Walk walk;
		const NodeId root=NodeId::ObjectsFolder();
		bool rootEnforced{};
		{
			rl _{ Mutex };
			for( let& [pk,resource] : Resources ){
				if( resource.Slug!="nodeIds" || resource.Schema!=_app )
					continue;
				try{
					walk.Base.emplace( resource.Criteria.empty() ? root : NodeId::DecodeJson(resource.Criteria), pk );
					walk.Enforcing |= !resource.IsDeleted;
					rootEnforced |= resource.Criteria.empty() && !resource.IsDeleted;
				}
				catch( runtime_error& e ){
					ERR( "Invalid NodeId '{}' for permission {}: {}", resource.Criteria, pk.Value, e.what() );
					if( auto jde = dynamic_cast<Exception*>(&e); jde )
						jde->SetLevel( ELogLevel::NoLog );
				}
			}
		}
		//A scan with nothing enforced publishes the empty state anyway: on a re-run the operator has just deleted the last
		//one, and a stale map would keep enforcing it.
		if( walk.Enforcing ){
			if( let it = walk.Base.find(root); it!=walk.Base.end() ){
				walk.RootResource = it->second;
				walk.NodeResources.emplace( root, walk.RootResource );
				TRACE( "[{}]resource: {}", root.ToString(), walk.RootResource.Value );
			}
			walk.Visited.insert( root );
			walk.Path.push_back( root );
			walk.Descend( root, server, walk.RootResource );
		}
		if( rootEnforced ){//MayBrowse opens the type nodes the root resource would refuse - known here, not read per call.
			UA_BrowseDescription bd{ UA_NODEID_NUMERIC(0, UA_NS0ID_TYPESFOLDER), UA_BROWSEDIRECTION_FORWARD, UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES), UA_TRUE,
				UA_NODECLASS_OBJECTTYPE | UA_NODECLASS_VARIABLETYPE | UA_NODECLASS_DATATYPE | UA_NODECLASS_REFERENCETYPE, 0 };
			size_t size{}; UA_ExpandedNodeId* found{};
			if( let sc = UA_Server_browseRecursive(&server, &bd, &size, &found); !sc ){
				for( size_t i=0; i<size; ++i ){
					if( found[i].nodeId.namespaceIndex )
						walk.TypeNodes.emplace( found[i].nodeId );
				}
				UA_Array_delete( found, size, &UA_TYPES[UA_TYPES_EXPANDEDNODEID] );
			}
			else
				WARNT( _tags, "Could not collect the type nodes - every one outside namespace 0 is refused to a user without Read on the root:  {}", UAException::Message(sc) );
		}
		let baseCount = walk.Base.size();
		let nodeCount = walk.NodeResources.size();
		let typeNodeCount = walk.TypeNodes.size();
		let enforcing = walk.Enforcing;
		let rootResourcePK = walk.RootResource;
		Publish( move(walk) );
		_assigned = true;
		//Which branch this took decides open-vs-enforcing for the process lifetime, and nothing said so: a run whose
		//writes were authorized could not be told from one that was never enforcing (soak-findings #4).
		INFOT( _tags, "[{}]Node rights assigned - {}: {} nodeIds resource(s), {} node(s) mapped, root resource {}, {} type node(s) outside namespace 0.", _app,
			enforcing ? "ENFORCING" : "OPEN, every node unprotected, history edits refused until a nodeIds resource is enforced", baseCount, nodeCount, rootResourcePK.Value, typeNodeCount );
	}
	α OpcAuthorize::Publish( Walk&& walk )ι->void{
		ul _{ _nodeResourcesMutex };//_enabled last: it is what opens NodeRights' lookup of the rest.
		_nodeResources = move( walk.NodeResources );
		_beneath = move( walk.Beneath );
		_enclosing = move( walk.Enclosing );
		_typeNodes = move( walk.TypeNodes );
		_rootResourcePK = walk.RootResource;
		_enabled = walk.Enforcing;
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
	α OpcAuthorize::PurgeResource( Access::ResourcePK pk )ι->void{
		let mine = IsNodeResource( pk, {}, {} );
		Access::Authorize::PurgeResource( pk );
		if( mine )
			ReassignRights( "a nodeIds resource was purged" );
	}

	α OpcAuthorize::AddRolePermission( Access::RolePK rolePK, const Access::Permission& permission, const jobject& jResource )ι->void{
		const Access::Resource resource{ jResource };
		bool cached{ true };
		if( resource.Slug=="nodeIds" && resource.Schema==_app ){
			rl _{ Mutex };
			cached = FindResourceLocked( resource )!=nullptr;
		}
		Access::Authorize::AddRolePermission( rolePK, permission, jResource );
		if( !cached )
			ReassignRights( "a role grant created a nodeIds resource" );
	}

	//The base resolves a missing pk from the args the same way;  a by-slug change carries the slug outright.
	α OpcAuthorize::IsNodeResource( Access::ResourcePK pk, sv schemaName, const jobject& args )ι->bool{
		if( let slug = Json::FindSV(args, "slug"); slug )
			return *slug=="nodeIds" && (schemaName.empty() || schemaName==_app);
		if( !pk )
			pk = Access::ResourcePK{ Json::FindNumber<Access::ResourcePK::Type>(args, "id").value_or(0) };
		rl _{ Mutex };
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
		vector<Access::ResourcePK> chain;
		{
			rl _{ _nodeResourcesMutex };
			if( !_enabled )
				return;//no base resources: the server is unauthorized and every node open, as NodeRights answers.
			let node = criteria.empty() ? NodeId::ObjectsFolder() : NodeId::DecodeJson( criteria );//an undecodable criteria throws - a denial.
			if( let pk = Find(_nodeResources, node).value_or(_rootResourcePK); pk )
				chain = ChainLocked( pk );//NodeRights' resolution, so who may grant on a node is who holds Administer on it.
		}
		if( chain.empty() || user.Value==UserPK::System )
			return;//outside every configured branch - open, as NodeRights leaves it.
		TestUser( user, sl );
		rl _{ Mutex };
		if( empty(RightsOnLocked(chain, user).Rights & Access::ERights::Administer) )//All where nothing enforces it, as NodeRights opens a deleted resource.
			throw Access::AccessException{ sl, user, ELogLevel::Trace, "User does not have '{}' access to node '{}'.", ToString(Access::ERights::Administer), criteria };
	}

	α OpcAuthorize::NodeRights( const NodeId& nodeId, UserPK executer )ι->Access::ERights{ return Resolve( nodeId, executer ).Rights; }
	α OpcAuthorize::EditRights( const NodeId& nodeId, UserPK executer )ι->Access::ERights{ return Resolve( nodeId, executer ).Edits; }
	α OpcAuthorize::Resolve( const NodeId& nodeId, UserPK executer )ι->NodeAccess{
		using enum Access::ERights;
		optional<Governing> governing;
		{
			rl _{ _nodeResourcesMutex };
			governing = GoverningLocked( nodeId );
		}
		if( !governing )
			return { All, None };
		rl _{ Mutex };
		return RightsOnLocked( governing->Chain, executer );
	}
	α OpcAuthorize::GoverningLocked( const NodeId& nodeId )Ι->optional<Governing>{
		if( !_enabled )
			return nullopt; //authorization not configured for this server: all nodes open.
		if( let pk = Find(_nodeResources, nodeId); pk )
			return Governing{ ChainLocked(*pk), true };
		if( !_rootResourcePK )
			return nullopt; //no root resource: nodes outside a configured branch stay open (protect-specific-branches config).
		return Governing{ {_rootResourcePK}, false }; //unmapped node (e.g. created after startup) inherits the root resource instead of granting all access.
	}
	α OpcAuthorize::ChainLocked( Access::ResourcePK resourcePK )Ι->vector<Access::ResourcePK>{
		vector<Access::ResourcePK> y;
		for( auto pk = resourcePK; pk; pk = FindDefault(_enclosing, pk) )
			y.push_back( pk );
		return y;
	}
	α OpcAuthorize::RightsOnLocked( const vector<Access::ResourcePK>& chain, UserPK executer )ι->NodeAccess{
		using enum Access::ERights;
		//Both resource checks precede the user lookup:  whether a node is protected is a property of the resource, not of
		//who is asking.  The other way round, an unprotected tree answered None to a user with no acl row and All to one
		//with any - which is what denied a gateway session every read, and (once browse and the write mask were routed
		//here too) every browse, on a server nobody had configured rights on (opcserver-review3 #8).
		bool enforced{};
		for( let pk : chain ){
			auto resource = Resources.find( pk );
			if( resource==Resources.end() ){
				//AssignRights captured this pk from a row that has since gone.  It is the ordinary state, not a corruption:
				//ResourceSyncAwait creates the installation row soft-deleted, AssignRights does not filter deleted rows
				//(access-review3 L22), and the row is dropped from Resources afterwards.  It protects nothing, which is the
				//same answer Authorize::Rights gives for a resource nothing configured.
				static std::atomic_flag logged;//once:  stable for the life of the process, and this runs per read and per browse.
				if( !logged.test_and_set() )
					WARNT( _tags, "Resource {} is no longer loaded - the nodes it covered are protected only by the resources above it.  AssignRights took it as a base resource when it was still present.", pk.Value );
			}
			else
				enforced |= !resource->second.IsDeleted;
		}
		if( !enforced )
			return { All, None }; //nothing over the node is enforced: it is unprotected, and no edit.
		auto user = Users.find( executer );
		if( user==Users.end() || user->second.IsDeleted )
			return { None, None };
		Access::AllowedDisallowed rights{};
		for( let pk : chain ){
			let resource = user->second.ResourceRights( pk );
			rights.Allowed |= resource.Allowed;
			rights.Denied |= resource.Denied;
		}
		return { rights.Effective(), rights.Effective() };
	}
	α OpcAuthorize::MayBrowse( const NodeId& nodeId, UserPK executer )ι->bool{
		optional<Governing> governing;
		vector<vector<Access::ResourcePK>> beneath;//each resource configured below, as its chain
		bool typeNode{};
		{
			rl _{ _nodeResourcesMutex };
			governing = GoverningLocked( nodeId );
			if( !governing )
				return true;
			if( !governing->InTree )
				typeNode = nodeId.namespaceIndex==0 || _typeNodes.contains( nodeId );
			else if( let p = _beneath.find(nodeId); p!=_beneath.end() ){
				for( let pk : p->second )
					beneath.push_back( ChainLocked(pk) );//copies:  `Mutex` is never taken under this one.
			}
		}
		using enum Access::ERights;
		rl _{ Mutex };
		if( !empty(RightsOnLocked(governing->Chain, executer).Rights & Read) )
			return true;
		if( !governing->InTree )
			return typeNode;
		for( let& chain : beneath ){
			if( !empty(RightsOnLocked(chain, executer).Rights & Read) )
				return true;
		}
		return false;
	}

	α OpcAuthorize::UserRights( NodeId nodeId, UserPK executer )ι->EAccess{
		let [rights, edits] = Resolve( nodeId, executer );//generic rights in, UA access-level bits out - a cast put every right one bit off.
		//HistoryWrite is an edit's bit, so it is the edit rights', and left out of an unprotected node's:  a client sees its
		//history as read-only before it tries (historian spec *Authorization*).
		constexpr uint8 historyWrite{ underlying(EAccess::HistoryWrite) };
		return (EAccess)( (underlying(ToAccess(rights)) & ~historyWrite) | (underlying(ToAccess(edits)) & historyWrite) );
	}
}