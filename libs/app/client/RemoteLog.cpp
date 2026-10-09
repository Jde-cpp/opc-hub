#include <jde/app/client/RemoteLog.h>
#include <jde/app/log/LogSettingsAwait.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/process/execution.h>
#include <jde/app/client/IAppClient.h>

#define let const auto

namespace Jde::App::Client{
	RemoteLog::RemoteLog( const jobject& settings, sp<IAppClient> client )ι:
		ILogger{ settings },
		_client{ move(client) },
		_delay{ Json::FindDuration(settings, "delay", ELogLevel::Error).value_or(1min) },
		_maxEntries{ Json::FindNumber<uint32>(settings, "maxEntries").value_or(10'000) },
		_maxBatch{ Json::FindNumber<uint32>(settings, "maxBatch").value_or(100) }{//#14: ~2 orders of magnitude under the server's 1 MB socket cap at any plausible entry size.
		Process::AddShutdown( this );
	}
	RemoteLog::~RemoteLog(){
		Process::RemoveShutdown( this );//first: the ctor registered a raw `this`, and nothing else takes it back out.
		{
			ul _{ _mutex };
			_delay = Duration::min();
			if( _timer )
				_timer->Cancel();
		}
		for( bool running=true; running; ){
			{
				ul _{ _mutex };
				running = _running;
			}
			if( running )
				std::this_thread::sleep_for( 1ms );
		}
	}
	α RemoteLog::Start( sp<IAppClient> client )ι->void{
		_client = move( client );
		Executor();//locks up if starts in StartTimer.
		Execution::Run();
	}
	α RemoteLog::Shutdown( bool terminate, SL )ι->void{
		{
			ul _{ _mutex };
			if( !_client )
				return;
			_delay = Duration::min();
			if( _timer )
				_timer->Cancel();
		}
		if( !terminate )
			Send( false );//inline: a posted write is not guaranteed to run once the executor is stopping.
		ul _{ _mutex };
		_client = nullptr;
	}
	α RemoteLog::Init( sp<IAppClient> client )ι->void{
		SetLogTarget( "appServer", []()ι->LogTags*{ return Logging::FindLogger<RemoteLog>(); } );
		if( auto log = Logging::Add<RemoteLog>("remote", client); log )
			log->Start( move(client) );
	}

	α RemoteLog::Write( const Logging::Entry& m )ι->void{
		if( !empty(m.Tags & _tags) )//recursion guard
			return;
		_mutex.lock();
		if( !_client ){
			_mutex.unlock();
			return;
		}
		if( _entries.size()>=_maxEntries ){
			let drop = _entries.size()/2;
			_entries.erase( _entries.begin(), _entries.begin()+drop );
			if( !_dropped )//once per outage, not once per entry.  Safe under _mutex: the recursion guard above returns before this lock.
				WARN( "Remote log buffer reached {} entries - the app server is not taking them.  Dropping the oldest.", _maxEntries );
			_dropped += drop;
		}
		_entries.push_back( m );
		if( !_timer )
			StartTimer();
		else
			_mutex.unlock();
	}
	//A loop, not a tail call.  Restarting itself meant the replacement coroutine was live while the one that spawned it was
	//still finishing, so no single flag could say "nobody is running" - only a count could.  One coroutine at a time makes
	//_running a plain bool, and every transition of it happens under _mutex: set here while the caller still holds the
	//lock, cleared in the same critical section as the final unlock.
	//_mutex is held on entry by every caller and released before the first suspend - the contract Write relies on.  Not analyzed:
	//the loop retakes and releases it around each round, which a per-function analysis cannot follow.
	ABSL_NO_THREAD_SAFETY_ANALYSIS α RemoteLog::StartTimer()ι->TimerAwait::Task{
		_running = true;
		for( bool again=true; again; ){
			again = false;
			if( _delay<=Duration::zero() ){//Shutdown/~RemoteLog set this, so a cancelled round stops here instead of arming another.
				_timer.reset();
				_running = false;
				_mutex.unlock();
				break;
			}
			//By reference, taken before the unlock: only this loop assigns _timer, and only under the lock.
			auto& timer = *(_timer = mu<DurationTimer>( _delay ));
			_mutex.unlock();
			let fired = co_await timer;
			if( fired )
				Send();//takes _mutex itself, and moves _entries out when it can send.
			_mutex.lock();
			//_timer is held for the whole round rather than dropped here.  Dropping it early is what let Write - which
			//starts a round only when _timer is null - spawn a second coroutine alongside this one.
			again = !_entries.empty();//also covers entries written during Send(), which used to wait for the next Write.
			if( !again ){
				_timer.reset();
				_running = false;
				_mutex.unlock();//last touch of `this`; ~RemoteLog can only see _running false after this releases.
			}
		}
	}
	α RemoteLog::Send( bool post )ι->void{
		vector<Logging::Entry> entries;
		sp<IAppClient> client;
		uint dropped{};
		{
			ul _{ _mutex };
			if( _entries.empty() || !_client || !_client->Connected() )
				return;
			entries = move( _entries );
			_entries.clear();//a moved-from vector is only "valid but unspecified", and StartTimer's `again` reads it next.
			client = _client;
			dropped = std::exchange( _dropped, 0 );
		}
		if( dropped )//outside the lock, and after the swap, so the count reported is the one this batch leaves behind.
			WARN( "Remote log dropped {} entries while the app server was unreachable.", dropped );
		//Neither path captures `this`: a posted lambda is not covered by ~RemoteLog's wait, so it must not outlive the
		//object holding a pointer to it.
		auto write = [maxBatch=_maxBatch]( sp<IAppClient> client, vector<Logging::Entry>&& entries )ι{
			//#14: one Transmission per batch, not one for the whole backlog.
			for( uint i=0; i<entries.size(); i+=maxBatch ){
				let end = std::min<uint>( i+maxBatch, entries.size() );
				vector<Logging::Entry> batch{ std::make_move_iterator(entries.begin()+i), std::make_move_iterator(entries.begin()+end) };
				if( !client->Write(move(batch)) ){
					WARN( "Remote log lost {} entries - the session closed before the batch reached it.", entries.size()-i );
					break;//the session is gone; every remaining batch would fail the same way.
				}
			}
		};
		if( post )
			Post( [write,client=move(client),entries=move(entries)]() mutable{ write(move(client), move(entries)); } );
		else
			write( move(client), move(entries) );//shutdown: see the declaration.
	}
}