#pragma once
#include <jde/ql/QLAwait.h>
#include <jde/ql/types/MutationQL.h>
#include <jde/access/access.h>

namespace Jde::Access::Server{
	struct GroupAwait final : TAwait<jvalue>{
		GroupAwait( const QL::TableQL& query, UserPK executer, SRCE )ι:
			TAwait<jvalue>{ sl },
			_query{ query },
			_executer{ executer }
		{}
		α Suspend()ι->void override{ Select(); }
	private:
		QL::TableQL _query;
		Jde::UserPK _executer;
		α MembersStatement( QL::TableQL& membersQL, DB::WhereClause groupWhere )ε->DB::Statement;
		α Select()ι->QL::QLAwait<>::Task;
	};

	//addGroup, routed here by Server::CustomMutation:  authorized, then Authorize::TestAddGroupMember over the members named, then
	//the stock add.  An add that names no group or no member is left to the stock op, whose refusal names the column (GroupTests).
	struct GroupAddMAwait final : TAwait<jvalue>, noncopyable{
		GroupAddMAwait( QL::MutationQL m, UserPK executer, SRCE )ι:TAwait<jvalue>{ sl }, _mutation{ move(m) }, _executer{ executer }{}
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->TAwait<jvalue>::Task;
		QL::MutationQL _mutation;
		UserPK _executer;
	};
}