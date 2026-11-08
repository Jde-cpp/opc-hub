#include "jde/fwk/log/log.h"
#include <jde/fwk/process/execution.h>
#include <boost/asio.hpp>
#include <jde/fwk/utils/Vector.h>
#include <jde/fwk/process/thread.h>
#include <jde/fwk/settings.h>

#define let const auto
namespace Jde{
	constexpr ELogTags _tags{ ELogTags::App };
	namespace asio = boost::asio;
	//Idle -> Running -> Stopped, or Idle -> Stopped; never back, so nothing is rebuilt with nothing left to tear it down.
	struct Pool final{
		Ω Instance()ι->Pool&{ static Pool _instance; return _instance; }
		α Ioc()ι->sp<asio::io_context>;
		α OnThread()ι->bool{ rl _{ _mutex }; return _ioc && _ioc->get_executor().running_in_this_thread(); }
		α Running()ι->bool{ rl _{ _mutex }; return _ioc ? !_ioc->stopped() : _state.load()!=EState::Stopped; }
		α Release()ι->sp<asio::io_context>{ ul _{ _mutex }; return move( _ioc ); }
		α Start()ι->void;
		α Stop( bool terminate, SL sl )ι->void;
	private:
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Make()ι->void;
		enum class EState : uint8{ Idle, Running, Stopped };
		atomic<EState> _state{ EState::Idle };
		absl::Mutex _mutex;
		sp<asio::io_context> _ioc ABSL_GUARDED_BY(_mutex);
		up<asio::executor_work_guard<asio::io_context::executor_type>> _keepAlive ABSL_GUARDED_BY(_mutex);
		vector<std::jthread> _threads ABSL_GUARDED_BY(_mutex);
	};
}
α Jde::ThreadCount()ι->unsigned{ return std::max(1u, Settings::FindNumber<unsigned>("/workers/executor/threads").value_or(std::thread::hardware_concurrency())); }
α Jde::Executor()ι->sp<asio::io_context>{ return Pool::Instance().Ioc(); }

namespace Jde{
	static up<Vector<IShutdown*>> _shutdowns;
	α Execution::AddShutdown( IShutdown* shutdown )ι->void{
		if( !_shutdowns )
			_shutdowns = mu<Vector<IShutdown*>>();
	 	_shutdowns->push_back( shutdown );
	}

	α Execution::Start()ι->void{ Pool::Instance().Start(); }
	α Execution::Stop( bool terminate, SL sl )ι->void{ Pool::Instance().Stop( terminate, sl ); }
	α Execution::Release()ι->sp<asio::io_context>{ return Pool::Instance().Release(); }
	α Execution::OnThread()ι->bool{ return Pool::Instance().OnThread(); }
	α Execution::Running()ι->bool{ return Pool::Instance().Running(); }

	//asio starts a private thread for the first resolve, and it inherits the name of the thread that asked.
	Ω nameResolverThread( asio::io_context& ioc )ι->void{
		asio::post( ioc, [&ioc]{
			let name = Thread::Name();
			Thread::SetName( "Resolver" );
			auto resolver = ms<asio::ip::tcp::resolver>( ioc );
			resolver->async_resolve( "localhost", "12345", [resolver]( auto /*ec*/, auto /*endpoints*/ ){} );
			Thread::SetName( name );
		} );
	}
	//An exception leaves run() with the io_context intact, so the thread goes straight back in.
	Ω runHandlers( asio::io_context& ioc )ι->void{
		for( ;; ){
			try{
				ioc.run();
				return;
			}
			catch( ... ){
				CRITICAL( "An Executor event threw." );
			}
		}
	}
	α Pool::Make()ι->void{
		_ioc = ms<asio::io_context>( ThreadCount() );
		if( _state.load()==EState::Stopped )//never run, so what is posted to it is only ever destroyed with it.
			_ioc->stop();
		else
			_keepAlive = mu<asio::executor_work_guard<asio::io_context::executor_type>>( _ioc->get_executor() );
	}
	α Pool::Ioc()ι->sp<asio::io_context>{
		ASSERT( _state.load()!=EState::Idle );//Process::Startup starts the executor:  this is before it, and waits for it.
		ul _{ _mutex };
		if( !_ioc )
			Make();
		return _ioc;
	}
	α Pool::Start()ι->void{
		let count = ThreadCount();
		{
			ul _{ _mutex };
			auto idle = EState::Idle;
			if( !_state.compare_exchange_strong(idle, EState::Running) )//under the lock, so a Stop that follows finds every thread to join.
				return;
			if( !_ioc )
				Make();
			auto ioc = _ioc;
			nameResolverThread( *ioc );
			_threads.reserve( count );
			for( uint i=0; i<count; ++i )
				_threads.emplace_back( [ioc, i]{
					Thread::SetName( Ƒ("Ex[{}]", i) );
					runHandlers( *ioc );
				} );
		}
		INFO( "Created executor threadCount: {}.", count );
	}
	α Pool::Stop( bool terminate, SL sl )ι->void{
		if( _state.exchange(EState::Stopped)==EState::Stopped )
			return;
		sp<asio::io_context> ioc;
		{
			ul _{ _mutex };
			ioc = _ioc;
			_keepAlive = nullptr;
		}
		DBG( "Executor Shutdown: instances: {}.", ioc.use_count() );
		if( _shutdowns )
			_shutdowns->erase( [=](auto p){p->Shutdown(terminate, sl);} );
		if( ioc && terminate )
			ioc->stop(); // Stop the `io_context`. This will cause `run()` to return immediately, eventually destroying the `io_context` and all of the sockets in it.
		vector<std::jthread> threads;
		{
			ul _{ _mutex };
			threads = move( _threads );
		}
		threads.clear();//joins, outside the lock:  a handler still running may post.
		if( _shutdowns )
			_shutdowns->erase( [=](auto p){p->Shutdown(false);} );
		INFO( "Executor Stopped: instances: {}.", ioc.use_count() );
	}
}
α Jde::Post( absl::AnyInvocable<void()> f )ι->void{
	auto ctx = Executor();
	if( ctx->stopped() ){
		WARN( "Post after executor teardown - dropping work." );
		return;
	}
	asio::post( *ctx, std::move(f) );
}
α Jde::Post( VoidAwait::Handle&& h )ι->void{
	auto ctx = Executor();
	if( ctx->stopped() ){
		WARN( "Post after executor teardown - coroutine will not resume." );
		h = nullptr;
		return;
	}
	asio::post( *ctx, [h](){h.resume();} );
	h = nullptr;
}
α Jde::Post( VoidAwait::Handle&& h, Exception&& e )ι->void{
	auto ctx = Executor();
	if( ctx->stopped() ){
		WARN( "Post after executor teardown - coroutine will not resume: {}", e.what() );
		h = nullptr;
		return;
	}
	asio::post( *ctx, [h, e = e.Move()]() mutable {
		h.promise().ResumeExp( move(*e), h );
	} );
	h = nullptr;
}