#include <jde/calendar/client/Calendars.h>
#include <jde/fwk/co/Timer.h>
#include <jde/ql/IQL.h>

#define let const auto

namespace Jde::Calendar::Client{
	constexpr ELogTags _tags{ ELogTags::App };
	constexpr Duration Debounce{ 250ms };//trailing:  the creates of one form submit are one reload.
	//The four flat reads of spec *Reads*, one request:  the live rows of the three tables that have `deleted`, and every shift.
	constexpr sv Reads{ "{ calendars(deleted:null){ id name slug timeZone dayStart } calendarPatterns(deleted:null){ id calendarId effective cycleDays cycleStart } calendarShifts{ id calendarPatternId name days start minutes breaks } calendarExceptions(deleted:null){ id calendarId name start finish isOpen } }" };

	//A calendar's rows, each child table's by id:  what one load compares with the last, row for row.
	struct Source final{
		jobject Calendar;
		jarray Patterns;
		jarray Shifts;
		jarray Exceptions;
		bool operator==( const Source& )const = default;
	};
	struct Entry final{ Source Rows; sp<const Schedule> Calendar; };
	struct Snapshot final{
		flat_map<uint32,Entry> ById;
		flat_map<string,sp<const Schedule>,std::less<>> BySlug;
	};

	static std::mutex _mutex;
	static sp<QL::IQL> _server;//the app client's QLServer():  replaced on a reconnect.
	static UserPK _user;
	static sp<QL::IListener> _listener;//set from Configure to Shutdown.
	static sp<const Snapshot> _snapshot{ ms<const Snapshot>() };//swapped whole by a load.
	static flat_map<uint,Calendars::Listener> _listeners;
	static uint _listenerHandle{};
	static uint _loads{};
	//One load at a time:  _running is the claim of the one coroutine that may load, Configure's and then rounds().  A request
	//meanwhile sets _dirty, which that coroutine answers with one more round, and ends its wait when it is in one.
	static bool _running{};
	static bool _dirty{};
	static bool _stopped{};
	static Duration _wait{};//the next round's:  Debounce after an event, nothing after a reconnect.
	static TimerCancel _timer;//set while a round waits.

	Ω number( const jvalue& row, sv key )ε->uint32{ return Json::AsNumber<uint32>( Json::AsObject(row), key ); }
	Ω rows( const jobject& result, sv table )ι->const jarray&{
		static const jarray none;
		let p = result.if_contains( table );
		return p && p->is_array() ? p->get_array() : none;
	}
	//The read's rows by calendar.  A child row whose parent is not in its read, a deleted calendar's pattern or a deleted
	//pattern's shift, belongs to no calendar held.
	Ω group( const jobject& result )ε->flat_map<uint32,Source>{
		flat_map<uint32,Source> y;
		for( let& row : rows(result, "calendars") )
			y[number(row, "id")].Calendar = Json::AsObject( row );
		flat_map<uint32,uint32> patterns;//its calendar.
		for( let& row : rows(result, "calendarPatterns") ){
			let calendarId = number( row, "calendarId" );
			if( auto p = y.find(calendarId); p!=y.end() ){
				patterns.emplace( number(row, "id"), calendarId );
				p->second.Patterns.push_back( row );
			}
		}
		for( let& row : rows(result, "calendarShifts") ){
			if( let p = patterns.find(number(row, "calendarPatternId")); p!=patterns.end() )
				y.find( p->second )->second.Shifts.push_back( row );
		}
		for( let& row : rows(result, "calendarExceptions") ){
			if( auto p = y.find(number(row, "calendarId")); p!=y.end() )
				p->second.Exceptions.push_back( row );
		}
		for( auto&& [_, source] : y ){
			for( auto table : {&source.Patterns, &source.Shifts, &source.Exceptions} )
				std::ranges::sort( *table, {}, []( const jvalue& row ){ return Json::FindNumber<uint32>( row.get_object(), "id" ).value_or( 0 ); } );//every row passed number( row, "id" ) or its parent's key above, so each is an object.
		}
		return y;
	}

	Ω notify( const flat_set<uint32>& changed )ι->void{
		if( changed.empty() )
			return;
		vector<Calendars::Listener> listeners;
		{
			lg _{ _mutex };
			for( auto&& [_, fn] : _listeners )
				listeners.push_back( fn );
		}
		for( let& fn : listeners ){//outside the lock:  a listener calls Find.
			try{
				fn( changed );
			}
			catch( const std::exception& e ){
				WARN( "A calendar listener threw on a reload's changes:  {}", e.what() );
			}
		}
	}

