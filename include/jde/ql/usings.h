#pragma once

namespace Jde::QL{
	enum class EMutationQL : uint8{ Create=0, Update=1, Delete=2, Restore=3, Purge=4, Add=5, Remove=6, Start=7, Stop=8, Execute=9 };
	//The verb a mutation command starts with (createUser) and the participle a subscription name ends with (userCreated), indexed by
	//EMutationQL - one table, so the two spellings cannot drift.  Execute has no participle: nothing publishes an execute.
	struct MutationQLName{ sv Verb; sv Participle; };
	constexpr array<MutationQLName,10> MutationQLNames{{ {"create","Created"}, {"update","Updated"}, {"delete","Deleted"}, {"restore","Restored"}, {"purge","Purged"}, {"add","Added"}, {"remove","Removed"}, {"start","Started"}, {"stop","Stopped"}, {"execute",{}} }};
	static_assert( MutationQLNames.size()==(uint)underlying(EMutationQL::Execute)+1, "one row per EMutationQL value" );
	using ListenerId = uint32;
	using SubscriptionId = uint32;

	//Hears the subscriptions it registered (Subscriptions::Listen, IQL::Subscribe):  one trimmed notification per matching mutation.
	//(OnTraces - the app server's log stream to a C++ client - was a second pure virtual here that every server-side listener
	//stubbed with ASSERT(false) and nothing in production implemented; removed with ql-refactor B2.  The browser's logs page is
	//that stream's consumer, and it never went through this interface.)
	struct IListener{
		IListener( str name )ι:Name{name}{}
		β OnChange( const jvalue& j, SubscriptionId clientId )ε->void=0;
		string Name;
	};
}