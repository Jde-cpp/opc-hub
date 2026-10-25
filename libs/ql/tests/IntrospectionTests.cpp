//review3 #2:  Parser deliberately skips SetDBTable for `__type(name:"logTags")` (there is no logTags view), and only the
//`enumValues` branch of QueryType special-cased the name - the `fields` branch dereferenced the null table, so an
//unauthenticated `__type(name:"logTags"){ fields{…} }` segfaulted.  QueryType runs in SelectAwait::await_ready, before any
//Authorize, which is what made it reachable.  Schema-free:  with no AppSchema the parser leaves every DBTable null anyway,
//and nothing here reaches a data source.
#include <gtest/gtest.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/ql/ops/TablesAwait.h>
#include <jde/ql/ql.h>
#include "NullQL.h"
#include "UnitSchema.h"

#define let const auto

namespace Jde::QL::Tests{
	Ω query( string text, sp<NullQL> ql, const vector<sp<DB::AppSchema>>& schemas={} )ε->jvalue{
		vector<TableQL> tables; tables.push_back( QL::ParseQuery(move(text), {}, schemas) );
		return BlockAwait<TablesAwait,jvalue>( TablesAwait{move(tables), {}, Creds{UserPK{UserPK::System}}, sp<IQL>{ql}, SRCE_CUR} );
	}

	//the crash:  no view, no config entry, so there is nothing to introspect fields from.  The message is asserted as well as
	//the throw:  it used to arrive blank because SelectAwait::await_resume rethrew through the up<runtime_error>'s *static*
	//type, and every Jde exception's runtime_error base is runtime_error{""} - that is #28, now fixed, and this is the pin.
	TEST( IntrospectionTests, LogTagsFieldsThrowsInsteadOfDereferencingNull ){
		auto ql = ms<NullQL>();
		try{
			query( R"(__type(name:"logTags"){ fields{ name } })", ql );
			FAIL() << "expected a throw";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("logTags"), string::npos ) << "the message did not survive the rethrow: '" << e.what() << "'";
		}
	}

	//the branch that always worked, unchanged by the guard:  logTags' values come from the log tag registry, not a table.
	TEST( IntrospectionTests, LogTagsEnumValuesStillReturnsTheTagList ){
		auto ql = ms<NullQL>();
		let y = query( R"(__type(name:"logTags"){ enumValues{ id name } })", ql );
		let& enumValues = Json::AsArray( y.get_object(), "enumValues" );
		EXPECT_EQ( enumValues.size(), Logging::Tags().size() );
		ASSERT_FALSE( enumValues.empty() );
		EXPECT_TRUE( enumValues[0].get_object().contains("name") );
		EXPECT_FALSE( enumValues[0].get_object().contains("description") ); //only the columns asked for.
	}

	//every other unknown type name is refused a step earlier, by Parser::LoadTables -> GetViewPtr, so it never reaches QueryType.
	TEST( IntrospectionTests, UnknownTypeNameThrowsAtParse ){
		auto ql = ms<NullQL>();
		EXPECT_THROW( query(R"(__type(name:"notATable"){ fields{ name } })", ql), Exception );
	}

	//review3 #40: introspectFields skipped VarBinary by hand before calling QLType, but QLType threw for eight more types, so a
	//Guid column failed the whole document with "Query failed.".  A column with no graphql spelling is left out of the schema
	//now, which is what the VarBinary case already did and what QuerySchema does since #31.  A Guid has one since 5b (#223):
	//String, its text.
	TEST( IntrospectionTests, AColumnWithNoQLTypeIsOmittedRatherThanFailingTheType ){
		auto ql = ms<NullQL>();
		let schema = schemas();
		let y = query( R"(__type(name:"Node"){ name fields{ name } })", ql, schema );
		let& type = y.get_object();
		EXPECT_EQ( Json::AsSV(type, "name"), "Node" );
		flat_set<string> names;
		for( let& f : Json::AsArray(type, "fields") )
			names.emplace( Json::AsSV(Json::AsObject(f), "name") );
		EXPECT_TRUE( names.contains("id") ) << Str::Join( names, "," );   //the pk, still there.
		EXPECT_TRUE( names.contains("name") ) << Str::Join( names, "," );
		EXPECT_TRUE( names.contains("guid") ) << "a Guid column is a String";
		EXPECT_FALSE( names.contains("secret") ) << "and VarBinary never had one";
	}
	//the control: a table whose columns all map is unaffected - the skip must not eat anything else.
	TEST( IntrospectionTests, AFullyMappableTypeKeepsEveryField ){
		auto ql = ms<NullQL>();
		let schema = schemas();
		let y = query( R"(__type(name:"ProviderType"){ fields{ name } })", ql, schema );
		flat_set<string> names;
		for( let& f : Json::AsArray(y.get_object(), "fields") )
			names.emplace( Json::AsSV(Json::AsObject(f), "name") );
		EXPECT_EQ( names.size(), 2u ) << Str::Join( names, "," ); //id and name.
	}
}
