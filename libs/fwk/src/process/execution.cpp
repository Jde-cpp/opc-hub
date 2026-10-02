#include "jde/fwk/log/logTags.h"
#include "jde/fwk/utils/collections.h"
#include <jde/fwk/process/execution.h>
#include <boost/asio.hpp>
#include <jde/fwk/settings.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/process/thread.h>
#include <jde/fwk/utils/Vector.h>

#define let const auto
namespace Jde{
	constexpr ELogTags _tags{ ELogTags::App };
	namespace asio = boost::asio;
	α ThreadCount()ι->unsigned{ return std::max(1u, Settings::FindNumber<unsigned>("/workers/executor/threads").value_or(std::thread::hardware_concurrency())); }
	sp<asio::io_context> _ioc;
	struct IoStrand{
		IoStrand( sp<asio::io_context> ioc )ι: Ioc{ move(ioc) }, Strand{ *Ioc }{}
		sp<asio::io_context> Ioc;//keeps the io_context alive while a poster holds the strand.
		asio::io_context::strand Strand;
	};
	sp<IoStrand> _ioStrand;
	mutex _executorMutex;//guards _ioc/_ioStrand creation & teardown - PostIO races the executor thread's shutdown reset.
	up<asio::executor_work_guard<asio::io_context::executor_type>> _keepAlive;
}
α Jde::Executor()ι->sp<asio::io_context>{
	lg _{ _executorMutex };
	if( !_ioc && !Process::Finalizing() ){//never resurrect after cleanup - a stray Post would otherwise re-create the io_context (and Run a new executor thread) with nothing left to tear them down.
		_ioc = ms<asio::io_context>( ThreadCount() );
		_ioStrand = ms<IoStrand>( _ioc );
		_keepAlive = mu<asio::executor_work_guard<asio::io_context::executor_type>>( _ioc->get_executor() );
	}
	return _ioc;
}
α Jde::ExecutorIoc()ι->sp<asio::io_context>{ lg _{ _executorMutex }; return _ioc; }

namespace Jde{
	static up<Vector<IShutdown*>> _shutdowns;
	α Execution::AddShutdown( IShutdown* shutdown )ι->void{
		if( !_shutdowns )
			_shutdowns = mu<Vector<IShutdown*>>();
	 	_shutdowns->push_back( shutdown );
	}

	struct CancellationSignals final{
		α Emit( asio::cancellation_type ct = asio::cancellation_type::all )ι->void;
		α Slot()ι->asio::cancellation_slot;
		α Signal()ι->sp<asio::cancellation_signal>;
		α Add( sp<asio::cancellation_signal> s )ι->void{ lg _(_mtx); _sigs.push_back(s); }
		α Remove( const sp<asio::cancellation_signal>& s )ι->void{ lg _(_mtx); std::erase(_sigs, s); }
		α Clear()ι->void{ lg _(_mtx); _sigs.clear(); }
		α Size()ι->uint{ lg _(_mtx); return _sigs.size(); }
	private:
		vector<sp<asio::cancellation_signal>> _sigs;
		mutex _mtx;
	};
	CancellationSignals _cancelSignals;
	α Execution::AddCancelSignal( sp<asio::cancellation_signal> s )ι->void{ _cancelSignals.Add(s); }
	α Execution::RemoveCancelSignal( const sp<asio::cancellation_signal>& s )ι->void{ _cancelSignals.Remove(s); }
	α Execution::CancelSignalCount()ι->uint{ return _cancelSignals.Size(); }

	α CancelSignals()->CancellationSignals&{ return _cancelSignals; }

	struct ExecutorContext final : IShutdown{
		ExecutorContext()ι:
			_thread{ [&](){
				TRACET( ELogTags::Test, "Ex[0]" );
				Execute();
			}}{
			_started.wait( false );
			INFO( "Created executor threadCount: {}.", ThreadCount() );
		}
		~ExecutorContext()ι{
			_cancelSignals.Clear();
		}
		α Shutdown( bool terminate, SL sl )ι->void override;
		Ω Started()ι->bool{ return _started.test(); }
		Ω Ioc()ι->sp<asio::io_context>{ return _ioc; }
	private:
		Ω Execute()ι->void;
		std::jthread _thread;
		static atomic_flag _started;
		static atomic_flag _stopped;
	};
	atomic_flag ExecutorContext::_started{};
	atomic_flag ExecutorContext::_stopped{};
	mutex _runMutex;//serializes first-call construction: concurrent Run()s used to each build an ExecutorContext, and SetExecutor's overwrite joined the loser's thread - parked in ioc->run() - until shutdown.
	α Execution::Run()->void{
		if( ExecutorContext::Started() )
			return;
		lg _{ _runMutex };
		if( !ExecutorContext::Started() ){//re-check: another thread may have constructed while this one waited - the ctor returns only after _started is set.
			if( auto keepAlive = Executor(); keepAlive )//null once finalizing - don't start a new executor thread at teardown.
				Process::SetExecutor( mu<ExecutorContext>() );
		}
	}

