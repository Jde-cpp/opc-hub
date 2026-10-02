#include "gtest/gtest.h"
#include <jde/ql/QLAwait.h>
#include <jde/access/usings.h>
#include "globals.h"

#define let const auto
namespace Jde::Access{
	using namespace Json;
	using namespace Tests;

	class UserTests : public ::testing::Test{
	protected:
	};


	TEST_F( UserTests, Fields ){
		let query = "{ __type(name: \"User\") { fields { name type { name kind ofType{name kind} } } }}";
		let json = QL().QuerySync( query, {}, GetRoot() );
		let actual = Str::Replace( serialize(json), '"', '\'' );
		let expected = "{'name':'User','fields':[{'name':'id','type':{'name':null,'kind':'NON_NULL','ofType':{'name':'ID','kind':'SCALAR'}}},{'name':'name','type':{'name':null,'kind':'NON_NULL','ofType':{'name':'String','kind':'SCALAR'}}},{'name':'provider','type':{'name':'Provider','kind':'ENUM'}},{'name':'slug','type':{'name':null,'kind':'NON_NULL','ofType':{'name':'String','kind':'SCALAR'}}},{'name':'attributes','type':{'name':'UInt','kind':'SCALAR'}},{'name':'created','type':{'name':null,'kind':'NON_NULL','ofType':{'name':'DateTime','kind':'SCALAR'}}},{'name':'updated','type':{'name':'DateTime','kind':'SCALAR'}},{'name':'deleted','type':{'name':'DateTime','kind':'SCALAR'}},{'name':'description','type':{'name':'String','kind':'SCALAR'}},{'name':'email','type':{'name':'String','kind':'SCALAR'}},{'name':'loginName','type':{'name':'String','kind':'SCALAR'}},{'name':'modulus','type':{'name':'String','kind':'SCALAR'}},{'name':'exponent','type':{'name':'UInt','kind':'SCALAR'}},{'name':'issuer','type':{'name':'String','kind':'SCALAR'}},{'name':'subjectAlt','type':{'name':'String','kind':'SCALAR'}},{'name':'distinguished','type':{'name':'String','kind':'SCALAR'}},{'name':'expiration','type':{'name':'DateTime','kind':'SCALAR'}},{'name':'fingerprint','type':{'name':'String','kind':'SCALAR'}}]}";
		ASSERT_EQ( actual, expected );
	}

	//ql-review3 #20: `provider` is not a column of users_ql - it is how provider_id renders, through the enum table's name.
	//addColumn knew that and the filter/orderBy paths did not, so the ql-list's own filter and sort dialogs - which offer every
	//field __type advertises, `provider` among them - came back `[users_ql.provider]Could not find column.`
	//ql-review3 #20: `provider` is not a column of users_ql - it is how provider_id renders, through the enum table's name.
	//addColumn knew that and the filter/orderBy paths did not, so the ql-list's own filter and sort dialogs - which offer every
	//field __type advertises, `provider` among them - came back `[users_ql.provider]Could not find column.`
	TEST_F( UserTests, FilterAndOrderByEnumDisplayName ){
		let root = GetRoot();
		let byId = QL().QuerySync<jarray>( "users(providerId:["+std::to_string(underlying(EProviderType::Google))+"]){ id }", {}, root );
		ASSERT_FALSE( byId.empty() );
		EXPECT_EQ( serialize(QL().QuerySync<jarray>(R"(users(provider:["Google"]){ id })", {}, root)), serialize(byId) ) << "the display name has to select the same rows as its id";
		EXPECT_EQ( serialize(QL().QuerySync<jarray>(R"(users(provider:"Google"){ id })", {}, root)), serialize(byId) ); //scalar as well as array.
		//the id column filtered by a *name*:  the client only ever has the name, and DB::Value{UInt,"Google"} used to throw.
		EXPECT_EQ( serialize(QL().QuerySync<jarray>(R"(users(providerId:["Google"]){ id })", {}, root)), serialize(byId) );

		EXPECT_NO_THROW( QL().QuerySync<jarray>(R"(users(orderBy:[{provider:"asc"}]){ id })", {}, root) );//sorts by the id column - the display join is not guaranteed to be in the statement.
	}
	TEST_F( UserTests, FilterByUnknownEnumNameThrows ){
		try{
			QL().QuerySync<jarray>( R"(users(provider:["NotAProvider"]){ id })", {}, GetRoot() );
			ADD_FAILURE() << "parsed";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("NotAProvider"), string::npos ) << e.what();
		}
	}

	TEST_F( UserTests, Crud ){
		const string slug{ "crud" };
		let existingUser = GetUser( slug, GetRoot() );
		auto id = GetId(existingUser);
		ASSERT_NE( id, 0 );

		let update = Ƒ( "mutation updateUser( \"id\":{}, \"name\":\"{}\" )", id, "newName" );
		let updateJson = QL().QuerySync<jvalue>( update, {}, GetRoot() );
		ASSERT_TRUE( AsSV(Tests::GetUser(slug, GetRoot()), "name")=="newName" );

		let del = Ƒ( "mutation deleteUser(\"id\":{})", id );
		let deleteJson = QL().QuerySync<jvalue>( del, {}, GetRoot() );
		ASSERT_TRUE( Tests::SelectUser(slug, GetRoot()).empty() );
		ASSERT_TRUE( !Tests::GetUser(slug, GetRoot(), true).empty() );

		PurgeUser( {id}, GetRoot() );
		ASSERT_TRUE( Tests::SelectUser(slug, GetRoot(), true).empty() );
	}

	TEST_F( UserTests, MultipleUsersSelect ){
		let a = GetId( GetUser("MultipleUsersA", GetRoot()) );
		let b = GetId( GetUser("MultipleUsersB", GetRoot()) );
		auto q = "query{ users(id:[$a,$b], orderBy:$orderBy){id loginName provider} }";
		auto vars = jobject{ {"a", a}, {"b", b}, {"orderBy", jarray{{{"name", "asc"}}}} };
		auto saved = QL().QuerySync<jarray>( move(q), move(vars), GetRoot() );
		ASSERT_EQ( saved.size(), 2 );
		ASSERT_EQ( saved[0].at("loginName").get_string(), "MultipleUsersA" );
		ASSERT_EQ( saved[1].at("loginName").get_string(), "MultipleUsersB" );
		PurgeUser( {a}, GetRoot() );
		PurgeUser( {b}, GetRoot() );
	}
	TEST_F( UserTests, NotIn ){
		auto q = "{users(slug:{nin:[\"root\"]}){slug} }";
		auto notRoot = QL().QuerySync<jarray>( move(q), {}, GetRoot() );
		for( auto& user : notRoot )
			ASSERT_NE( user.at("slug").get_string(), "root" );
	}
	TEST_F( UserTests, ProvidersSelect ){
		let readGroups = "__type(name: \"Provider\") { enumValues { id name } }";
		let readGroupsJson = QL().QuerySync( readGroups, {}, GetRoot() );
		ASSERT_TRUE( AsArrayPath(readGroupsJson, "enumValues").size()>0 );
	}
}