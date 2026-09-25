#include <jde/fwk/io/json.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/ql.h>
#include "../src/opcInternal.h"
#include "../src/auth/UM.h"
#include "../src/ql/GatewayQL.h"
#include "../src/ql/OpcQLHook.h"
#include "utils/helpers.h"

#define let const auto
namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };

	struct ServerCnnctnDBTests : ::testing::Test{
		atomic_flag Wait;
		std::any Result;
		std::any Id;
	protected:
		Ω SetUpTestCase()ι->void{};
		α SetUp()ι->void{ Wait.clear(); }
		static uint OpcProviderId;
		α InsertFailedImpl()ε->Access::ProviderPK;
		α PurgeFailedImpl()ε->Access::ProviderPK;
		α CrudImpl()ε->Access::ProviderPK;
		α CrudImpl2( ServerCnnctnPK id )ε->Access::ProviderPK;
		α CrudPurge( ServerCnnctnPK id )ε->Access::ProviderPK;
	};
	uint ServerCnnctnDBTests::OpcProviderId{};

	α GetProviderPK( string slug )ε->Access::ProviderPK{
		return BlockTAwait<Access::ProviderPK>( ProviderAwait{slug} );
	}
	α GetOpcServers( optional<DB::Key> key=nullopt, bool includeDeleted=false )->vector<ServerCnnctn>{
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
		BlockAwait<TAwait<jvalue>,jvalue>( move(*OpcQLHook{}.InsertFailure(insert, {UserPK::System})) );
		ASSERT_EQ( 0, GetProviderPK(slug) );
	}

	TEST_F( ServerCnnctnDBTests, PurgeFailed ){
		let existingServer = SelectServerCnnctn( OpcServerSlug );
		auto opcPK = existingServer ? existingServer->Id : 0;
		if( !opcPK )
			opcPK = BlockAwait<CreateServerCnnctnAwait,ServerCnnctnPK>( CreateServerCnnctnAwait{} );
		Id = opcPK;
		BlockTAwait<Access::ProviderPK>( ProviderMAwait{OpcServerSlug, false} );//BeforePurge mock.

		QL::MutationQL purge{ "purgeServerConnection", { {"id", opcPK} }, {}, nullopt, true, QL().Schemas(), false };
		BlockAwait<TAwait<jvalue>,jvalue>( move(*OpcQLHook{}.PurgeFailure(purge, {UserPK::System})) );
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

		let providerId = BlockTAwait<Access::ProviderPK>( ProviderAwait{slug} );
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

	TEST_F( ServerCnnctnDBTests, Crud ){
		try{
			auto providerPK = CrudImpl();
			ASSERT_EQ( 0, providerPK );
		}
		catch( const runtime_error& e ){
			ASSERT_DESC( false, e.what() );
		}
	}
}