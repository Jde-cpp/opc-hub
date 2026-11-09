#pragma once
#include <jde/ql/usings.h>

namespace Jde::QL{ struct Subscription; }
namespace Jde::App::Proto::FromServer{ class Traces; }
namespace Jde::App::Client{ struct IAppClient; }
namespace Jde::App::Client::Subscriptions{
	//What a client asked for, as opposed to what the current socket happens to be carrying.  Server-side subscriptions die
	//with the socket, so the live ids belong to the socket - `AppClientSocketSession::ListenRemote`/`ClearSubscriptions` - but the
	//*request* has to outlive them or nothing can be put back afterwards.  Kept per (client, query, variables, listener) rather than per
	//subscription id, because one query can ack into several ids and replaying it re-creates all of them.
	struct Request final{
		const IAppClient* Client;//whose session acked it:  a process that hosts several app clients (Jde.Opc.Tests, the gateway's beside the OpcServer's) replays each one's own.
		string Query;
		jobject Variables;
		sp<QL::IListener> Listener;
		flat_set<QL::SubscriptionId> Ids;//what this request acked into, so Forget can drop it by id.
		uint Failures{};//L5: consecutive replays the server has refused.  Reset by Remember, which only runs on an ack.
	};
	α Remember( const IAppClient* client, string query, jobject variables, sp<QL::IListener> listener, const flat_set<QL::SubscriptionId>& ids )ι->void;
	α Forget( const sp<QL::IListener>& listener, const flat_set<QL::SubscriptionId>& ids )ι->void;//ids empty = every request of listener's.
	α Remembered()ι->vector<Request>;
	α NoteReplayFailure( const Request& request )ι->bool;
	//Re-issues every request Remembered for `client` on its session, which has just come up.  Returns the number replayed, 0 on
	//a first connect.  `replayed` runs once every one has been answered, and not at all on a first connect:  a read of the rows
	//made sooner could miss an edit landing before its table's subscription is back.
	α Replay( sp<IAppClient> client, function<void()> replayed={} )ι->uint;
}