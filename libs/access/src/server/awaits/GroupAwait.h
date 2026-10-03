#pragma once
#include <jde/ql/QLAwait.h>
#include <jde/ql/QLHook.h>
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
		α MembersStatement( QL::TableQL& membersQL, const jobject& groupArgs )ε->DB::Statement;
		α Select()ι->QL::QLAwait<>::Task;
	};

	struct GroupHook final : QL::IQLHook{
		α AddBefore( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;
	private:
		α AddRemoveArgs( const QL::MutationQL& m )ι->std::pair<optional<GroupPK>, flat_set<IdentityPK::Type>>;//nullopt: neither spelling of the parent id was sent - reported, not thrown, since this is ι.
	};
}