	α CancellationSignals::Emit( asio::cancellation_type ct )ι->void{
		lg _( _mtx );
		for( uint i=0; i<_sigs.size(); ++i ){
			TRACE( "Emitting cancellation signal {}.", i );
			_sigs[i]->emit( ct );
		}
	}
	α CancellationSignals::Slot()ι->asio::cancellation_slot{
		return Signal()->slot();
	}
	α CancellationSignals::Signal()ι->sp<asio::cancellation_signal>{
		lg _( _mtx );
		auto p = find_if( _sigs, [](auto& sig){return !sig->slot().has_handler();} );
		return p == _sigs.end() ? _sigs.emplace_back( ms<asio::cancellation_signal>() ) : *p;
	}
	α ExecutorContext::Shutdown( bool terminate, SL sl )ι->void{
		DBG( "Executor Shutdown: instances: {}.", _ioc.use_count() );
		{//may run twice, or after Execute() tore down - _keepAlive is already null then.
			lg _{ _executorMutex };
			if( _keepAlive ){
				_keepAlive->reset();
				_keepAlive = nullptr;
			}
		}
		if( _shutdowns )
			_shutdowns->erase( [=](auto p){p->Shutdown(terminate, sl);} );
		if( _ioc && terminate )
			_ioc->stop(); // Stop the `io_context`. This will cause `run()` to return immediately, eventually destroying the `io_context` and all of the sockets in it.
		else{
			_cancelSignals.Emit( asio::cancellation_type::all );
		}
		//Process::RemoveShutdown( this ); //deadlock
	}
	α ExecutorContext::Execute()ι->void{
		auto threads = Reserve<std::jthread>( ThreadCount() - 1 );
		Vector<Thread::ProcessThreadId> threadIds;
		threadIds.push_back( Thread::Id() );
		auto ioc = _ioc; //keep alive
		Thread::SetName( "Resolver" );
		for( auto i = ThreadCount() - 1; i > 0; --i ){
			threads.emplace_back( [ioc=ioc, &threadIds](){
				threadIds.push_back( Thread::Id() );
				ioc->run();
			});
		}
		TRACE( "Executor Started: instances: {}.", ioc.use_count() );

		boost::asio::ip::tcp::resolver resolver( *ioc );
    resolver.async_resolve( "localhost", "12345", [&](auto /*ec*/, auto /*endpoints*/){
			while( threadIds.size() < ThreadCount() )
				std::this_thread::sleep_for( 1ms );
			uint i=0;
			threadIds.visit( [&i](auto& id){
				Thread::SetName( id, Ƒ("Ex[{}]", i++) );
			});
		});
		_stopped.clear();
		_started.test_and_set();
		_started.notify_all();
		ioc->run();
		{//in-flight PostIO holds its own sp - clearing under the mutex keeps it from posting into a torn-down strand.
			lg _{ _executorMutex };
			_ioStrand = nullptr;
			_ioc.reset();
		}
		_started.clear();
		if( _shutdowns )
			_shutdowns->erase( [=](auto p){p->Shutdown(false);} );
		INFO( "Executor Stopped: instances: {}.", ioc.use_count() );
		for( auto& t : threads )
			t.join();
		DBG( "Removing Executor remaining instances: {}.", ioc.use_count()-1 );
		ioc.reset(); //need to clear out client connections.
		_cancelSignals.Clear();
		_started.clear();
		_stopped.test_and_set();
		_stopped.notify_all();
	}
}
α Jde::Post( absl::AnyInvocable<void()> f )ι->void{
	auto ctx = Executor();
	if( !ctx ){
		WARN( "Post after executor teardown - dropping work." );
		return;
	}
	asio::post( *ctx, std::move(f) );
	Execution::Run();
}
α Jde::PostIO( absl::AnyInvocable<void()> f )ι->void{
	Executor();
	sp<IoStrand> s;
	{ lg _{ _executorMutex }; s = _ioStrand; }
	if( s )//null while shutting down - the io threads are gone & the work could never run anyway.
		asio::post( s->Strand, std::move(f) );
	Execution::Run();
}
α Jde::Post( VoidAwait::Handle&& h )ι->void{
	auto ctx = Executor();
	if( !ctx ){
		WARN( "Post after executor teardown - coroutine will not resume." );
		h = nullptr;
		return;
	}
	asio::post( *ctx, [h](){h.resume();} );
	h = nullptr;
}
α Jde::Post( VoidAwait::Handle&& h, Exception&& e )ι->void{
	auto ctx = Executor();
	if( !ctx ){
		WARN( "Post after executor teardown - coroutine will not resume: {}", e.what() );
		h = nullptr;
		return;
	}
	asio::post( *ctx, [h, e = e.Move()]() mutable {
		h.promise().ResumeExp( move(*e), h );
	} );
	h = nullptr;
}