#include "Sandbox.h"
#include "../nodes/Nodes.h"

#define let const auto

namespace Jde::Opc::Flows::Js{
	//What the interrupt's own error has to be built in where a body has filled the heap:  the error, its message and a
	//backtrace of ten frames, with room over.
	constexpr size_t _reserve{ 64*1024 };
	constexpr std::array<sv,3> _files{ "code:", "onStart:", "onStop:" };//what the node compiles under.

	Runtime::Runtime( const FunctionLimits& limits, string flow, jobject env )ε:
		Rt{ JS_NewRuntime() },
		Limits{ limits },
		Flow{ move(flow) },
		Env{ move(env) }{
		THROW_IF( !Rt, "The sandbox's runtime could not be made." );
		JS_SetMemoryLimit( Rt, Limits.Memory );
		JS_SetMaxStackSize( Rt, Limits.Stack );
		JS_SetGCThreshold( Rt, std::min(JS_GetGCThreshold(Rt), GcShare()) );
		JS_SetInterruptHandler( Rt, Interrupt, this );
		JS_SetHostPromiseRejectionTracker( Rt, Rejected, this );
	}
	Runtime::~Runtime(){
		for( let& rejection : _rejected ){
			JS_FreeValueRT( Rt, rejection.Promise );
			JS_FreeValueRT( Rt, rejection.Reason );
		}
		JS_FreeRuntime( Rt );
	}

	α Runtime::Interrupt( JSContext* ctx, void* opaque )ι->int{
		auto& rt = *static_cast<Runtime*>( opaque );
		return rt._entry && rt._entry->Expired( ctx, true );
	}
	α Runtime::Rejected( JSContext* ctx, JSValueConst promise, JSValueConst reason, bool handled, void* opaque )ι->void{
		auto& rejected = static_cast<Runtime*>( opaque )->_rejected;
		let p = find_if( rejected, [&]( let& r ){ return JS_VALUE_GET_PTR(r.Promise)==JS_VALUE_GET_PTR(promise); } );
		if( !handled ){
			if( p==rejected.end() )
				rejected.push_back( {ctx, JS_DupValue(ctx, promise), JS_DupValue(ctx, reason)} );
		}
		else if( p!=rejected.end() ){
			JS_FreeValue( ctx, p->Promise );
			JS_FreeValue( ctx, p->Reason );
			rejected.erase( p );
		}
	}
	α Runtime::Handled( JSValueConst promise )ι->void{
		let p = find_if( _rejected, [&]( let& r ){ return JS_VALUE_GET_PTR(r.Promise)==JS_VALUE_GET_PTR(promise); } );
		if( p==_rejected.end() )
			return;
		JS_FreeValueRT( Rt, p->Promise );
		JS_FreeValueRT( Rt, p->Reason );
		_rejected.erase( p );
	}
	α Runtime::TakeRejected()ι->vector<Rejection>{ return std::exchange( _rejected, {} ); }
	α Runtime::Forget( JSContext* ctx )ι->void{
		std::erase_if( _rejected, [&]( let& r ){
			if( r.Ctx!=ctx )
				return false;
			JS_FreeValueRT( Rt, r.Promise );
			JS_FreeValueRT( Rt, r.Reason );
			return true;
		} );
	}
	α Runtime::TakeError( JSContext* ctx )ι->string{
		JSValue error = JS_GetException( ctx );
		auto y = _entry && _entry->Interrupted() ? _entry->Why() : ErrorText( ctx, error );
		JS_FreeValue( ctx, error );
		return y;
	}

