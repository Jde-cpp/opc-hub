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