#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/io/json.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/ql.h>
#include "../src/opcInternal.h"
#include "../src/GatewayAppClient.h"
#include "../src/auth/UM.h"
#include "../src/ql/GatewayQL.h"
#include "../src/ql/ServerCnnctnMAwait.h"
#include "utils/helpers.h"

#define let const auto
namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };

	struct ServerCnnctnDBTests : ::testing::Test{
	protected:
		α CrudImpl()ε->Access::ProviderPK;
		α CrudImpl2( ServerCnnctnPK id )ε->Access::ProviderPK;
		α CrudPurge( ServerCnnctnPK id )ε->Access::ProviderPK;
	};

	Ω GetProviderPK( string slug )ε->Access::ProviderPK{
		return BlockAwait( ProviderAwait{slug} );
	}
	Ω GetOpcServers( optional<DB::Key> key=nullopt, bool includeDeleted=false )->vector<ServerCnnctn>{
		return BlockAwait<ServerCnnctnAwait,vector<ServerCnnctn>>( ServerCnnctnAwait{key, includeDeleted} );
	}

	TEST_F( ServerCnnctnDBTests, InsertFailed ){
		TRACE( "InsertFailed::Started" );
		let slug = OpcServerSlug;
		auto jInsert = Json::Parse( Ƒ("{{\"slug\":\"{}\"}}", slug) );
		QL::MutationQL insert{ "createServerConnection", move(jInsert), {}, nullopt, true, QL().Schemas(), false };

		let existingProviderPK = GetProviderPK( slug );
		let existingServer = SelectServerCnnctn( slug );
		let existingOpcPK = existingServer ? existingServer->Id : 0;
		let& table = GetViewPtr( "server_connections" );
		if( !existingOpcPK && !existingProviderPK ){
			auto pk = BlockAwait<CreateServerCnnctnAwait,ServerCnnctnPK>( CreateServerCnnctnAwait{} );
			DS()->ExecuteSync( {Ƒ("delete from {} where server_connection_id='{}'", table->DBName, pk)} ); //InsertFailed checks if failure occurs because exists.
		}
		else{
			if( existingOpcPK )
				DS()->ExecuteSync( {Ƒ("delete from {} where server_connection_id='{}'", table->DBName, existingOpcPK)} ); //InsertFailed checks if failure occurs because exists.
		}
		BlockAwait<TAwait<jvalue>,jvalue>( move(*ServerCnnctnMAwait::ProviderStep(insert, EProviderStep::InsertFailure)) );
		ASSERT_EQ( 0, GetProviderPK(slug) );
	}

	TEST_F( ServerCnnctnDBTests, PurgeFailed ){
		let existingServer = SelectServerCnnctn( OpcServerSlug );
		auto opcPK = existingServer ? existingServer->Id : 0;
		if( !opcPK )
			opcPK = BlockAwait<CreateServerCnnctnAwait,ServerCnnctnPK>( CreateServerCnnctnAwait{} );
		BlockAwait( ProviderMAwait{OpcServerSlug, false} );//BeforePurge mock.

		QL::MutationQL purge{ "purgeServerConnection", { {"id", opcPK} }, {}, nullopt, true, QL().Schemas(), false };
		BlockAwait<TAwait<jvalue>,jvalue>( move(*ServerCnnctnMAwait::ProviderStep(purge, EProviderStep::PurgeFailure)) );
		let providerPK = GetProviderPK( OpcServerSlug );
		ASSERT_NE( 0, providerPK );
		PurgeServerCnnctn();
	}

	α ServerCnnctnDBTests::CrudImpl()ε->Access::ProviderPK{
		let existingServer = SelectServerCnnctn( OpcServerSlug );
		let existingOpcPK = existingServer ? existingServer->Id : 0;
		if( existingOpcPK )
			PurgeServerCnnctn( existingOpcPK );
		let createdId = BlockAwait<CreateServerCnnctnAwait,ServerCnnctnPK>( CreateServerCnnctnAwait{} );
		let selectAll = "serverConnections{ id name attributes created updated deleted slug description certificateUri isDefault url opcSessions{ count } }";
		let selectAllJson = QL().QuerySync<jarray>( selectAll, {}, {UserPK::System} );
		TRACET( _tags, "selectAllJson={}", serialize(selectAllJson) );
		let id = Json::AsNumber<ServerCnnctnPK>( Json::AsObject(selectAllJson[0]), "id" );
		THROW_IF( !Json::FindNumberPath<uint32>(Json::AsObject(selectAllJson[0]), "opcSessions/count"), "opcSessions{{count}} missing from '{}'", serialize(selectAllJson[0]) ); //live total grafted onto the DB row; 0 is fine here.
		THROW_IF( createdId!=id, "createdId={} id={}", createdId, id );
		return CrudImpl2( id );
	}

	α ServerCnnctnDBTests::CrudImpl2( ServerCnnctnPK id )ε->Access::ProviderPK{
		auto conn = SelectServerCnnctn( OpcServerSlug );
		THROW_IF( conn->Id!=id, "id={} readJson={}", id, serialize(conn->ToJson()) );
		let slug = conn->Slug;

		let providerId = BlockAwait( ProviderAwait{slug} );
		THROW_IF( providerId==0, "providerId==0" );

		let description = "new description";
		let update = Ƒ( "mutation updateServerConnection( id:{}, description:\"{}\" ) }}", id, description );
		let updateJson = QL().QuerySync<jvalue>( update, {}, {UserPK::System} );
		TRACET( _tags, "updateJson={}", serialize(updateJson) );
		let updated = SelectServerCnnctn( id );
		THROW_IF( updated->Description!=description, "description={} updated={}", description, serialize(updated->ToJson()) );

		//slug is the connection's identity - the url segment, the key the live UAClient sits under - so the meta marks it
		//updateable:false.  createUpdate skips the column, the statement has nothing to set, and the mutation is refused.
		let rename = Ƒ( "mutation updateServerConnection( id:{}, slug:\"renamed\" ) }}", id );
		bool refused{};
		try{ QL().QuerySync<jvalue>( rename, {}, {UserPK::System} ); }
		catch( const std::exception& e ){ refused = true; TRACET( _tags, "rename refused: {}", e.what() ); }
		THROW_IF( !refused, "a slug rename was accepted" );
		let renamed = SelectServerCnnctn( id );
		THROW_IF( renamed->Slug!=slug, "slug='{}' should still be '{}'", renamed->Slug, slug );

		let del = Ƒ( "deleteServerConnection(\"id\":{})", id );
		let deleteJson = QL().QuerySync<jvalue>( del, {}, {UserPK::System} );
		TRACET( _tags, "deleted={}", serialize(deleteJson) );
		conn = SelectServerCnnctn( id );
		THROW_IF( !conn->Deleted, "deleted failed" );

		return CrudPurge( id );
	}

	α ServerCnnctnDBTests::CrudPurge( ServerCnnctnPK id )ε->Access::ProviderPK{
		BlockAwait<PurgeServerCnnctnAwait,uint>( PurgeServerCnnctnAwait{ id } );
		let opcServers = GetOpcServers( id, true );
		THROW_IF( opcServers.size(), "Purge Failed" );
		return GetProviderPK( OpcServerSlug );
	}

	//install-issues #48:  " eng-test" was accepted - provider " eng-test", certificate `OpcHub. eng-test.pem`, a login that had to carry
	//the space.  The slug is refused before the insert, so neither the row nor its provider is made.
	TEST_F( ServerCnnctnDBTests, SlugRefused ){
		for( let slug : {" eng-test", "eng-test ", "eng test", "eng\\\\test", "-eng", ""} ){
			let create = Ƒ( "mutation createServerConnection( slug:\"{}\", name:\"Slug test\", url:\"opc.tcp://127.0.0.1:4840\", isDefault:false ){{id}}", slug );
			bool refused{};
			try{ QL().QuerySync<jvalue>( create, {}, {UserPK::System} ); }
			catch( const std::exception& e ){ refused = true; TRACET( _tags, "'{}' refused: {}", slug, e.what() ); }
			EXPECT_TRUE( refused ) << "slug '" << slug << "'";
			if( !*slug )
				continue;//"" is the default connection's key to the lookups below
			EXPECT_FALSE( SelectServerCnnctn(string{slug}) ) << "slug '" << slug << "'";
			EXPECT_EQ( 0, GetProviderPK(slug) ) << "slug '" << slug << "'";
		}
	}

	//install-issues #51:  a delete is soft and keeps the slug.  A create on it reached the insert, failed on the UNIQUE constraint
	//with a raw sqlite error, and the failure hook then purged the deleted row's provider.
	TEST_F( ServerCnnctnDBTests, SlugReuse ){
		const string slug{ "reuse-test" };
		if( auto stale = GetOpcServers( DB::Key{slug}, true ); stale.size() )
			PurgeServerCnnctn( stale.front().Id );
		let create = [&]()->optional<string>{
			try{ BlockAwait( CreateServerCnnctnAwait{slug, "opc.tcp://127.0.0.1:4840", "urn:reuse-test"} ); }
			catch( const std::exception& e ){ return e.what(); }
			return nullopt;
		};
		ASSERT_FALSE( create() );
		let id = SelectServerCnnctn( slug )->Id;
		let providerPK = GetProviderPK( slug );
		ASSERT_NE( 0, providerPK );

		auto error = create();
		ASSERT_TRUE( error );
		EXPECT_NE( string::npos, error->find("in use") ) << *error;

		QL().QuerySync<jvalue>( Ƒ("deleteServerConnection(\"id\":{})", id), {}, {UserPK::System} );
		error = create();
		ASSERT_TRUE( error );
		EXPECT_NE( string::npos, error->find("deleted connection") ) << *error;
		EXPECT_EQ( providerPK, GetProviderPK(slug) );

		PurgeServerCnnctn( id );
		EXPECT_FALSE( create() );
		if( let recreated = SelectServerCnnctn(slug); recreated )
			PurgeServerCnnctn( recreated->Id );
	}

	//qlhook-removal #2:  the provider step reuses a provider with no row (the seed ships one), and ran before the engine's own
	//checks - a create the engine refused then purged that provider on its way out.
	TEST_F( ServerCnnctnDBTests, EngineRefusalKeepsProvider ){
		const string slug{ "engine-refused" };
		PurgeServerCnnctn( slug );
		BlockAwait( ProviderMAwait{DB::Key{slug}, true} );
		let providerPK = GetProviderPK( slug );
		ASSERT_NE( 0, providerPK );
		absl::Cleanup purge = [&]{ BlockAwait( ProviderMAwait{DB::Key{slug}, false} ); };
		bool refused{};
		try{ QL().QuerySync<jvalue>( Ƒ("mutation createServerConnection( slug:\"{}\", name:\"Engine refused\", isDefault:false ){{id}}", slug), {}, {UserPK::System} ); }//no url
		catch( const std::exception& e ){ refused = true; TRACET( _tags, "refused: {}", e.what() ); }
		EXPECT_TRUE( refused );
		EXPECT_FALSE( SelectServerCnnctn(slug) );
		EXPECT_EQ( providerPK, GetProviderPK(slug) );
	}

	//qlhook-removal #5 (ql-review3 #25):  ServerCnnctnMAwait authorizes before its provider step.  A caller the enforced resource
	//grants nothing is refused with the provider untouched:  a purge's step would have taken it first, and its failure step made it
	//again under a new id;  a create's would have made one for a row never written.
	TEST_F( ServerCnnctnDBTests, UnauthorizedLeavesTheProvider ){
		const string slug{ "unauthorized-test" }, newSlug{ "unauthorized-new" };
		PurgeServerCnnctn( slug );
		BlockAwait( CreateServerCnnctnAwait{slug, "opc.tcp://127.0.0.1:4840", "urn:unauthorized-test"} );
		let id = SelectServerCnnctn( slug )->Id;
		absl::Cleanup purge = [id]{ PurgeServerCnnctn( id ); };
		let providerPK = GetProviderPK( slug );
		ASSERT_NE( 0, providerPK );

		EnforceResource( "gateway", "serverConnections", true );
		absl::Cleanup unenforce = []{ EnforceResource( "gateway", "serverConnections", false ); };
		let caller = AppClient()->UserPK();//granted nothing on serverConnections
		let refused = [&]( string mutation ){
			try{ QL().QuerySync<jvalue>( move(mutation), {}, {caller} ); }
			catch( const std::exception& e ){ TRACET( _tags, "refused: {}", e.what() ); return true; }
			return false;
		};
		EXPECT_TRUE( refused(Ƒ("mutation purgeServerConnection( id:{} )", id)) );
		EXPECT_TRUE( SelectServerCnnctn(id) ) << "the row was purged anyway";
		EXPECT_EQ( providerPK, GetProviderPK(slug) ) << "the provider step ran before the refusal";
		EXPECT_TRUE( refused(Ƒ("mutation createServerConnection( slug:\"{}\", name:\"Unauthorized\", url:\"opc.tcp://127.0.0.1:4840\", isDefault:false ){{id}}", newSlug)) );
		EXPECT_EQ( 0, GetProviderPK(newSlug) ) << "the provider step ran before the refusal";
	}

	TEST_F( ServerCnnctnDBTests, Crud ){
		try{
			auto providerPK = CrudImpl();
			ASSERT_EQ( 0, providerPK );
		}
		catch( const runtime_error& e ){
			ASSERT_DESC( false, e.what() );
		}
	}

	//#413 (docs/clients/spec.md *Connection settings*):  the seven columns round-trip through the insert proc, the QL's select, the
	//row ctor (ServerCnnctnAwait) and an update, null where unset;  an update clears one with null.
	TEST_F( ServerCnnctnDBTests, SettingsRoundTrip ){
		const string slug{ "settings-test" };
		PurgeServerCnnctn( slug );
		let create = Ƒ( "mutation createServerConnection( slug:\"{}\", name:\"Settings test\", url:\"opc.tcp://127.0.0.1:4840\", certificateUri:\"urn:settings\", isDefault:false, securityMode:\"Sign\", securityPolicy:\"http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256\", sessionTimeout:60000, requestTimeout:2000, pingInterval:15000, ttl:120000, locale:\"en-US,de\" ){{id}}", slug );
		let id = Json::AsNumber<ServerCnnctnPK>( QL().QuerySync<jobject>(create, {}, {UserPK::System}), "id" );
		absl::Cleanup purge = [id]{ PurgeServerCnnctn( id ); };
		let check = [&]( const ServerCnnctn& row, sv where ){
			EXPECT_EQ( row.SecurityMode, "Sign" ) << where;
			EXPECT_EQ( row.SecurityPolicy, "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256" ) << where;
			EXPECT_EQ( row.SessionTimeout, optional<uint32>{60000} ) << where;
			EXPECT_EQ( row.RequestTimeout, optional<uint32>{2000} ) << where;
			EXPECT_EQ( row.PingInterval, optional<uint32>{15000} ) << where;
			EXPECT_EQ( row.Ttl, optional<uint32>{120000} ) << where;
			EXPECT_EQ( row.Locale, "en-US,de" ) << where;
			EXPECT_EQ( row.EffectiveMode(), ServerCnnctn::ModeSign ) << where;
		};
		check( *SelectServerCnnctn(id), "ql" );//the json ctor
		check( GetOpcServers(DB::Key{id}, false).front(), "row" );//the row ctor, what a client is built from
		QL().QuerySync<jvalue>( Ƒ("mutation updateServerConnection( id:{}, securityMode:\"SignAndEncrypt\", securityPolicy:null, sessionTimeout:null, ttl:30000 )", id), {}, {UserPK::System} );
		let updated = GetOpcServers( DB::Key{id}, false ).front();
		EXPECT_EQ( updated.SecurityMode, "SignAndEncrypt" );
		EXPECT_TRUE( updated.SecurityPolicy.empty() );
		EXPECT_FALSE( updated.SessionTimeout );
		EXPECT_EQ( updated.RequestTimeout, optional<uint32>{2000} );
		EXPECT_EQ( updated.Ttl, optional<uint32>{30000} );
		let blank = GetOpcServers( DB::Key{OpcServerSlug}, false );
		if( blank.size() ){//the suite's default row sets none of them
			EXPECT_TRUE( blank.front().SecurityMode.empty() );
			EXPECT_FALSE( blank.front().PingInterval );
			EXPECT_EQ( blank.front().EffectiveMode(), ServerCnnctn::ModeSignAndEncrypt );//the floor its certificateUri sets
		}
	}

	//The save rules (ServerCnnctn::SettingsError) at the insert, and at an update over the merged row:  Sign without a certificate
	//URI, a mode pinned on a row with none, a row losing the uri its mode needs, an unknown or padded mode or policy, a contradiction
	//between the two, a duration of 0 where 0 is no default, and one over SQL Server's int.  None reaches the table.
	TEST_F( ServerCnnctnDBTests, SettingsRefused ){
		const string slug{ "settings-refused" };
		PurgeServerCnnctn( slug );
		let create = [&]( sv args )->optional<string>{
			try{ QL().QuerySync<jvalue>( Ƒ("mutation createServerConnection( slug:\"{}\", name:\"Settings refused\", url:\"opc.tcp://127.0.0.1:4840\", isDefault:false{} ){{id}}", slug, args), {}, {UserPK::System} ); }
			catch( const std::exception& e ){ return e.what(); }
			return nullopt;
		};
		for( let [args, expected] : vector<std::pair<string,string>>{
			{ ", securityMode:\"Sign\"", "certificate URI" },
			{ ", securityMode:\"SignAndEncrypt\"", "certificate URI" },
			{ ", securityPolicy:\"http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256\"", "certificate URI" },
			{ ", securityMode:\"Encrypt\"", "not one of None, Sign or SignAndEncrypt" },
			{ ", certificateUri:\"urn:x\", securityPolicy:\"http://opcfoundation.org/UA/SecurityPolicy#Basic256\"", "not one the gateway carries" },
			{ ", certificateUri:\"urn:x\", securityMode:\"None\", securityPolicy:\"http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256\"", "security mode None" },
			{ ", certificateUri:\"urn:x\", securityPolicy:\"http://opcfoundation.org/UA/SecurityPolicy#None\"", "Security policy None" },
			{ ", certificateUri:\"urn:x\", securityMode:\" Sign \"", "not one of None, Sign or SignAndEncrypt" },//judged as the engine stores it, untrimmed
			{ ", sessionTimeout:0", "of 0" },
			{ ", requestTimeout:0", "of 0" },
			{ ", ttl:0", "of 0" },
			{ ", ttl:2147483648", "at most" },//over SQL Server's int, and a uint32 on Windows alone
			{ ", certificateUri:\"urn:x\", sessionTimeout:\"soon\"", "number" } } ){//the engine's own conversion refuses this one, ahead of the save rule's (MergeSettings), which testConnection alone reaches.
			let error = create( args );
			ASSERT_TRUE( error ) << args;
			EXPECT_NE( string::npos, error->find(expected) ) << args << " -> " << *error;
			EXPECT_FALSE( SelectServerCnnctn(slug) ) << args;
		}
		//accepted, and then the update's half:  the uri the mode needs cannot leave, and a mode cannot be pinned on a row with no uri.
		ASSERT_FALSE( create(", certificateUri:\"urn:x\", securityMode:\"Sign\", pingInterval:0") );//the ping interval's 0 turns the keep-alive off
		let id = SelectServerCnnctn( slug )->Id;
		absl::Cleanup purge = [id]{ PurgeServerCnnctn( id ); };
		let update = [&]( sv args )->optional<string>{
			try{ QL().QuerySync<jvalue>( Ƒ("mutation updateServerConnection( id:{}, {} )", id, args), {}, {UserPK::System} ); }
			catch( const std::exception& e ){ return e.what(); }
			return nullopt;
		};
		auto error = update( "certificateUri:\"\"" );
		ASSERT_TRUE( error );
		EXPECT_NE( string::npos, error->find("certificate URI") ) << *error;
		EXPECT_EQ( SelectServerCnnctn(id)->CertificateUri, "urn:x" );
		ASSERT_FALSE( update("securityMode:null, certificateUri:\"\"") );//both at once is fine:  the merged row is None without a uri.
		EXPECT_TRUE( SelectServerCnnctn(id)->CertificateUri.empty() );
		error = update( "securityMode:\"Sign\"" );//pinned on a row that now has none
		ASSERT_TRUE( error );
		EXPECT_NE( string::npos, error->find("certificate URI") ) << *error;
		EXPECT_TRUE( SelectServerCnnctn(id)->SecurityMode.empty() );
		ASSERT_FALSE( update("description:\"no settings named\"") );//a rule-free update asks no select
	}
}