	Runtime::Entry::Entry( Runtime& rt, bool stopping )ι:
		_rt{ rt },
		_stopping{ stopping },
		_deadline{ steady_clock::now()+duration_cast<steady_clock::duration>(rt.Limits.Timeout) }{
		ASSERT( !rt._entry );
		rt._entry = this;
		JS_UpdateStackTop( rt.Rt );
		//The threshold after a collection is half as much again as what is live, which may be past the limit:  what is
		//garbage but for a cycle would then never go before the heap is full.
		if( JS_GetGCThreshold(rt.Rt)>rt.GcShare() )
			JS_SetGCThreshold( rt.Rt, rt.GcShare() );
	}
	Runtime::Entry::~Entry(){
		if( _ran.Interrupted ){
			JS_SetMemoryLimit( _rt.Rt, _rt.Limits.Memory );
			JS_SetMaxStackSize( _rt.Rt, _rt.Limits.Stack );
		}
		_rt._entry = nullptr;
	}
	α Runtime::Entry::Expired( JSContext* ctx, bool running )ι->bool{
		if( _ran.Interrupted )//the limits stay as the first interrupt left them:  a second raises nothing more.
			return true;
		let halted = !_stopping && _rt._halted.load( std::memory_order_relaxed );
		if( !halted && steady_clock::now()<_deadline )
			return false;
		_ran.Interrupted = true;
		_ran.Halted = halted;
		_ran.Killed = running;
		_ran.Culprit = ctx;
		JS_SetMemoryLimit( _rt.Rt, _rt.Limits.Memory+_reserve );
		JS_SetMaxStackSize( _rt.Rt, 1 );
		return true;
	}
	α Runtime::Entry::Drain()ι->void{
		let rt = _rt.Rt;
		let cut = _ran.Interrupted;
		while( !_ran.Interrupted && JS_IsJobPending(rt) && !Expired(JS_GetPendingJobContext(rt), false) ){
			JSContext* ctx{};
			let result = JS_ExecutePendingJob( rt, &ctx );
			if( !ctx )
				break;
			if( find(_ran.Touched, ctx)==_ran.Touched.end() )
				_ran.Touched.push_back( ctx );
			if( result<0 ){
				JSValue error = JS_GetException( ctx );
				if( !_ran.Interrupted )
					_ran.Failures.emplace_back( ctx, ErrorText(ctx, error) );
				JS_FreeValue( ctx, error );
			}
		}
		_ran.InDrain = !cut && _ran.Interrupted;
	}
	α Runtime::Entry::Why()Ι->string{
		return _ran.Halted
			? string{ "The flow stopped." }
			: Ƒ( "It ran past the timeout of {} ms.", duration_cast<milliseconds>(_rt.Limits.Timeout).count() );
	}
	α Runtime::Entry::Result()ι->Ran{
		_ran.Why = _ran.Interrupted ? Why() : string{};
		Ran y{ move(_ran) };
		_ran = Ran{};
		_ran.Interrupted = y.Interrupted;
		_ran.Halted = y.Halted;
		return y;
	}

	//Where an error was thrown, from the first of its backtrace's frames that is the node's own code:  `code:3:11`.
	Ω where( JSContext* ctx, JSValueConst error )ι->string{
		JSValue stack = JS_GetPropertyStr( ctx, error, "stack" );
		string y;
		size_t size{};
		if( let text = JS_IsString(stack) ? JS_ToCStringLen( ctx, &size, stack ) : nullptr ){
			sv frames{ text, size };
			for( let file : _files ){
				for( auto at = frames.find(file); at!=sv::npos && y.empty(); at = frames.find(file, at+1) ){
					if( at && frames[at-1]!='(' && frames[at-1]!=' ' )
						continue;
					let end = frames.find_first_of( ")\n", at );
					y = frames.substr( at, end==sv::npos ? end : end-at );
				}
				if( y.size() )
					break;
			}
			JS_FreeCString( ctx, text );
		}
		if( JS_IsException(stack) )
			JS_FreeValue( ctx, JS_GetException(ctx) );
		JS_FreeValue( ctx, stack );
		return y;
	}
}
namespace Jde::Opc{
	α Flows::Js::ErrorText( JSContext* ctx, JSValueConst error )ι->string{
		if( JS_IsNull(error) )
			return string{ OutOfMemory };
		string y;
		size_t size{};
		if( let text = JS_IsUninitialized(error) ? nullptr : JS_ToCStringLen(ctx, &size, error) ){
			y.assign( text, size );
			JS_FreeCString( ctx, text );
		}
		else{
			JS_FreeValue( ctx, JS_GetException(ctx) );
			return "An error that does not read as text.";
		}
		if( JS_IsError(error) && y!=OutOfMemory ){
			if( let at = where(ctx, error); at.size() )
				y += Ƒ( " ({})", at );
		}
		return y;
	}
	α Flows::MakeSandbox( const FunctionLimits& limits, string flow, jobject env )ε->sp<Js::Runtime>{
		return ms<Js::Runtime>( limits, move(flow), move(env) );
	}
	α Flows::Halt( Js::Runtime& sandbox )ι->void{ sandbox.Halt(); }
}