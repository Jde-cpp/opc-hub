#pragma once
#include <jde/db/awaits/ExecuteAwait.h>
#include <jde/access/access.h>
#include <jde/ql/IQLSession.h>
#include <jde/ql/types/MutationQL.h>

namespace Jde::Access::Server{
	//updateProfile( slug:"logs/views", value:"{…}" ) — upsert scoped to the executer; missing/null value deletes the row.  A guest
	//(#483) is refused, as an anonymous caller is:  its user is every visitor's, so its rows would be every visitor's.
	struct ProfileAwait final : TAwait<jvalue>, noncopyable{
		ProfileAwait( QL::MutationQL m, QL::Creds creds, SRCE )ι:
			TAwait<jvalue>{ sl },
			_mutation{ move(m) },
			_executer{ creds.UserPK() },
			_guest{ creds.IsGuest() }
		{}
		α Suspend()ι->void override;
	private:
		α Execute()ι->DB::ExecuteAwait::Task;
		QL::MutationQL _mutation;
		Jde::UserPK _executer;
		bool _guest;
	};
}
