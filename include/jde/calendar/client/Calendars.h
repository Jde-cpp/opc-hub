#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <jde/calendar/Schedule.h>

namespace Jde::QL{ struct IQL; }
//A gateway's cache of every calendar (docs/calendar/spec.md *The client*), process-wide:  loaded when the gateway connects,
//reloaded whole 250 ms after the last event of the four tables, and again on the new session after a reconnect.  It reads and
//subscribes through one QL::IQL, the app client's QLServer(), so nothing here knows a socket:  a split gateway's is its
//session's ClientQL, read as the session's user, and the hub's is the AppServer's own QL, read as System.
namespace Jde::Calendar::Client::Calendars{
	//Subscribes to the four tables' events through `ql` as `executer`, then loads.  Never throws:  a refused subscription or
	//read, a split gateway without Read on an enforced table, is logged as an Error, and the cache then holds nothing.  Once
	//per process, at startup.
	struct ConfigureAwait final : AnyVoidAwait{
		ConfigureAwait( sp<QL::IQL> ql, UserPK executer, SRCE )ι:AnyVoidAwait{ sl }, _ql{ move(ql) }, _executer{ executer }{}
	private:
		α Suspend()ι->void override{ Execute(); }
		α Execute()ι->VoidTask;
		sp<QL::IQL> _ql;
		UserPK _executer;
	};
	α Configure( sp<QL::IQL> ql, UserPK executer, SRCE )ι->ConfigureAwait;
	//A reconnect's:  `ql` is the new session's, the one Configure was handed having died with the old, and the calendars are
	//read again through it, since the edits made while the gateway was away raised no event here.  The subscriptions come back
	//by themselves (App::Client::Subscriptions::Replay).  Nothing before Configure.
	α Reload( sp<QL::IQL> ql )ι->void;
	α Shutdown( bool terminate, SRCE )ι->void;//stops the reloads and the subscriptions;  what is held stays readable.

	//A calendar's snapshot, immutable, so it is read with no lock and stays whole however long it is kept;  null for a calendar
	//that is deleted, purged or was never there.  A reload that finds a calendar's rows unchanged keeps its snapshot.
	α Find( uint32 id )ι->sp<const Schedule>;
	α Find( sv slug )ι->sp<const Schedule>;
	//`fn` hears the ids a reload changed, compared row for row:  created, edited in any of the four tables, or gone.  Called
	//on the reload's thread after the swap, so Find already answers with the new rows.  Returns StopListen's handle;  a reload
	//in flight may call `fn` once more after StopListen returns.
	using Listener = function<void( const flat_set<uint32>& changed )>;
	α Listen( Listener fn )ι->uint;
	α StopListen( uint handle )ι->void;
	α LoadCount()ι->uint;//the loads that read the tables and swapped, for tests.
}