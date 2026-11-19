#include <gtest/gtest.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include <jde/db/meta/AppSchema.h>

namespace Jde::DB::Tests{
	//db-refactor B4: Table absorbed View.  FindOwnColumn is the former View::FindColumn - this table's columns only, where
	//`id` names a lone surrogate key - and FindColumn is that, then the extended table's.  Neither needs a schema: Extends
	//is wired by hand here, the way Initialize would resolve the json placeholder.
	static auto parse( sv json )->jobject{ return boost::json::parse( json ).as_object(); }

	TEST( TableTests, FindOwnColumnStopsAtThisTable ){
		auto identities = ms<Table>( "identities", parse(R"({"columns":{"identityId":{"sk":0,"type":"UInt"},"name":{},"deleted":{"nullable":true}}})") );
		Table users{ "users", parse(R"({"columns":{"identityId":{"sk":0,"type":"UInt"},"slug":{}},"extends":"identities"})") };
		ASSERT_TRUE( users.Extends );
		EXPECT_EQ( users.Extends->Name, "identities" ); //the ctor's placeholder, resolved by Initialize.
		users.Extends = identities;

		EXPECT_EQ( users.FindOwnColumn("slug"), users.Columns[1] );
		EXPECT_FALSE( users.FindOwnColumn("name") );                   //identities' column: never through Extends.
		EXPECT_EQ( users.FindColumn("name"), identities->Columns[1] ); //FindColumn chains.
		EXPECT_FALSE( users.FindColumn("nothing") );
		EXPECT_FALSE( users.FindOwnColumn("deleted") );
		EXPECT_TRUE( users.FindColumn("deleted") );
	}

	TEST( TableTests, IdNamesTheLoneSurrogateKey ){
		Table users{ "users", parse(R"({"columns":{"identityId":{"sk":0,"type":"UInt"},"slug":{}}})") };
		ASSERT_EQ( users.SurrogateKeys.size(), 1u );
		EXPECT_EQ( users.FindOwnColumn("id"), users.SurrogateKeys[0] ); //no column is called "id" - the alias resolves to the sk.
		EXPECT_EQ( users.FindColumn("id"), users.SurrogateKeys[0] );
		EXPECT_EQ( users.FindOwnColumn("identity_id"), users.SurrogateKeys[0] ); //json name -> db name.

		Table pair{ "pair", parse(R"({"columns":{"a":{"sk":0},"b":{"sk":1}}})") };
		EXPECT_FALSE( pair.FindOwnColumn("id") ); //two surrogate keys: "id" is ambiguous, so no alias.
		EXPECT_FALSE( Table{"placeholder"}.FindColumn("id") ); //no columns at all.
	}

	//db-review3 #33: getMap() runs from Table's member-init list and FindColumn's "id" alias reads SurrogateKeys, which used to be declared *after* Map - so the read was of a not-yet-constructed vector.
	//The surrogate key is deliberately not named "id": with the old order the garbage size() sends FindColumn to the Columns search, which has no "id", and GetColumnPtr throws.
	TEST( TableTests, MapIdAliasResolvesSurrogateKey ){
		const Table t{ "m", parse(R"({"columns":{"entity_id":{"sk":0,"i":0},"member_id":{"i":1}},"map":{"parentId":"id","childId":"member_id"}})") };
		ASSERT_EQ( t.SurrogateKeys.size(), 1u );
		ASSERT_TRUE( t.Map.has_value() );
		EXPECT_EQ( t.Map->Parent, t.SurrogateKeys[0] ); //"id" resolved through the surrogate key, not by column name.
		EXPECT_EQ( t.Map->Parent->Name, "entity_id" );
		EXPECT_EQ( t.Map->Child->Name, "member_id" );
	}

	//db-review2 #10: SchemaDdl::Sync now delegates to ObjectPrefix (was a swapped-ternary duplicate that yielded "" for a "db.um_" prefix). Lock in ObjectPrefix.
	TEST( AppSchemaTests, ObjectPrefix ){
		EXPECT_EQ( (AppSchema{ "s", {}, "db.um_" }).ObjectPrefix(), "um_" ); //MSSQL-style schema.prefix -> the object prefix after the dot.
		EXPECT_EQ( (AppSchema{ "s", {}, "um_" }).ObjectPrefix(), "um_" );    //no dot -> unchanged.
		EXPECT_EQ( (AppSchema{ "s", {}, "" }).ObjectPrefix(), "" );          //empty -> empty.
	}
}