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
#include "ServedValues.h"

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
		if( let& source = path.back().Id; source )
			return Mapped( slug, *source );
		string id{ slug };
		for( let& segment : path )
			id += Ƒ( "/{}", segment.Name );
		return NodeId{ UA_NODEID_STRING_ALLOC(OwnNamespace, id.c_str()) };
	}
	α GatewayServer::NamespaceUri( const ServerCnnctnNK& slug, sv remote )ι->string{
		let prefix = Ƒ( "urn:jde:{}:", slug );
		return remote.starts_with( prefix ) ? string{ remote } : prefix+string{ remote };
	}

	α GatewayServer::Listen( const Crypto::CryptoSettings& processSsl )ε->void{
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
		//Heard before the groups load and the rows are read:  a row the hooks write, a client the registry gains and a group's change
		//from here on post to the endpoint's thread, and Serial holds them until Start has the folders.  A member's values are kept
		//from its first, the hook that tells of it coming before its item.
		{
			lg _{ _instanceMutex };
			_instance = server;
		}
		wp<GatewayServer> weak{ server };
		UAClient::SetConnectionListener( [weak]( const ConnectionChange& change ){
			if( auto p = weak.lock(); p )
				p->OnChange( change );
		} );
		HistHost::Instance().Listen( server );
	}
	α GatewayServer::Start()ε->void{
		auto server = Instance();
		if( !server )
			return;
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
			for( let& host : hosts ){
				auto members = host->HistMembers();
				for( let& member : members )//a restart's, of hosts loaded before it listened;  else GroupLoaded joined them.
					server->_values->Join( host->Id(), member.Index );
				server->Serve( host->Id(), host->Guid(), host->Slug(), move(members) );
			}
			//Each group's array as its collector last read it:  one read before a restart's Listen told no one, and a stable connection
			//reads it again only on a return.  Seeded only where a read hasn't set it, which is the later.
			for( let& host : hosts ){
				auto uris = host->NamespaceUris();
				server->Register( host->Slug(), uris );
				server->_values->SeedUris( host->Id(), move(uris) );
			}
			for( auto& f : server->_held )
				f();
			server->_held.clear();
			server->_ua->Run();
		}
		catch( std::exception& ){
			Stop();
			throw;
		}
		uint connections{}, nodes{};
		{
			lg _{ server->_mutex };
			connections = server->_connections.size();
			for( let& [_, group] : server->_groups )
				nodes += group.Nodes.size();
		}
		INFOT( ELogTags::App, "OPC UA endpoint at {} - {} connection folder(s), {} served node(s).", server->Url(), connections, nodes );
	}
	GatewayServer::GatewayServer( UASettings settings )ι:
		_values{ ms<ServedValues>() },
		_settings{ move(settings) }
	{}
	GatewayServer::~GatewayServer(){ _work.reset(); }
	α GatewayServer::Stop()ι->void{
		sp<GatewayServer> server;
		{
			lg _{ _instanceMutex };
			server = move( _instance );
		}
		if( !server )
			return;
		UAClient::SetConnectionListener( nullptr );
		HistHost::Instance().Listen( {} );
		up<UAServer> ua;
		{
			lg _{ server->_mutex };
			ua = move( server->_ua );
		}
		ua.reset();//outside the lock:  it joins the server's thread, and a posted change may be waiting on the lock to find no server.
		server->_work.reset();
		server->_thread.join();//what's still posted runs first, finds no server and is held, then dropped.
		lg _{ server->_mutex };
		server->_held.clear();
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

	//One thread of its own, not the executor's many:  a slug's Lost and its next Activated, or a purge and the row made again under
	//its slug, apply in the order they were posted, and a wait there for the service lock holds no executor thread.  Stop runs what's
	//still queued, with _ua null, so it's held, and drops it.
	α GatewayServer::Serial( absl::AnyInvocable<void()> f )ι->void{
		boost::asio::post( _context, [self=shared_from_this(), f=std::move(f)]() mutable {
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
	//A member is the cache's from the hook on, before its item exists, the tap storing its every value;  served or not.
	α GatewayServer::GroupLoaded( const HistGroupHost& host )ι->void{
		auto nodes = host.HistMembers();
		for( let& node : nodes )
			_values->Join( host.Id(), node.Index );
		Serial( [this, id=host.Id(), guid=host.Guid(), slug=host.Slug(), nodes=move(nodes)]() mutable { Serve( id, move(guid), move(slug), move(nodes) ); } );
	}
	α GatewayServer::GroupRemoved( HistGroupPK group )ι->void{
		_values->Forget( group );
		Serial( [this, group]{
			if( auto p = _groups.find(group); p!=_groups.end() )
				RemoveGroup( p );
		} );
	}
	//The attributes the host has when the add runs, not the hook's row's:  a describe that landed first stored them before its
	//NodeDescribed, which found no node to update.
	α GatewayServer::NodeJoined( const HistGroupHost& host, HistMember member )ι->void{
		_values->Join( host.Id(), member.Index );
		Serial( [this, host=host.shared_from_this(), member=move(member)]() mutable {
			if( auto attributes = host->Attributes(member.Index) )
				member.Attributes = move( *attributes );
			Serve( host->Id(), host->Guid(), host->Slug(), {move(member)} );
		} );
	}
	α GatewayServer::NodeLeft( HistGroupPK group, HistGroupNodePK index )ι->void{
		_values->Leave( group, index );
		Serial( [this, group, index]{
			auto g = _groups.find( group );
			if( g==_groups.end() )
				return;
			RemoveNode( g->second, index );
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
			if( let& current = p->second.Member.Attributes; current.Path==attributes.Path && current.ValueRank==attributes.ValueRank && current.ArrayDimensions==attributes.ArrayDimensions ){
				Rewrite( p->second, attributes );
				p->second.Member.Attributes = move( attributes );
				return;
			}
			//The node moves - first described, or its source rearranged - or changes shape, which open62541 can't write in place:  ValueRank
			//and ArrayDimensions each refuse a value the other's current one doesn't fit.
			HistMember member{ p->second.Member };
			member.Attributes = move( attributes );
			if( RemoveNode(g->second, index) )//else it stays where it is until the next describe.
				AddNode( g->second, move(member) );
		} );
	}
	α GatewayServer::ValueChanged( HistGroupPK group, HistGroupNodePK index, const UA_DataValue& value )ι->void{ _values->Store( group, index, value ); }
	α GatewayServer::NodesLost( HistGroupPK group, const vector<HistGroupNodePK>& indexes )ι->void{ _values->Lose( group, indexes ); }
	//The array to the cache now, under the host's lock, ahead of the values it brings;  its namespaces posted.
	α GatewayServer::NamespacesRead( HistGroupPK group, const ServerCnnctnNK& slug, const vector<string>& uris )ι->void{
		_values->SetUris( group, uris );
		Serial( [this, slug, uris]{ Register( slug, uris ); } );
	}
	//Every URI of the array gets its namespace, so each index a value can carry maps (spec *Namespaces*);  0's, and a source that lists
	//it again, stay 0.
	α GatewayServer::Register( const ServerCnnctnNK& slug, const vector<string>& uris )ι->void{
		if( !_connections.contains(slug) )//a slug with no row - a test connect's.
			return;
		for( uint i=1; i<uris.size(); ++i ){
			if( uris[i]==_ns0Uri )
				_values->Index( slug, uris[i], 0 );
			else
				Namespace( slug, uris[i] );
		}
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
		auto& group = _groups.try_emplace( id, Group{id, move(guid), move(slug), {}} ).first->second;
		for( auto& member : members ){
			if( !group.Nodes.contains(member.Index) )
				AddNode( group, move(member) );
		}
	}
	α GatewayServer::Namespace( const ServerCnnctnNK& slug, sv remote )ι->UA_UInt16{
		auto& known = _namespaces[slug];
		if( auto p = known.find(remote); p!=known.end() )
			return p->second;
		let index = UA_Server_addNamespace( _ua->Ptr(), NamespaceUri(slug, remote).c_str() );//the index it has, or a new one.
		known.emplace( remote, index );
		_values->Index( slug, string{remote}, index );
		return index;
	}
	α GatewayServer::Mapped( const ServerCnnctnNK& slug, const ExNodeId& source )ι->NodeId{
		NodeId y{ source.nodeId };
		y.namespaceIndex = Namespace( slug, ToSV(source.namespaceUri) );
		return y;
	}
	Ω nodeClass( UA_Server& ua, const NodeId& node )ι->optional<UA_NodeClass>{
		UA_NodeClass y;
		return UA_Server_readNodeClass( &ua, node, &y ) ? nullopt : optional<UA_NodeClass>{ y };
	}
	//A node and everything under it by forward HierarchicalReferences:  an HA Configuration, its properties and AggregateConfiguration's.
	Ω subtree( UA_Server& ua, NodeId root )ι->vector<NodeId>{
		vector<NodeId> y{ move(root) };
		for( uint i=0; i<y.size(); ++i ){
			UA_BrowseDescription description; UA_BrowseDescription_init( &description );
			description.nodeId = y[i];//a view:  not cleared.
			description.browseDirection = UA_BROWSEDIRECTION_FORWARD;
			description.referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HIERARCHICALREFERENCES );
			description.includeSubtypes = true;
			auto found = UA_Server_browse( &ua, 0, &description );
			vector<NodeId> next;
			for( size_t r=0; r<found.referencesSize; ++r )
				next.emplace_back( found.references[r].nodeId.nodeId );
			UA_BrowseResult_clear( &found );
			std::ranges::move( next, std::back_inserter(y) );
		}
		return y;
	}
	//How a node hangs off its parent:  a served variable's component, as on the source, else organized by a folder.
	Ω reference( bool underVariable )ι->UA_NodeId{ return UA_NODEID_NUMERIC( 0, underVariable ? UA_NS0ID_HASCOMPONENT : UA_NS0ID_ORGANIZES ); }
	//The node's forward HierarchicalReferences, or the browse's status:  BadNodeIdUnknown for one never made.
	Ω children( UA_Server& ua, const NodeId& node )ι->std::expected<size_t,UA_StatusCode>{
		UA_BrowseDescription description; UA_BrowseDescription_init( &description );
		description.nodeId = node;//a view:  not cleared.
		description.browseDirection = UA_BROWSEDIRECTION_FORWARD;
		description.referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HIERARCHICALREFERENCES );
		description.includeSubtypes = true;
		auto found = UA_Server_browse( &ua, 1, &description );
		let status = found.statusCode;
		let size = found.referencesSize;
		UA_BrowseResult_clear( &found );
		return status ? std::expected<size_t,UA_StatusCode>{ std::unexpect, status } : size;
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
	Ω name( const HistNodeAttributes& attributes, const NodeId& id )ι->string{ return attributes.Path && attributes.Path->size() ? attributes.Path->back().Name : identifier( id ); }
	Ω accessLevel( const HistNodeAttributes& attributes )ι->UA_Byte{ return attributes.AccessLevel.value_or( UA_ACCESSLEVELMASK_READ ) | UA_ACCESSLEVELMASK_HISTORYREAD; }//HistoryWrite with Phase 4, Write kept for Phase 5.
	//Mapped before it's added and unmapped only once it's gone (Unserve), so no session meets the node outside its group's rights;  a
	//throw part way undoes what was added.  Its value is its member's in the mirror's cache, kept there from the first, so the first
	//read answers it.
	α GatewayServer::AddNode( Group& group, HistMember member )ι->void{
		if( !_connections.contains(group.Slug) ){
			WARN( "[{}]Node {} '{}' isn't served:  connection '{}' has no folder.", group.Guid, member.Index, member.Node.ToString(), group.Slug );
			return;
		}
		auto& ua = *_ua->Ptr();
		let& attributes = member.Attributes;
		let path = attributes.Path ? std::span<const HistPathSegment>{ *attributes.Path } : std::span<const HistPathSegment>{};
		NodeId id{ Mapped(group.Slug, member.Node) };
		if( !_access->Add(id, group.Guid) ){
			ERR( "[{}]Node {} '{}' isn't served:  {} already is, for group {}.", group.Guid, member.Index, member.Node.ToString(), id.ToString(), _access->Guid(id).value_or("") );
			return;
		}
		if( !_values->Add(id, group.Id, member.Index, group.Slug) ){
			ERR( "[{}]Node {} '{}' isn't served:  {} has a value source already.", group.Guid, member.Index, member.Node.ToString(), id.ToString() );
			_access->Remove( id );
			return;
		}
		//A folder standing in for this node, its children served first:  they come out, the folder going with the last, and back under the variable.
		vector<std::pair<HistGroupPK,HistMember>> lifted;
		if( nodeClass(ua, id)==UA_NODECLASS_OBJECT )
			lifted = Lift( group.Slug, id );
		bool added{};
		NodeId configuration;
		vector<NodeId> configured;
		try{
			NodeId parent{ FolderId(group.Slug) };
			bool underVariable{};
			for( uint i=0; i+1<path.size(); ++i ){//the nodes along the way, each folder once however many members it holds.
				NodeId folder{ PathFolderId(group.Slug, path.subspan(0, i+1)) };
				UA_ObjectAttributes folderAttributes = UA_ObjectAttributes_default;
				folderAttributes.displayName = text( path[i].Name );
				let sc = UA_Server_addObjectNode( &ua, folder, parent, reference(underVariable), UA_QualifiedName{Namespace(group.Slug, path[i].Uri), ToUV(path[i].Name)}, UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE), folderAttributes, nullptr, nullptr );
				if( sc && sc!=UA_STATUSCODE_BADNODEIDEXISTS )
					throw UAException{ sc, Ƒ("folder '{}'", path[i].Name) };
				underVariable = sc && nodeClass( ua, folder )==UA_NODECLASS_VARIABLE;//there already:  a folder, or a member's variable.
				parent = move( folder );
			}
			let browse = name( attributes, id );
			const UA_QualifiedName browseName{ path.size() ? Namespace(group.Slug, path.back().Uri) : id.namespaceIndex, ToUV(browse) };
			UA_VariableAttributes variable = UA_VariableAttributes_default;
			variable.displayName = text( attributes.DisplayName ? *attributes.DisplayName : browse );
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
			//Unreadable until its callback source is in:  the add stores the type's default as a Good value, and an add with the
			//callback already in refuses a node with no value yet, whose read its type check makes.
			variable.accessLevel = accessLevel( attributes ) & ~UA_ACCESSLEVELMASK_READ;
			variable.historizing = true;//every served node is a group member.
			variable.minimumSamplingInterval = std::chrono::duration<double,std::milli>{ member.Thresholds.MinTimeInterval }.count();//the collector's rate is the ceiling (spec *Live values*).
			UAε( UA_Server_addVariableNode(&ua, id, parent, reference(underVariable), browseName, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), variable, _values.get(), nullptr) );
			added = true;
			UAε( UA_Server_setVariableNode_callbackValueSource(&ua, id, ServedValues::Source()) );//the context above is what its read finds the cache by.
			UAε( UA_Server_writeAccessLevel(&ua, id, accessLevel(attributes)) );
			configuration = Opc::Server::HAConfiguration::Add( ua, id );
			Opc::Server::HAConfiguration::Publish( ua, configuration, member.Thresholds );
			Opc::Server::HAConfiguration::PublishAggregates( ua, configuration );
			configured = subtree( ua, configuration );//its group's as soon as open62541 has given them ids, which it picks.
			for( let& node : configured )
				_access->Add( node, group.Guid );
			let index = member.Index;
			group.Nodes.insert_or_assign( index, Served{move(member), move(id), move(configuration), move(configured)} );
		}
		catch( const std::exception& e ){
			ERR( "[{}]Could not serve node {} '{}':  {}", group.Guid, member.Index, member.Node.ToString(), e.what() );
			let index = member.Index;
			Served served{ move(member), move(id), move(configuration), move(configured) };
			if( !Unserve(group, served, added) )
				group.Nodes.insert_or_assign( index, move(served) );//still there, and mapped:  its removal tries again.
		}
		Restore( move(lifted) );
	}
	//The HA Configuration first, its nodes unmapped once it's gone, so what's left under the variable is members alone.  Then the
	//variable, unmapped only once it's gone - a failed delete leaves it under its group's rights, not open to every session - and the
	//folders left empty.
	α GatewayServer::Unserve( const Group& group, const Served& served, bool added, bool prune )ι->bool{
		let gone = [&]( const NodeId& node, sv what ){
			let sc = UA_Server_deleteNode( _ua->Ptr(), node, true );
			if( sc && sc!=UA_STATUSCODE_BADNODEIDUNKNOWN )
				WARN( "[{}]Could not remove node {}'s {}:  {}", group.Guid, served.Member.Index, what, UAException::Message(sc) );
			return !sc || sc==UA_STATUSCODE_BADNODEIDUNKNOWN;
		};
		if( !UA_NodeId_isNull(&served.Configuration) && gone(served.Configuration, "HA Configuration") ){
			for( let& node : served.Configured )
				_access->Remove( node );
		}
		//Members under the variable - a property, a structure's component - come out with it, and back under a folder standing in for it.
		auto lifted = added && children( *_ua->Ptr(), served.Id ).value_or( 0 ) ? Lift( group.Slug, served.Id ) : vector<std::pair<HistGroupPK,HistMember>>{};
		let deleted = !added || gone( served.Id, "variable" );
		if( deleted ){
			_access->Remove( served.Id );
			_values->Remove( served.Id );
			if( let& path = served.Member.Attributes.Path; prune && path && path->size()>1 )
				Prune( group.Slug, std::span<const HistPathSegment>{*path}.first(path->size()-1) );
		}
		Restore( move(lifted) );
		return deleted;
	}
	α GatewayServer::RemoveNode( Group& group, HistGroupNodePK index, bool prune )ι->bool{
		auto p = group.Nodes.find( index );
		if( p==group.Nodes.end() )
			return true;
		const Served served{ p->second };//a copy:  Unserve's Lift can rearrange Nodes.
		if( !Unserve(group, served, true, prune) )
			return false;
		group.Nodes.erase( index );
		return true;
	}
	Ω depth( const HistMember& member )ι->uint{ return member.Attributes.Path ? member.Attributes.Path->size() : 0; }
	α GatewayServer::RemoveGroup( flat_map<HistGroupPK,Group>::iterator group )ι->flat_map<HistGroupPK,Group>::iterator{
		auto& nodes = group->second.Nodes;
		vector<std::pair<uint,HistGroupNodePK>> deepest;//a member's children before it, so none is lifted only to come straight back.
		for( let& [index, served] : nodes )
			deepest.emplace_back( depth(served.Member), index );
		std::ranges::sort( deepest, std::greater{} );
		flat_map<NodeId,vector<HistPathSegment>> parents;//each folder that held a member, pruned once they've all gone, not once each.
		for( let& [_, index] : deepest ){
			auto p = nodes.find( index );
			if( p==nodes.end() )
				continue;
			auto path = p->second.Member.Attributes.Path;
			if( RemoveNode(group->second, index, false) && path && path->size()>1 ){
				path->pop_back();
				parents.try_emplace( PathFolderId(group->second.Slug, *path), move(*path) );
			}
		}
		vector<vector<HistPathSegment>> folders;
		for( auto&& [_, path] : parents )
			folders.push_back( move(path) );
		std::ranges::sort( folders, std::greater{}, &vector<HistPathSegment>::size );//the deepest first:  its parent may empty with it.
		for( let& folder : folders )
			Prune( group->second.Slug, folder );
		return nodes.empty() ? _groups.erase( group ) : std::next( group );//a node that stays keeps its group, for the next removal to try.
	}
	α GatewayServer::Lift( const ServerCnnctnNK& slug, const NodeId& node )ι->vector<std::pair<HistGroupPK,HistMember>>{
		vector<std::tuple<uint,HistGroupPK,HistGroupNodePK>> under;
		for( let& [pk, group] : _groups ){
			if( group.Slug!=slug )
				continue;
			for( let& [index, served] : group.Nodes ){
				let& path = served.Member.Attributes.Path;
				for( uint i=0; path && i+1<path->size(); ++i ){
					if( PathFolderId(slug, std::span<const HistPathSegment>{*path}.first(i+1))==node ){
						under.emplace_back( path->size(), pk, index );
						break;
					}
				}
			}
		}
		std::ranges::sort( under, std::greater{} );//deepest first:  none has a child served when it goes.
		vector<std::pair<HistGroupPK,HistMember>> y;
		for( let& [_, pk, index] : under ){
			auto& group = _groups.find( pk )->second;
			auto p = group.Nodes.find( index );
			if( p==group.Nodes.end() )
				continue;
			HistMember member{ p->second.Member };
			if( RemoveNode(group, index) )
				y.emplace_back( pk, move(member) );
		}
		std::ranges::reverse( y );//shallowest first:  a parent back before its children.
		return y;
	}
	α GatewayServer::Restore( vector<std::pair<HistGroupPK,HistMember>> lifted )ι->void{
		for( auto& [pk, member] : lifted ){
			if( auto g = _groups.find(pk); g!=_groups.end() && !g->second.Nodes.contains(member.Index) )
				AddNode( g->second, move(member) );
		}
	}
	α GatewayServer::Prune( const ServerCnnctnNK& slug, std::span<const HistPathSegment> parents )ι->void{
		for( ; parents.size(); parents = parents.first(parents.size()-1) ){
			const NodeId folder{ PathFolderId(slug, parents) };
			let found = children( *_ua->Ptr(), folder );
			if( !found && found.error()==UA_STATUSCODE_BADNODEIDUNKNOWN )
				continue;//never made:  an AddNode that threw before it.
			if( !found || *found || nodeClass(*_ua->Ptr(), folder)!=UA_NODECLASS_OBJECT )
				break;//not empty, or a served variable, which its own member removes.
			if( let sc = UA_Server_deleteNode(_ua->Ptr(), folder, true); sc )
				WARN( "Could not remove the empty folder '{}/{}':  {}", slug, parents.back().Name, UAException::Message(sc) );
		}
	}
	α GatewayServer::Rewrite( Served& served, const HistNodeAttributes& attributes )ι->void{
		auto& ua = *_ua->Ptr();
		let& id = served.Id;
		let warn = [&]( sv what, UA_StatusCode sc ){ if( sc ) WARN( "Could not rewrite '{}'s {}:  {}", id.ToString(), what, UAException::Message(sc) ); };
		warn( "DisplayName", UA_Server_writeDisplayName(&ua, id, text(attributes.DisplayName ? *attributes.DisplayName : name(attributes, id))) );
		warn( "Description", UA_Server_writeDescription(&ua, id, attributes.Description ? text(*attributes.Description) : UA_LocalizedText{}) );
		warn( "DataType", UA_Server_writeDataType(&ua, id, dataType(attributes.DataType)) );
		warn( "AccessLevel", UA_Server_writeAccessLevel(&ua, id, accessLevel(attributes)) );
	}
}