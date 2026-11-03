#include "BlockingPool.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/process/thread.h>
#include <jde/fwk/settings.h>

namespace Jde::Opc::Gateway{
	namespace asio = boost::asio;
	namespace{
		struct Pool final{
			Pool( uint threads )ε: Guard{ asio::make_work_guard(Ctx) }{
				Workers.reserve( threads );
				for( uint i=0; i<threads; ++i )
					Workers.emplace_back( [this, i]{ Thread::SetName( Ƒ("Blocking[{}]", i) ); Ctx.run(); } );
			}
			~Pool(){ Guard.reset(); }//the threads run what is queued, then join - Workers goes first.
			asio::io_context Ctx;
			asio::executor_work_guard<asio::io_context::executor_type> Guard;
			vector<std::jthread> Workers;
		};
		Ω threads()ι->uint{ return std::max<uint>( 1, Settings::FindNumber<uint>("/workers/blocking/threads").value_or(2) ); }
		std::mutex _mutex;
		up<Pool> _pool;
		bool _stopped{};
	}
	α BlockingPool::Post( absl::AnyInvocable<void()> work )ι->void{
		{
			std::lock_guard _{ _mutex };
			if( !_stopped ){
				if( !_pool )
					_pool = mu<Pool>( threads() );
				asio::post( _pool->Ctx, std::move(work) );
				return;
			}
		}
		work();
	}
	α BlockingPool::Stop()ι->void{
		up<Pool> pool;
		{
			std::lock_guard _{ _mutex };
			_stopped = true;
			pool = std::move( _pool );
		}
		pool.reset();//joins off the lock:  a job that Posts while this waits for it would block on the lock, and this on the job.
	}
	α BlockingPool::Run( absl::AnyInvocable<void()> work, absl::AnyInvocable<void(up<Exception>)> onError, SL sl )ι->void{
		Post( [work=std::move(work), onError=std::move(onError), sl]()mutable{
			try{
				THROW_IFX( Process::ShuttingDown(), Exception(sl, {EHttpStatus::ServiceUnavailable}, "The gateway is shutting down.") );
				work();
			}
			catch( std::runtime_error& e ){
				Jde::Post( [onError=std::move(onError), error=Exception::From(std::move(e))]()mutable{ onError( std::move(error) ); } );
			}
		} );
	}
}