	//Swaps in the calendars the read holds and tells the listeners which changed.  Only a load swaps, and one runs at a time,
	//so the snapshot read here is the one replaced.
	Ω apply( const jobject& result )ε->void{
		auto sources = group( result );
		sp<const Snapshot> last;
		{
			lg _{ _mutex };
			last = _snapshot;
		}
		auto next = ms<Snapshot>();
		flat_set<uint32> changed;
		for( auto&& [id, source] : sources ){
			sp<const Schedule> calendar;
			if( let held = last->ById.find(id); held!=last->ById.end() && held->second.Rows==source )
				calendar = held->second.Calendar;
			else{
				try{
					calendar = ms<const Schedule>( source.Calendar, source.Patterns, source.Shifts, source.Exceptions );
				}
				catch( const std::exception& e ){//a zone this machine's time zone database lacks:  the others are still held.
					ERR( "Calendar {} is not held:  {}", id, e.what() );
					continue;
				}
				changed.emplace( id );
			}
			next->BySlug.emplace( calendar->Slug(), calendar );
			next->ById.emplace( id, Entry{move(source), move(calendar)} );
		}
		for( auto&& [id, _] : last->ById ){
			if( !next->ById.contains(id) )
				changed.emplace( id );
		}
		{
			lg _{ _mutex };
			_snapshot = move( next );
			++_loads;
		}
		notify( changed );
	}

	Ω clear()ι->void{
		flat_set<uint32> gone;
		{
			lg _{ _mutex };
			for( auto&& [id, _] : _snapshot->ById )
				gone.emplace( id );
			_snapshot = ms<const Snapshot>();
		}
		notify( gone );
	}

	//One read of the four tables, swapped in.  Never throws.  A refusal, the tables enforced with no Read for this process's
	//user, leaves nothing held, as the spec's *Authorization* says;  any other failure, a socket that dropped under the read,
	//leaves what the last load read, which the reconnect's reload replaces.
	Ω load()ι->AnyVoidTask{
		sp<QL::IQL> server; UserPK user;
		{
			lg _{ _mutex };
			server = _server;
			user = _user;
		}
		if( !server )
			co_return;
		try{
			auto read = server->Query( string{Reads}, {}, user );
			let result = co_await Any( *read );
			apply( Json::AsObject(result) );
		}
		catch( const std::exception& e ){
			{
				lg _{ _mutex };
				if( _stopped )
					co_return;
			}
			let known = dynamic_cast<const Exception*>( &e );
			let status = known ? known->HttpStatus() : EHttpStatus::InternalServerError;
			if( status==EHttpStatus::Forbidden || status==EHttpStatus::Unauthorized ){
				ERR( "The calendars were refused, so none is held - grant this process's user Read on the app schema's calendars, calendarPatterns, calendarShifts and calendarExceptions:  {}", e.what() );
				clear();
			}
			else
				ERR( "Could not load the calendars - holding what the last load read:  {}", e.what() );
		}
	}

	//The debounce and the reload, for as long as there is a request to answer.  The caller holds the _running claim.
	Ω rounds()ι->VoidTask{
		for( ;; ){
			optional<DurationTimer> timer;
			{
				lg _{ _mutex };
				if( _stopped || !_dirty ){
					_running = false;
					co_return;
				}
				_timer = timer.emplace( _wait ).Canceller();
			}
			let fired = co_await *timer;
			{
				lg _{ _mutex };
				_timer = {};
				if( !fired )//a request restarted the wait, or Shutdown ended it.
					continue;
				_dirty = false;
			}
			co_await load();//a request from here on is one more round, however many there are.
		}
	}
	Ω request( Duration wait )ι->void{
		{
			lg _{ _mutex };
			if( _stopped || !_listener )
				return;
			_dirty = true;
			_wait = wait;
			if( _running ){
				if( _timer )
					_timer.Cancel();//posts the wait's end, so nothing re-enters this lock.
				return;
			}
			_running = true;
		}
		rounds();
	}

	//Every event means the same here, reload, so the id only has to be the subscription's own.
	struct TableListener final : QL::IListener{
		TableListener()ι:QL::IListener{ "Calendars" }{}
		α OnChange( const jvalue&, QL::SubscriptionId )ε->void override{ request( Debounce ); }
	};

