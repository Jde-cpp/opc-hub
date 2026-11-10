#include <jde/db/IDataSource.h>
#include <jde/db/generators/FromClause.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Catalog.h>
#include <jde/db/meta/DBSchema.h>
#include "../src/MySqlSyntax.h"

#define let const auto

namespace Jde::DB::MySql::Tests{
	//Answers the dialect only - FromClause::ToString reaches it through Table > AppSchema > DBSchema > Catalog.
	struct SyntaxOnlyDS final : IDataSource{
		α Disconnect()ε->void override{}
		α SetConfig( const jobject& )ε->void override{}
		α ServerMeta()ι->IServerMeta& override{ std::abort(); }
		α Query( Sql&&, bool, SL )ε->QueryAwait override{ throw std::logic_error{"no server"}; }
		α Syntax()ι->const DB::Syntax& override{ return MySqlSyntax::Instance(); }
	protected:
		α Execute( Sql&&, SL, Params )ε->uint override{ throw std::logic_error{"no server"}; }
		α InsertSeqSyncUInt( InsertClause&&, SL )ε->uint override{ throw std::logic_error{"no server"}; }
	};

	Ω column( const sp<AppSchema>& schema, string table, sv name )ι->sp<Column>{
		auto t = ms<Table>( move(table) );
		t->Schema = schema;
		auto c = ms<Column>( name );
		c->Table = move( t );
		return c;
	}

	//#590: a list under `glob` is a clause and a placeholder for each pattern, all of which must match - the three dialects
	//share Syntax::FormatOperator and differ in the pattern operator alone.
	TEST( SyntaxTests, AGlobListIsEveryPattern ){
		auto schema = ms<AppSchema>( "access", flat_map<string,sp<Table>>{}, "access_" );
		schema->DBSchema = ms<DBSchema>( "", flat_map<string,sp<Table>>{}, "" );
		schema->DBSchema->Catalog = ms<Catalog>( ms<SyntaxOnlyDS>() );
		let name = column( schema, "access_identities", "name" );
		let& my = MySqlSyntax::Instance();
		EXPECT_EQ( my.FormatOperator(*name, EOperator::Glob), "access_identities.name regexp ?" );
		EXPECT_EQ( my.FormatOperator(*name, EOperator::Glob, 2), "(access_identities.name regexp ? and access_identities.name regexp ?)" );
		EXPECT_THROW( my.FormatOperator(*name, EOperator::Glob, 0), Exception );
		EXPECT_THROW( my.FormatOperator(*name, EOperator::Regex, 2), Exception );//a regex is still one pattern.
		const Syntax tsql;
		EXPECT_EQ( tsql.FormatOperator(*name, EOperator::Glob, 3), "(access_identities.name like ? and access_identities.name like ? and access_identities.name like ?)" );
	}

	//#355: `using(identity_id)` coalesces against every table already in the from clause, and the user's aliased
	//identities row has an identity_id too - mysql refused UserAwait::GroupStatement at prepare (1052 ambiguous).
	TEST( JoinTests, GroupStatementJoinsWithOn ){
		auto schema = ms<AppSchema>( "access", flat_map<string,sp<Table>>{}, "access_" );
		schema->DBSchema = ms<DBSchema>( "", flat_map<string,sp<Table>>{}, "" );
		schema->DBSchema->Catalog = ms<Catalog>( ms<SyntaxOnlyDS>() );
		let userId = column( schema, "access_identities", "identity_id" );

		FromClause from;
		from += { userId, "users_", column(schema, "access_groups", "member_id"), {}, true };
		from += { column(schema, "access_groups", "identity_id"), column(schema, "access_identities", "identity_id"), true };
		EXPECT_EQ( from.ToString(),
			"from access_identities users_"
			"\n\tjoin access_groups on users_.identity_id=access_groups.member_id"
			"\n\tjoin access_identities on access_groups.identity_id=access_identities.identity_id" );
	}
}