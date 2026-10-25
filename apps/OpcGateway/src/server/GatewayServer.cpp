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
#include "GatewayNodeAccess.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Server };
	constexpr sv _root{ "/gateway/server" };
	using Opc::Server::UASettings;
	using Opc::Server::UAServer;
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

	α GatewayServer::FolderId( const ServerCnnctnNK& slug )ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(Opc::Server::HAConfiguration::OwnNamespace, slug.c_str()) }; }
	α GatewayServer::ConnectionId( const ServerCnnctnNK& slug )ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(Opc::Server::HAConfiguration::OwnNamespace, Ƒ("{}/_Connection", slug).c_str()) }; }
	α GatewayServer::VariableId( const ServerCnnctnNK& slug, sv name )ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(Opc::Server::HAConfiguration::OwnNamespace, Ƒ("{}/_Connection/{}", slug, name).c_str()) }; }

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
			lg _{ server->_mutex };
			server->_ua = mu<UAServer>( server->_settings, AppClient(), ms<GatewayNodeAccess>(), nullptr );
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
			for( auto& f : server->_held )
				f();
			server->_held.clear();
			server->_ua->Run();
		}
		catch( std::exception& ){
			Stop();
			throw;
		}
		INFOT( ELogTags::App, "OPC UA endpoint at {} - {} connection folder(s).", server->Url(), server->_connections.size() );
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
	//The folder alone:  open62541 deletes the hierarchical children nothing else references with it (buildDeleteNodeSet).
	α GatewayServer::RemoveFolder( const ServerCnnctnNK& slug )ι->void{
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
}