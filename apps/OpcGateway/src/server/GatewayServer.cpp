#include "GatewayServer.h"
#include <boost/asio/post.hpp>
#include <jde/fwk/settings.h>
#include <jde/fwk/co/Await.h>
#include <jde/opc/UAException.h>
#include <jde/opc/server/UAServer.h>
#include <jde/opc/server/HAConfiguration.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/UAString.h>
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"
#include "GatewayNodeAccess.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Server };
	constexpr sv _root{ "/gateway/server" };
	constexpr sv _ns0Uri{ "http://opcfoundation.org/UA/" };
	using Opc::Server::UASettings;
	using Opc::Server::UAServer;
	using Opc::Server::HAConfiguration::OwnNamespace;
	using EKind = ConnectionChange::EKind;

	static sp<GatewayServer> _instance; static std::mutex _instanceMutex;
	α GatewayServer::Instance()ι->sp<GatewayServer>{
		lg _{ _instanceMutex };
		return _instance;
	}

	α GatewayServer::ReadSettings( const Crypto::CryptoSettings& processSsl )ε->optional<UASettings>{
		let block = Settings::FindObject( _root );
		if( !block )
			return nullopt;
		let key = [&]( sv name ){ return Ƒ( "{}/{}", _root, name ); };
		//absent:  the process's own pair, whose URI SAN is the gateway's applicationUri - the one every server already holds for
		//this gateway (spec *Startup and deploy*).
		let ssl = Settings::FindObject( key("ssl") );
		UASettings y{ .Ssl{ ssl ? Crypto::CryptoSettings{*ssl} : processSsl } };
		y.ApplicationName = Ƒ( "Jde-Cpp OpcGateway [{}]", App::InstanceName() );
		y.Port = Settings::FindNumber<PortType>( key("port") ).value_or( 4841 );
		y.Address = Settings::FindString( key("address") ).value_or( "" );
		let servers = Process::AppDataFolder()/"ssl"/"servers";//the gateway's own, which ServerTrust::EnsureDirs makes.
		for( let& dir : Settings::FindStringArray(key("trustedCertDirs")) )
			y.TrustedCertDirs.emplace_back( dir );
		if( y.TrustedCertDirs.empty() )
			y.TrustedCertDirs.push_back( servers/"clients" );
		y.RejectedCertDir = Settings::FindPath( key("rejectedCertDir") ).value_or( servers/"rejected" );
		y.None = Settings::FindBool( key("none") ).value_or( y.None );
		y.ReadTokens( _root );
		y.Limit.MaxSessions = Settings::FindNumber<uint16>( key("limits/maxSessions") );
		y.Limit.MaxMonitoredItems = Settings::FindNumber<uint32>( key("limits/maxMonitoredItems") );
		y.Limit.SessionTimeout = Settings::FindDuration( key("limits/sessionTimeout") );
		y.Limit.MinSamplingInterval = Settings::FindDuration( key("limits/minSamplingInterval") );
		y.Limit.MaxAsyncOperations = Settings::FindNumber<uint>( key("limits/maxAsyncOperations") );
		return y;
	}

	α GatewayServer::FolderId( const ServerCnnctnNK& slug )ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(OwnNamespace, slug.c_str()) }; }
	α GatewayServer::ConnectionId( const ServerCnnctnNK& slug )ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(OwnNamespace, Ƒ("{}/_Connection", slug).c_str()) }; }
	α GatewayServer::VariableId( const ServerCnnctnNK& slug, sv name )ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(OwnNamespace, Ƒ("{}/_Connection/{}", slug, name).c_str()) }; }
	α GatewayServer::PathFolderId( const ServerCnnctnNK& slug, std::span<const HistPathSegment> path )ι->NodeId{
		string id{ slug };
		for( let& segment : path )
			id += Ƒ( "/{}", segment.Name );
		return NodeId{ UA_NODEID_STRING_ALLOC(OwnNamespace, id.c_str()) };
	}
	α GatewayServer::NamespaceUri( const ServerCnnctnNK& slug, sv remote )ι->string{
		let prefix = Ƒ( "urn:jde:{}:", slug );
		return remote.starts_with( prefix ) ? string{ remote } : prefix+string{ remote };
	}

	α GatewayServer::Start( const Crypto::CryptoSettings& processSsl )ε->void{
		auto settings = ReadSettings( processSsl );
		if( !settings ){
			INFO( "No '{}':  this gateway serves no OPC UA endpoint.", _root );
			return;
		}
		for( let& dir : settings->TrustedCertDirs ){//the places an admin drops a client certificate exist from the first start, as the gateway's ssl/servers does.
			std::error_code ec;
			fs::create_directories( dir, ec );
		}
		auto server = ms<GatewayServer>( move(*settings) );
		//Heard before the rows are read:  a row the hooks write and a client the registry gains from here on post to the strand,
		//and Serial holds them until the folders below exist.
		{
			lg _{ _instanceMutex };
			_instance = server;
		}
		wp<GatewayServer> weak{ server };
		UAClient::SetConnectionListener( [weak]( const ConnectionChange& change ){
			if( auto p = weak.lock(); p )
				p->OnChange( change );
		} );
		try{
			auto rows = BlockAwait<ServerCnnctnAwait,vector<ServerCnnctn>>( ServerCnnctnAwait{nullopt, true} );
			auto hosts = HistHost::Instance().Groups();//as the historian is now:  a group loaded after this is held, and Serve skips what is served.
			std::ranges::sort( hosts, []( let& a, let& b ){ return a->Slug()!=b->Slug() ? a->Slug()<b->Slug() : a->Id()<b->Id(); } );//the namespaces in slug order (spec *Namespaces*).
			lg _{ server->_mutex };
			server->_access = ms<GatewayNodeAccess>( AppClient()->Acl() );
			server->_ua = mu<UAServer>( server->_settings, AppClient(), server->_access, nullptr );
			//The registry as it is now:  a change before this is in it, and one after is held or waits on _mutex.
			let errors = UAClient::LastFailures();//as a live Failed words it, and still there after a recovery.
			for( auto& row : rows ){
				Connection connection{ .Id = row.Id, .Url = move(row.Url), .Connected = UAClient::HasSession(row.Slug) };
				if( auto p = errors.find(row.Slug); p!=errors.end() )
					connection.LastError = p->second;
				server->_connections.emplace( move(row.Slug), move(connection) );
			}
			for( let& [slug, connection] : server->_connections )
				server->AddFolder( slug, connection );
			for( let& host : hosts )
				server->Serve( host->Id(), host->Guid(), host->Slug(), host->HistMembers() );
			for( auto& f : server->_held )
				f();
			server->_held.clear();
			server->_ua->Run();
		}
		catch( std::exception& ){
			Stop();
			throw;
		}
		uint nodes{};
		{
			lg _{ server->_mutex };
			for( let& [_, group] : server->_groups )
				nodes += group.Nodes.size();
		}
		INFOT( ELogTags::App, "OPC UA endpoint at {} - {} connection folder(s), {} served node(s).", server->Url(), server->_connections.size(), nodes );
	}
	GatewayServer::GatewayServer( UASettings settings )ι:
		_settings{ move(settings) },
		_strand{ Executor()->get_executor() }
	{}
	α GatewayServer::Stop()ι->void{
		sp<GatewayServer> server;
		{
			lg _{ _instanceMutex };
			server = move( _instance );
		}
		if( !server )
			return;
		UAClient::SetConnectionListener( nullptr );
		up<UAServer> ua;
		{
			lg _{ server->_mutex };
			ua = move( server->_ua );
		}
		ua.reset();//outside the lock:  it joins the server's thread, and a posted change may be waiting on the lock to find no server.
	}
	α GatewayServer::Server()ι->UAServer*{
		lg _{ _mutex };
		return _ua.get();
	}

	//The gateway's own nodes:  string ids in the endpoint's own namespace, the applicationUri's, stable across starts (spec
	//*Namespaces*).  Attributes are views into the strings for the add, which copies them.
	Ω text( sv s )ι->UA_LocalizedText{ return UA_LocalizedText{ {}, ToUV(s) }; }
	Ω qualified( sv s )ι->UA_QualifiedName{ return UA_QualifiedName{ Opc::Server::HAConfiguration::OwnNamespace, ToUV(s) }; }
	α GatewayServer::AddFolder( const ServerCnnctnNK& slug, const Connection& connection )ι->void{
		try{
			auto& ua = *_ua->Ptr();
			UA_ObjectAttributes folder = UA_ObjectAttributes_default;
			folder.displayName = text( slug );
			UAε( UA_Server_addObjectNode(&ua, FolderId(slug), NodeId::ObjectsFolder(), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), qualified(slug), UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE), folder, nullptr, nullptr) );
			UA_ObjectAttributes object = UA_ObjectAttributes_default;
			object.displayName = text( "_Connection" );
			UAε( UA_Server_addObjectNode(&ua, ConnectionId(slug), FolderId(slug), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), qualified("_Connection"), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), object, nullptr, nullptr) );
			let add = [&]( sv name, const UA_DataType& type ){
				UA_VariableAttributes attributes = UA_VariableAttributes_default;
				attributes.displayName = text( name );
				attributes.dataType = type.typeId;
				attributes.valueRank = UA_VALUERANK_SCALAR;
				attributes.accessLevel = UA_ACCESSLEVELMASK_READ;
				UAε( UA_Server_addVariableNode(&ua, VariableId(slug, name), ConnectionId(slug), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), qualified(name), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr) );
			};
			add( "Connected", UA_TYPES[UA_TYPES_BOOLEAN] );
			add( "Since", UA_TYPES[UA_TYPES_DATETIME] );
			add( "Url", UA_TYPES[UA_TYPES_STRING] );
			add( "LastError", UA_TYPES[UA_TYPES_STRING] );
			add( "Reconnects", UA_TYPES[UA_TYPES_UINT32] );
			WriteAll( slug, connection );
		}
		catch( const std::exception& e ){
			ERR( "Could not add the folder for connection '{}':  {}", slug, e.what() );
		}
	}
	//The folder alone:  open62541 deletes the hierarchical children nothing else references with it (buildDeleteNodeSet).  Its
	//groups' nodes first, for their HA Configurations, which hang off a non-hierarchical reference, and the mirror's map;  a row
	//with groups can't be purged, so there are none unless a hook was missed.
	α GatewayServer::RemoveFolder( const ServerCnnctnNK& slug )ι->void{
		for( auto p = _groups.begin(); p!=_groups.end(); p = p->second.Slug==slug ? RemoveGroup( p ) : std::next( p ) );
		if( let sc = UA_Server_deleteNode(_ua->Ptr(), FolderId(slug), true); sc )
			WARN( "Could not remove the folder for connection '{}':  {}", slug, UAException::Message(sc) );
	}
	α GatewayServer::Write( const ServerCnnctnNK& slug, sv name, const void* scalar, const UA_DataType& type )ι->void{
		UA_Variant value; UA_Variant_setScalar( &value, const_cast<void*>(scalar), &type );
		if( let sc = UA_Server_writeValue(_ua->Ptr(), VariableId(slug, name), value); sc )
			WARN( "Could not write '{}/_Connection/{}':  {}", slug, name, UAException::Message(sc) );
	}
	α GatewayServer::WriteAll( const ServerCnnctnNK& slug, const Connection& connection )ι->void{
		const UA_Boolean connected{ connection.Connected };
		Write( slug, "Connected", &connected, UA_TYPES[UA_TYPES_BOOLEAN] );
		let since = UADateTime{ connection.Since }.UA();
		Write( slug, "Since", &since, UA_TYPES[UA_TYPES_DATETIME] );
		let url = ToUV( connection.Url );
		Write( slug, "Url", &url, UA_TYPES[UA_TYPES_STRING] );
		let error = ToUV( connection.LastError );
		Write( slug, "LastError", &error, UA_TYPES[UA_TYPES_STRING] );
		const UA_UInt32 reconnects = (UA_UInt32)connection.Reconnects;
		Write( slug, "Reconnects", &reconnects, UA_TYPES[UA_TYPES_UINT32] );
	}

	//One strand, not the executor's many threads:  a slug's Lost and its next Activated, or a purge and the row made again under its
	//slug, apply in the order they were posted.  After Stop, _ua is null, so the few still queued are held and go with the server.
	α GatewayServer::Serial( absl::AnyInvocable<void()> f )ι->void{
		boost::asio::post( _strand, [self=shared_from_this(), f=std::move(f)]() mutable {
			lg _{ self->_mutex };
			if( self->_ua )
				f();
			else
				self->_held.push_back( std::move(f) );
		} );
	}
	α GatewayServer::OnChange( const ConnectionChange& change )ι->void{
		Serial( [this, change]{ Apply( change ); } );
	}
	α GatewayServer::Apply( const ConnectionChange& change )ι->void{
		auto p = _connections.find( change.Slug );
		if( p==_connections.end() )//a slug with no row - a test connect's.
			return;
		auto& connection = p->second;
		bool reconnected{};
		if( change.Kind==EKind::Failed )
			connection.LastError = change.Error;
		else if( change.Kind==EKind::Lost && change.Down )//the slug's last session went:  another credential's still up is no outage.
			connection.Outage = true;
		else if( change.Kind==EKind::Activated && connection.Outage ){
			connection.Outage = false;
			++connection.Reconnects;
			reconnected = true;
		}
		//As the registry is now:  a later change has its own Apply after this one.  A reconnect moves Since even when the outage was
		//over before its Lost applied, and Connected never read false.
		if( let connected = UAClient::HasSession(change.Slug); connected!=connection.Connected || reconnected ){
			connection.Connected = connected;
			connection.Since = Clock::now();
		}
		WriteAll( change.Slug, connection );
	}

	α GatewayServer::Find( const DB::Key& key )ι->flat_map<ServerCnnctnNK,Connection>::iterator{
		if( !key.IsPK() )
			return _connections.find( key.NK() );
		return std::ranges::find_if( _connections, [&]( let& kv ){ return kv.second.Id==key.PK(); } );
	}
	α GatewayServer::ConnectionCreated( ServerCnnctn row )ι->void{
		Serial( [this, row=move(row)]() mutable {
			if( _connections.contains(row.Slug) )//Start's rows had it.
				return;
			Connection connection{ .Id = row.Id, .Url = move(row.Url) };
			auto& added = _connections.emplace( row.Slug, move(connection) ).first->second;
			AddFolder( row.Slug, added );
		} );
	}
	α GatewayServer::ConnectionUrlChanged( const DB::Key& key, string url )ι->void{
		Serial( [this, key, url=move(url)]{
			auto p = Find( key );
			if( p==_connections.end() )
				return;
			p->second.Url = url;
			let value = ToUV( p->second.Url );
			Write( p->first, "Url", &value, UA_TYPES[UA_TYPES_STRING] );
		} );
	}
	α GatewayServer::ConnectionPurged( const DB::Key& key )ι->void{
		Serial( [this, key]{
			auto p = Find( key );
			if( p==_connections.end() )
				return;
			RemoveFolder( p->first );
			_connections.erase( p );
		} );
	}

	//The served nodes (spec *Address space*, *Namespaces*).
	α GatewayServer::GroupLoaded( const HistGroupHost& host )ι->void{
		Serial( [this, id=host.Id(), guid=host.Guid(), slug=host.Slug(), nodes=host.HistMembers()]() mutable { Serve( id, move(guid), move(slug), move(nodes) ); } );
	}
	α GatewayServer::GroupRemoved( HistGroupPK group )ι->void{
		Serial( [this, group]{
			if( auto p = _groups.find(group); p!=_groups.end() )
				RemoveGroup( p );
		} );
	}
	α GatewayServer::NodeJoined( const HistGroupHost& host, HistMember member )ι->void{
		Serial( [this, id=host.Id(), guid=host.Guid(), slug=host.Slug(), member=move(member)]() mutable { Serve( id, move(guid), move(slug), {move(member)} ); } );
	}
	α GatewayServer::NodeLeft( HistGroupPK group, HistGroupNodePK index )ι->void{
		Serial( [this, group, index]{
			auto g = _groups.find( group );
			if( g==_groups.end() )
				return;
			if( auto p = g->second.Nodes.find(index); p!=g->second.Nodes.end() )
				RemoveNode( g->second, p );
		} );
	}
	α GatewayServer::NodeDescribed( HistGroupPK group, HistGroupNodePK index, HistNodeAttributes attributes )ι->void{
		Serial( [this, group, index, attributes=move(attributes)]() mutable {
			auto g = _groups.find( group );
			if( g==_groups.end() )
				return;
			auto p = g->second.Nodes.find( index );
			if( p==g->second.Nodes.end() )
				return;
			if( p->second.Member.Attributes.Path==attributes.Path ){
				Rewrite( p->second, attributes );
				p->second.Member.Attributes = move( attributes );
				return;
			}
			HistMember member{ p->second.Member };//the node moves:  first described, or its source rearranged.
			member.Attributes = move( attributes );
			RemoveNode( g->second, p );
			AddNode( g->second, move(member) );
		} );
	}
	α GatewayServer::ThresholdsChanged( HistGroupPK group, vector<std::pair<HistGroupNodePK,Opc::Hist::Thresholds>> thresholds )ι->void{
		Serial( [this, group, thresholds=move(thresholds)]{
			auto g = _groups.find( group );
			if( g==_groups.end() )
				return;
			for( let& [index, t] : thresholds ){
				auto p = g->second.Nodes.find( index );
				if( p==g->second.Nodes.end() )
					continue;
				auto& served = p->second;
				let interval = std::chrono::duration<double,std::milli>{ t.MinTimeInterval }.count();
				if( let sc = UA_Server_writeMinimumSamplingInterval(_ua->Ptr(), served.Id, interval); sc )
					WARN( "[{}]Could not write node {}'s MinimumSamplingInterval:  {}", g->second.Guid, index, UAException::Message(sc) );
				try{
					Opc::Server::HAConfiguration::Publish( *_ua->Ptr(), served.Configuration, t );
				}
				catch( const std::exception& e ){
					WARN( "[{}]Could not write node {}'s HA Configuration:  {}", g->second.Guid, index, e.what() );
				}
				served.Member.Thresholds = t;
			}
		} );
	}

	α GatewayServer::Serve( HistGroupPK id, string guid, ServerCnnctnNK slug, vector<HistMember> members )ι->void{
		auto& group = _groups.try_emplace( id, Group{move(guid), move(slug), {}} ).first->second;
		for( auto& member : members ){
			if( !group.Nodes.contains(member.Index) )
				AddNode( group, move(member) );
		}
	}
	α GatewayServer::Namespace( const ServerCnnctnNK& slug, sv remote )ι->UA_UInt16{
		return UA_Server_addNamespace( _ua->Ptr(), NamespaceUri(slug, remote).c_str() );//the index it has, or a new one.
	}
	//The identifier alone, `i=6042`, for a node its source hasn't described:  its browse and display name until the path comes.
	Ω identifier( const NodeId& node )ι->string{
		let text = node.ToString();
		let p = text.find( ';' );
		return p==string::npos ? text : text.substr( p+1 );
	}
	//The type served (spec *Address space*, HistNodeAttributes::DataType):  the row's, a namespace-0 type, else BaseDataType until the source has said.
	Ω dataType( const optional<ExNodeId>& type )ι->UA_NodeId{
		if( type && type->nodeId.namespaceIndex==0 && (!type->namespaceUri.length || ToSV(type->namespaceUri)==_ns0Uri) )
			return type->nodeId;
		return UA_NODEID_NUMERIC( 0, UA_NS0ID_BASEDATATYPE );
	}
	α GatewayServer::AddNode( Group& group, HistMember member )ι->void{
		if( !_connections.contains(group.Slug) ){
			WARN( "[{}]Node {} '{}' isn't served:  connection '{}' has no folder.", group.Guid, member.Index, member.Node.ToString(), group.Slug );
			return;
		}
		try{
			auto& ua = *_ua->Ptr();
			let& attributes = member.Attributes;
			let path = attributes.Path ? std::span<const HistPathSegment>{ *attributes.Path } : std::span<const HistPathSegment>{};
			NodeId parent{ FolderId(group.Slug) };
			for( uint i=0; i+1<path.size(); ++i ){//the folders along the way, each once however many members it holds.
				NodeId folder{ PathFolderId(group.Slug, path.subspan(0, i+1)) };
				UA_ObjectAttributes folderAttributes = UA_ObjectAttributes_default;
				folderAttributes.displayName = text( path[i].Name );
				let sc = UA_Server_addObjectNode( &ua, folder, parent, UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QualifiedName{Namespace(group.Slug, path[i].Uri), ToUV(path[i].Name)}, UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE), folderAttributes, nullptr, nullptr );
				if( sc && sc!=UA_STATUSCODE_BADNODEIDEXISTS )
					throw UAException{ sc, Ƒ("folder '{}'", path[i].Name) };
				parent = move( folder );
			}
			let ns = Namespace( group.Slug, ToSV(member.Node.namespaceUri) );
			NodeId id{ member.Node.nodeId };
			id.namespaceIndex = ns;
			let name = path.size() ? path.back().Name : identifier( id );
			const UA_QualifiedName browseName{ path.size() ? Namespace(group.Slug, path.back().Uri) : ns, ToUV(name) };
			UA_VariableAttributes variable = UA_VariableAttributes_default;
			variable.displayName = text( attributes.DisplayName ? *attributes.DisplayName : name );
			if( attributes.Description )
				variable.description = text( *attributes.Description );
			variable.dataType = dataType( attributes.DataType );
			variable.valueRank = attributes.ValueRank.value_or( UA_VALUERANK_ANY );
			vector<UA_UInt32> dimensions;//UA_UInt32, not uint32, which is wider on Linux.
			if( attributes.ArrayDimensions ){
				dimensions.assign( attributes.ArrayDimensions->begin(), attributes.ArrayDimensions->end() );
				variable.arrayDimensions = dimensions.data();
				variable.arrayDimensionsSize = dimensions.size();
			}
			variable.accessLevel = attributes.AccessLevel.value_or( UA_ACCESSLEVELMASK_READ ) | UA_ACCESSLEVELMASK_HISTORYREAD;//HistoryWrite with Phase 4, Write kept for Phase 5.
			variable.historizing = true;//every served node is a group member.
			variable.minimumSamplingInterval = std::chrono::duration<double,std::milli>{ member.Thresholds.MinTimeInterval }.count();//the collector's rate is the ceiling (spec *Live values*).
			UAε( UA_Server_addVariableNode(&ua, id, parent, UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), browseName, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), variable, nullptr, nullptr) );
			NodeId configuration{ Opc::Server::HAConfiguration::Add(ua, id) };
			Opc::Server::HAConfiguration::Publish( ua, configuration, member.Thresholds );
			_access->Add( id, group.Guid );
			let index = member.Index;
			group.Nodes.insert_or_assign( index, Served{move(member), move(id), move(configuration)} );
		}
		catch( const std::exception& e ){
			ERR( "[{}]Could not serve node {} '{}':  {}", group.Guid, member.Index, member.Node.ToString(), e.what() );
		}
	}
	//The HA Configuration first:  HasHistoricalConfiguration isn't hierarchical, so the variable's delete wouldn't take it.  Then the
	//variable, and the folders it leaves empty.
	α GatewayServer::RemoveNode( Group& group, flat_map<HistGroupNodePK,Served>::iterator served )ι->void{
		let& s = served->second;
		_access->Remove( s.Id );
		for( let& [node, what] : {std::pair<const NodeId&,sv>{s.Configuration, "HA Configuration"}, std::pair<const NodeId&,sv>{s.Id, "variable"}} ){
			if( let sc = UA_Server_deleteNode(_ua->Ptr(), node, true); sc )
				WARN( "[{}]Could not remove node {}'s {}:  {}", group.Guid, served->first, what, UAException::Message(sc) );
		}
		if( s.Member.Attributes.Path && s.Member.Attributes.Path->size()>1 )
			Prune( group.Slug, std::span<const HistPathSegment>{*s.Member.Attributes.Path}.first(s.Member.Attributes.Path->size()-1) );
		group.Nodes.erase( served );
	}
	α GatewayServer::RemoveGroup( flat_map<HistGroupPK,Group>::iterator group )ι->flat_map<HistGroupPK,Group>::iterator{
		while( group->second.Nodes.size() )
			RemoveNode( group->second, group->second.Nodes.begin() );
		return _groups.erase( group );
	}
	α GatewayServer::Prune( const ServerCnnctnNK& slug, std::span<const HistPathSegment> parents )ι->void{
		for( ; parents.size(); parents = parents.first(parents.size()-1) ){
			const NodeId folder{ PathFolderId(slug, parents) };
			UA_BrowseDescription description; UA_BrowseDescription_init( &description );
			description.nodeId = folder;//a view:  not cleared.
			description.browseDirection = UA_BROWSEDIRECTION_FORWARD;
			description.referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HIERARCHICALREFERENCES );
			description.includeSubtypes = true;
			auto found = UA_Server_browse( _ua->Ptr(), 1, &description );
			let empty = found.statusCode==UA_STATUSCODE_GOOD && !found.referencesSize;
			UA_BrowseResult_clear( &found );
			if( !empty )
				break;
			if( let sc = UA_Server_deleteNode(_ua->Ptr(), folder, true); sc )
				WARN( "Could not remove the empty folder '{}/{}':  {}", slug, parents.back().Name, UAException::Message(sc) );
		}
	}
	α GatewayServer::Rewrite( Served& served, const HistNodeAttributes& attributes )ι->void{
		auto& ua = *_ua->Ptr();
		let& id = served.Id;
		let warn = [&]( sv what, UA_StatusCode sc ){ if( sc ) WARN( "Could not rewrite '{}'s {}:  {}", id.ToString(), what, UAException::Message(sc) ); };
		if( attributes.DisplayName )
			warn( "DisplayName", UA_Server_writeDisplayName(&ua, id, text(*attributes.DisplayName)) );
		if( attributes.Description )
			warn( "Description", UA_Server_writeDescription(&ua, id, text(*attributes.Description)) );
		warn( "DataType", UA_Server_writeDataType(&ua, id, dataType(attributes.DataType)) );
		if( attributes.ValueRank )
			warn( "ValueRank", UA_Server_writeValueRank(&ua, id, *attributes.ValueRank) );
		if( attributes.ArrayDimensions ){
			vector<UA_UInt32> dimensions{ attributes.ArrayDimensions->begin(), attributes.ArrayDimensions->end() };
			UA_Variant value; UA_Variant_setArray( &value, dimensions.data(), dimensions.size(), &UA_TYPES[UA_TYPES_UINT32] );
			warn( "ArrayDimensions", UA_Server_writeArrayDimensions(&ua, id, value) );
		}
		if( attributes.AccessLevel )
			warn( "AccessLevel", UA_Server_writeAccessLevel(&ua, id, *attributes.AccessLevel | UA_ACCESSLEVELMASK_HISTORYREAD) );
	}
}