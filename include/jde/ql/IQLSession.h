#pragma once

namespace Jde::QL{

	struct IQLSession : noncopyable{
		IQLSession()ι=default;
		IQLSession( UserPK userPK )ι:UserPK{userPK}{}
		virtual ~IQLSession();
		UserPK UserPK;
		//minted for a caller with no sign-in as the install's guest user (#483):  UserPK carries its rights, but nobody signed in, and
		//every visitor's session has the same one - so a check of who signed in, or whose a session is, reads this, never UserPK.
		bool IsGuest{};
	};
	inline IQLSession::~IQLSession(){}

	struct Creds{
		α Session()ι->sp<IQLSession>{
			auto p = std::get_if<sp<IQLSession>>(&Value);
			return p ? *p : nullptr;
		}
		α UserPK()ι->Jde::UserPK{
			auto userPK = std::get_if<Jde::UserPK>( &Value );
			if( auto p = userPK ? nullptr : Session(); p )
				userPK = &p->UserPK;
			return userPK ? *userPK : Jde::UserPK{};
		}
		α IsGuest()ι->bool{ auto p = Session(); return p ? p->IsGuest : Guest; }
		variant<Jde::UserPK, sp<IQLSession>> Value;
		bool Guest{};//a bare UserPK's IsGuest:  a socket running a query as its guest session's user hands on the pk alone.
	};
}
