#include <jde/db/IDataSource.h>
#include <jde/db/Row.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/QLAwait.h>
#include "helpers.h"
#include "../src/appStartup.h"
#include "../src/ql/AppServerQL.h"
#define let const auto

//The `logs` resource on the AppServer:  an instance registers with its access schema (auth_resource), and its forwarded lines
//and stored tag levels then answer to <that schema>/logs - the AppServer's own to app/logs.
namespace Jde::App::Server::Tests{
	struct LogAccessTests : ::testing::Test{
		static constexpr sv Schema{ "opc.logAccessTests" };
		//Find-or-create throughout (FindOrCreate):  the default (mysql) run keeps its db, and a second create trips the unique indexes.
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
		//Find-or-create each step on its own:  a run that died between two of them (the mysql db is kept) leaves the rest to the
		//next, and a second reader still gets its acl from a role that exists (325-logs-resource review #13).
		Ω GrantRead( Jde::UserPK user )->void{
			constexpr sv slug{ "logAccessReader" };
			let role = RunQL( "role( slug:$slug, deleted:null ){ id permissionRight{ id resource(schemaName:$schema, slug:\"logs\", criteria:null){ id } } }", {{"slug", slug}, {"schema", Schema}} );
			let found = role.is_object() && !role.get_object().empty();
			let roleId = found ? Id( role ) : Id( RunQL("createRole( slug:$slug, name:$slug ){ id }", {{"slug", slug}}) );
			if( !found || Json::FindDefaultObject(role.get_object(), "permissionRight").empty() )
				RunQL( "addRole( id:$roleId, permissionRight:{ allowed:2, denied:0, resource:{ schemaName:$schema, slug:\"logs\" } } )", {{"roleId", roleId}, {"schema", Schema}} );
			let acl = RunQL( "acl( identityId:$userId ){ role{ id } }", {{"userId", user.Value}} );
			let granted = acl.is_array() && std::ranges::any_of( acl.get_array(), [roleId]( let& row ){
				return row.is_object() && Json::FindNumber<uint>( Json::FindDefaultObject(row.get_object(), "role"), "id" )==roleId;
			} );
			if( !granted )
				RunQL( "createAcl( identity:{ id:$userId }, role:{ id:$roleId } )", {{"userId", user.Value}, {"roleId", roleId}} );
		}
	};

	TEST_F( LogAccessTests, TheRegisteredSchemaIsKeptOnTheInstance ){
		let instance = App::AddConnection( "Tests.LogAccess", "kept", "logaccess-host", 117, string{Schema} ).Instance;
		EXPECT_EQ( App::InstanceLogSchema(instance), Schema );
		let rows = Server::AppSchema()->DS()->Select( {Ƒ("select resource_schema from {} where instance_id=?", Server::AppSchema()->GetTable("instances").DBName), {DB::Value{instance}}} );
		ASSERT_EQ( rows.size(), 1u );
		EXPECT_EQ( rows[0].GetString(0), Schema ) << "persisted, so a restart still knows whose lines they are";

		(void)App::AddConnection( "Tests.LogAccess", "kept", "logaccess-host", 117 );//re-registered without one - a renamed /opcServer/resource, or none
		EXPECT_EQ( App::InstanceLogSchema(instance), Server::AppSchema()->Name );
		EXPECT_TRUE( Server::AppSchema()->DS()->Select({Ƒ("select resource_schema from {} where instance_id=?", Server::AppSchema()->GetTable("instances").DBName), {DB::Value{instance}}})[0].IsNull(0) );
	}

	//325-logs-resource review #2:  any session may register any (program, name), so a socket registration changes the row's
	//schema only as an administrator of both the schema it leaves and the one it claims.
	TEST_F( LogAccessTests, ARegistrationChangesTheSchemaOnlyAsAnAdministratorOfBoth ){
		(void)App::AddConnection( "Tests.LogAccess", "claimed", "logaccess-host", 119, string{Schema} );
		let stranger = User( "logAccessNobody" );
		let resourceId = EnforceLogs();
		let registered = [&]( str claimed, Jde::UserPK registrant ){ return App::RegistrationSchema( "Tests.LogAccess", "claimed", claimed, registrant ); };
		EXPECT_EQ( registered({}, stranger), Schema ) << "cleared, its lines would answer to app/logs alone";
		EXPECT_EQ( registered("opc.logAccessNowhere", stranger), Schema ) << "moved to a schema with no active root, which passes everyone";
		EXPECT_EQ( registered({}, Jde::UserPK{Jde::UserPK::System}), "" ) << "an administrator of the schema it leaves";
		EXPECT_EQ( App::RegistrationSchema("Tests.LogAccess", "unclaimed", string{Schema}, stranger), "" ) << "nor claims one it does not administer";

		RunQL( Ƒ("deleteResource( id:{} )", resourceId) );//unenforced, as installed
		EXPECT_EQ( registered({}, stranger), "" ) << "no active root, nothing to administer";
	}

	//#13:  the old GrantRead returned once the role existed, so a reader after the first - or a run that died after createRole -
	//never got its acl.
	TEST_F( LogAccessTests, GrantReadCompletesForASecondReader ){
		let instance = App::AddConnection( "Tests.LogAccess", "second", "logaccess-host", 120, string{Schema} ).Instance;
		GrantRead( User("logAccessReader") );
		let second = User( "logAccessSecondReader" );
		GrantRead( second );
		GrantRead( second );//and again changes nothing
		let resourceId = EnforceLogs();
		EXPECT_FALSE( App::HiddenLogInstances(second).contains(instance) );
		EXPECT_EQ( Json::AsArray(RunQL(Ƒ("acl( identityId:{} ){{ role{{ id }} }}", second.Value))).size(), 1u ) << "one acl, not one per call";
		RunQL( Ƒ("deleteResource( id:{} )", resourceId) );//unenforced, as installed
	}

	TEST_F( LogAccessTests, AnInstancesLogsGateItsLinesAndTagLevels ){
		let instance = App::AddConnection( "Tests.LogAccess", "gated", "logaccess-host", 118, string{Schema} ).Instance;
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