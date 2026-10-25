//A8: the mutation half of the engine moved out of QLAwait<jvalue>::Execute into MutationsAwait.  These pin the shaping it does -
//trim to the result request, wrap in the command name unless the document was raw, one object or a list - against an IQL that
//answers every mutation itself, the way the app servers' custom mutations do.  No data source: `status` is a system name, so the
//parser resolves no table and MutationAwait never reaches the crud ops.
#include <gtest/gtest.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/ql/ops/MutationsAwait.h>
#include <jde/ql/ql.h>
#include "NullQL.h"

#define let const auto

namespace Jde::QL::Tests{
	//resolves in await_ready - the value never leaves this thread.
	struct ReadyAwait final : TAwait<jvalue>{
		ReadyAwait( jvalue v, SRCE )ι:TAwait<jvalue>{sl}, _v{move(v)}{}
		α await_ready()ι->bool override{ return true; }
		α Suspend()ι->void override{ ASSERT(false); }
		α await_resume()ε->jvalue override{ return move(_v); }
	private:
		jvalue _v;
	};
	struct AnsweringQL final : NullQL{
		α CustomMutation( MutationQL&, Creds, SL )ι->up<TAwait<jvalue>> override{ ++Answered; return mu<ReadyAwait>( jarray{ jobject{{"id",7},{"name","seven"},{"rowCount",1}} } ); }
		uint Answered{};
	};
	Ω run( sv document, sp<AnsweringQL> ql, bool returnRaw=true )ε->jvalue{
		static const vector<sp<DB::AppSchema>> noSchemas;
		auto request = Parse( string{document}, {}, noSchemas, returnRaw );
		return BlockAwait<MutationsAwait,jvalue>( MutationsAwait{move(request.Mutations()), Creds{UserPK{UserPK::System}}, sp<IQL>{ql}, SRCE_CUR} );
	}

	TEST( MutationsAwaitTests, ResultIsTrimmedToTheRequest ){
		auto ql = ms<AnsweringQL>();
		let y = run( "mutation createStatus( slug:\"x\" ){ id name }", ql );
		EXPECT_EQ( y, (jvalue{ jobject{{"id",7},{"name","seven"}} }) ) << serialize(y);//rowCount and the arg are not asked for.
		EXPECT_EQ( ql->Answered, 1u );
	}
	TEST( MutationsAwaitTests, ANonRawDocumentWrapsInTheCommandName ){
		auto ql = ms<AnsweringQL>();
		let y = run( "mutation createStatus( slug:\"x\" ){ id }", ql, false );
		EXPECT_EQ( y, (jvalue{ jobject{{"createStatus", jobject{{"id",7}}}} }) ) << serialize(y);
	}
	TEST( MutationsAwaitTests, SeveralMutationsAreAList ){
		auto ql = ms<AnsweringQL>();
		let y = run( "mutation createStatus( slug:\"x\" ){ id } updateStatus( id:7 ){ name }", ql );
		EXPECT_EQ( y, (jvalue{ jarray{ jobject{{"id",7}}, jobject{{"name","seven"}} } }) ) << serialize(y);
		EXPECT_EQ( ql->Answered, 2u );
	}
	TEST( MutationsAwaitTests, NoResultRequestNoResult ){
		auto ql = ms<AnsweringQL>();
		let y = run( "mutation createStatus( slug:\"x\" )", ql );
		EXPECT_TRUE( y.is_array() && y.get_array().empty() ) << serialize(y);//asked for nothing back; the mutation still ran.
		EXPECT_EQ( ql->Answered, 1u );
	}
}