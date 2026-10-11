#include <jde/db/IDataSource.h>
#include <jde/db/Row.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/QLAwait.h>
#include "../src/appStartup.h"
#include "../src/ql/AppServerQL.h"
#define let const auto

//The `logs` resource on the AppServer:  an instance registers with its access schema (auth_resource), and its forwarded lines
//and stored tag levels then answer to <that schema>/logs - the AppServer's own to app/logs.
namespace Jde::App::Server::Tests{
	struct LogAccessTests : ::testing::Test{
		static constexpr sv Schema{ "opc.logAccessTests" };
		Ω RunQL( string query, jobject vars={}, Jde::UserPK executer=Jde::UserPK{Jde::UserPK::System} )->jvalue{
			return BlockAwait<QL::QLAwait<jvalue>,jvalue>( QL::QLAwait<jvalue>{move(query), move(vars), executer, Server::QLPtr()} );
		}
		Ω Status( function<void()> f )->EHttpStatus{
			try{
				f();
			}
			catch( const Exception& e ){
				return e.HttpStatus();
			}
			return EHttpStatus::None;
		}
		Ω Id( const jvalue& v )->uint{ return Json::AsNumber<uint>( Json::AsObject(v), "id" ); }
		//Find-or-create throughout:  the default (mysql) run keeps its db, and a second create trips the unique indexes.
		Ω User( str slug )->Jde::UserPK{
			auto user = RunQL( "user( slug:$slug, deleted:null ){ id }", {{"slug", slug}} );
			if( user.is_null() || (user.is_object() && user.get_object().empty()) )
				user = RunQL( "createUser( slug:$slug, name:$slug ){ id }", {{"slug", slug}} );
			return Jde::UserPK{ (Jde::UserPK::Type)Id(user) };
		}
		//Schema/logs, enforced.
		Ω EnforceLogs()->uint{
			auto resources = Json::AsArray( RunQL(Ƒ("resources( schemaName:[\"{}\"] ){{ id slug criteria deleted }}", Schema)) );
			optional<uint> id; bool deleted{ true };
			for( let& r : resources ){
				let& o = Json::AsObject( r );
				if( Json::AsString(o, "slug")=="logs" && Json::FindDefaultSV(o, "criteria").empty() ){
					id = Id( r );
					deleted = !o.at( "deleted" ).is_null();
				}
			}
			if( !id ){
				id = Id( RunQL(Ƒ("createResource( schemaName:\"{}\", name:\"logs\", slug:\"logs\", allowed:2 ){{ id }}", Schema)) );
				deleted = !Json::AsArray( RunQL(Ƒ("resources( id:{} ){{ id deleted }}", *id)) )[0].at( "deleted" ).is_null();
			}
			if( deleted )
				RunQL( Ƒ("restoreResource( id:{} )", *id) );
			return *id;
		}
		Ω GrantRead( Jde::UserPK user )->void{
			constexpr sv slug{ "logAccessReader" };
			auto role = RunQL( "role( slug:$slug, deleted:null ){ id }", {{"slug", slug}} );
			if( !role.is_null() && !(role.is_object() && role.get_object().empty()) )
				return;
			let roleId = Id( RunQL("createRole( slug:$slug, name:$slug ){ id }", {{"slug", slug}}) );
			RunQL( "addRole( id:$roleId, permissionRight:{ allowed:2, denied:0, resource:{ schemaName:$schema, slug:\"logs\" } } )", {{"roleId", roleId}, {"schema", Schema}} );
			RunQL( "createAcl( identity:{ id:$userId }, role:{ id:$roleId } )", {{"userId", user.Value}, {"roleId", roleId}} );
		}
	};

	TEST_F( LogAccessTests, TheRegisteredSchemaIsKeptOnTheInstance ){
		let instance = get<1>( App::AddConnection("Tests.LogAccess", "kept", "logaccess-host", 117, string{Schema}) );
		EXPECT_EQ( App::InstanceLogSchema(instance), Schema );
		let rows = Server::AppSchema()->DS()->Select( {Ƒ("select resource_schema from {} where instance_id=?", Server::AppSchema()->GetTable("instances").DBName), {DB::Value{instance}}} );
		ASSERT_EQ( rows.size(), 1u );
		EXPECT_EQ( rows[0].GetString(0), Schema ) << "persisted, so a restart still knows whose lines they are";

		(void)App::AddConnection( "Tests.LogAccess", "kept", "logaccess-host", 117 );//re-registered without one - a renamed /opcServer/resource, or none
		EXPECT_EQ( App::InstanceLogSchema(instance), Server::AppSchema()->Name );
		EXPECT_TRUE( Server::AppSchema()->DS()->Select({Ƒ("select resource_schema from {} where instance_id=?", Server::AppSchema()->GetTable("instances").DBName), {DB::Value{instance}}})[0].IsNull(0) );
	}

	TEST_F( LogAccessTests, AnInstancesLogsGateItsLinesAndTagLevels ){
		let instance = get<1>( App::AddConnection("Tests.LogAccess", "gated", "logaccess-host", 118, string{Schema}) );
		let nobody = User( "logAccessNobody" );
		let reader = User( "logAccessReader" );
		GrantRead( reader );
		let resourceId = EnforceLogs();

		EXPECT_TRUE( App::HiddenLogInstances(nobody).contains(instance) );
		EXPECT_FALSE( App::HiddenLogInstances(reader).contains(instance) );
		let tagLevels = Ƒ( "instanceTagLevel( id:{} ){{ text }}", instance );
		EXPECT_EQ( Status([&]{ RunQL(tagLevels, {}, nobody); }), EHttpStatus::Forbidden );
		EXPECT_EQ( Status([&]{ RunQL(tagLevels, {}, {}); }), EHttpStatus::Unauthorized ) << "enforced:  who is asking";
		EXPECT_EQ( Status([&]{ RunQL(tagLevels, {}, reader); }), EHttpStatus::None );
		let update = Ƒ( R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["sql"],level:"Debug"}}] ))", instance );
		EXPECT_EQ( Status([&]{ RunQL(update, {}, reader); }), EHttpStatus::Forbidden ) << "changing the levels is Update";

		RunQL( Ƒ("deleteResource( id:{} )", resourceId) );//unenforced, as installed
		EXPECT_FALSE( App::HiddenLogInstances(nobody).contains(instance) );
		EXPECT_EQ( Status([&]{ RunQL(tagLevels, {}, nobody); }), EHttpStatus::None );
		EXPECT_EQ( Status([&]{ RunQL(tagLevels, {}, {}); }), EHttpStatus::None ) << "anyone's, signed in or not";
	}
}