	//IQL::Subscribe once per table and event, with the text the access events use (EventsSubscribeAwait.cpp).  The id is the
	//table's index above an event bit, from 0x10000:  a socket session hands an event to every listener of its id, and the access
	//listener's, on the same session, end at 0x1040.  Never throws:  a table whose event is refused is logged and left.
	Ω subscribe( sp<QL::IQL> server, UserPK user, sp<QL::IListener> listener )ι->AnyVoidTask{
		using enum QL::EMutationQL;
		struct Table final{ sv Name; sv Capitalized; std::span<const QL::EMutationQL> Events; };
		static constexpr array<QL::EMutationQL,5> all{ Create, Update, Delete, Restore, Purge };
		static constexpr array<QL::EMutationQL,3> noDeleted{ Create, Update, Purge };//calendarShifts has no `deleted`.
		static constexpr array<Table,4> tables{ {{"calendar", "Calendar", all}, {"calendarPattern", "CalendarPattern", all}, {"calendarShift", "CalendarShift", noDeleted}, {"calendarException", "CalendarException", all}} };
		for( uint i=0; i<tables.size(); ++i ){
			let& table = tables[i];
			try{
				for( let event : table.Events ){
					let participle = QL::MutationQLNames[underlying(event)].Participle;
					let id = QL::SubscriptionId( (i+1)<<16 | 1u<<underlying(event) );
					auto ack = server->Subscribe( Ƒ("subscription {0}{2}{{ {1}{2}(subscriptionId:$id){{ id }} }}", table.Capitalized, table.Name, participle), jobject{{"id", id}}, listener, user );
					co_await Any( *ack );
				}
			}
			catch( const std::exception& e ){
				ERR( "Could not subscribe to the {} events - the calendars held follow no edit of that table until the next reconnect or restart:  {}", table.Name, e.what() );
			}
		}
	}

	//The first round is this coroutine's, under the _running claim:  an event from the first subscription on waits for the load
	//here and is answered by a round after it.
	α Calendars::ConfigureAwait::Execute()ι->VoidTask{
		auto listener = ms<TableListener>();
		{
			lg _{ _mutex };
			_server = _ql;
			_user = _executer;
			_listener = listener;
			_stopped = false;
			_dirty = false;
			_running = true;
		}
		co_await subscribe( _ql, _executer, listener );
		co_await load();
		bool more;
		{
			lg _{ _mutex };
			more = _dirty && !_stopped;
			_running = more;
		}
		if( more )
			rounds();
		Resume();
	}
	α Calendars::Configure( sp<QL::IQL> ql, UserPK executer, SL sl )ι->ConfigureAwait{ return ConfigureAwait{ move(ql), executer, sl }; }

	α Calendars::Reload( sp<QL::IQL> ql )ι->void{
		{
			lg _{ _mutex };
			if( !_listener )
				return;
			_server = move( ql );
		}
		request( Duration::zero() );
	}

	α Calendars::Shutdown( bool terminate, SL sl )ι->void{
		sp<QL::IQL> server; sp<QL::IListener> listener;
		{
			lg _{ _mutex };
			_stopped = true;
			if( _timer )
				_timer.Cancel();
			server = move( _server );
			listener = move( _listener );
			_listeners.clear();
		}
		if( server && listener && !terminate )
			server->Unsubscribe( move(listener), {}, sl );
	}

	α Calendars::Find( uint32 id )ι->sp<const Schedule>{
		lg _{ _mutex };
		let p = _snapshot->ById.find( id );
		return p==_snapshot->ById.end() ? nullptr : p->second.Calendar;
	}
	α Calendars::Find( sv slug )ι->sp<const Schedule>{
		lg _{ _mutex };
		let p = _snapshot->BySlug.find( slug );
		return p==_snapshot->BySlug.end() ? nullptr : p->second;
	}
	α Calendars::Listen( Listener fn )ι->uint{
		lg _{ _mutex };
		_listeners.emplace( ++_listenerHandle, move(fn) );
		return _listenerHandle;
	}
	α Calendars::StopListen( uint handle )ι->void{
		lg _{ _mutex };
		_listeners.erase( handle );
	}
	α Calendars::LoadCount()ι->uint{
		lg _{ _mutex };
		return _loads;
	}
}