#include "BlockingPool.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/process/thread.h>
#include <jde/fwk/settings.h>

namespace Jde::Opc::Gateway{
	namespace asio = boost::asio;
	namespace{
		struct Pool final{
			Pool( sv name, uint threads )ε: Guard{ asio::make_work_guard(Ctx) }{
				Workers.reserve( threads );
				for( uint i=0; i<threads; ++i )
					Workers.emplace_back( [this, name, i]{ Thread::SetName( Ƒ("{}[{}]", name, i) ); Ctx.run(); } );
			}
			~Pool(){ Guard.reset(); }//the threads run what is queued, then join - Workers goes first.
			asio::io_context Ctx;
			asio::executor_work_guard<asio::io_context::executor_type> Guard;
			vector<std::jthread> Workers;
		};
		//A pool started on its first job, with `Setting`'s thread count, two by default.
		struct Slot final{
			α Post( absl::AnyInvocable<void()> work )ι->void{
				{
					std::lock_guard _{ Mutex };
					if( !Stopped ){
						if( !Running )
							Running = mu<Pool>( Name, std::max<uint>(1, Settings::FindNumber<uint>(Setting).value_or(2)) );
						asio::post( Running->Ctx, std::move(work) );
						return;
					}
				}
				work();
			}
			α Stop()ι->void{
				up<Pool> pool;
				{
					std::lock_guard _{ Mutex };
					Stopped = true;
					pool = std::move( Running );
				}
				pool.reset();//joins off the lock:  a job that Posts while this waits for it would block on the lock, and this on the job.
			}
			α Run( absl::AnyInvocable<void()> work, absl::AnyInvocable<void(up<Exception>)> onError, SL sl )ι->void{
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
			const sv Name;
			const string Setting;
			std::mutex Mutex;
			up<Pool> Running;
			bool Stopped{};
		};
		Slot _blocking{ "Blocking", "/workers/blocking/threads" };
		Slot _discovery{ "Discovery", "/workers/discovery/threads" };
		Slot _flowReads{ "FlowRead", "/workers/flowReads/threads" };
	}
	α BlockingPool::Post( absl::AnyInvocable<void()> work )ι->void{ _blocking.Post( std::move(work) ); }
	α BlockingPool::Stop()ι->void{ _blocking.Stop(); }
	α BlockingPool::Run( absl::AnyInvocable<void()> work, absl::AnyInvocable<void(up<Exception>)> onError, SL sl )ι->void{ _blocking.Run( std::move(work), std::move(onError), sl ); }
	α DiscoveryPool::Stop()ι->void{ _discovery.Stop(); }
	α DiscoveryPool::Run( absl::AnyInvocable<void()> work, absl::AnyInvocable<void(up<Exception>)> onError, SL sl )ι->void{ _discovery.Run( std::move(work), std::move(onError), sl ); }
	α FlowReadPool::Stop()ι->void{ _flowReads.Stop(); }
	α FlowReadPool::Run( absl::AnyInvocable<void()> work, absl::AnyInvocable<void(up<Exception>)> onError, SL sl )ι->void{ _flowReads.Run( std::move(work), std::move(onError), sl ); }
}