#include <jde/app/client/RemoteLog.h>
#include <jde/app/log/LogSettingsAwait.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/process/execution.h>
#include <jde/app/client/IAppClient.h>

#define let const auto

namespace Jde::App::Client{
	RemoteLog::RemoteLog( const jobject& settings, sp<IAppClient> client )ι:
		BufferedLogger{ settings, Json::FindNumber<uint32>(settings, "maxEntries").value_or(10'000) },
		_client{ move(client) },
		_maxBatch{ Json::FindNumber<uint32>(settings, "maxBatch").value_or(100) }{//#14: ~2 orders of magnitude under the server's 1 MB socket cap at any plausible entry size.
		Process::AddShutdown( this );
	}
	RemoteLog::~RemoteLog(){
		Process::RemoveShutdown( this );//first: the ctor registered a raw `this`, and nothing else takes it back out.
		Stop();
	}
	α RemoteLog::Shutdown( bool terminate, SL )ι->void{
		StopTimer();
		_mutex.lock();
		if( terminate )
			_mutex.unlock();
		else
			Send( false );//inline: a posted write is not guaranteed to run once the executor is stopping.
		ul _{ _mutex };
		_client = nullptr;
	}
	α RemoteLog::Init( sp<IAppClient> client )ι->void{
		SetLogTarget( "appServer", []()ι->LogTags*{ return Logging::FindLogger<RemoteLog>(); } );
		Logging::Add<RemoteLog>( "remote", move(client) );
	}

	α RemoteLog::Write( const Logging::Entry& m )ι->void{
		if( !empty(m.Tags & _tags) )//recursion guard
			return;
		bool firstDrop;
		{
			ul _{ _mutex };
			if( !_client )
				return;
			_entries.push_back( m );
			firstDrop = Cap();
			ArmTimer();
		}
		if( firstDrop )//once per outage, not once per entry.
			WARN( "Remote log buffer passed {} entries - the app server is not taking them.  Dropping the oldest.", MaxSize() );
	}
	α RemoteLog::Drop()ι->uint{
		let drop = _entries.size()/2;
		_entries.erase( _entries.begin(), _entries.begin()+drop );
		return drop;
	}
	α RemoteLog::Send( bool post )ι->void{
		if( _entries.empty() || !_client || !_client->Connected() ){
			_mutex.unlock();
			return;
		}
		auto entries = move( _entries );
		_entries.clear();//a moved-from vector is only "valid but unspecified", and the round's Size() reads it next.
		auto client = _client;
		let dropped = TakeDropped();
		_mutex.unlock();
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