#include <jde/flows/Props.h>
#include "../nodes/Nodes.h"
#include "Sandbox.h"
#include "Values.h"

#define let const auto

namespace Jde::Opc::Flows::Js{
	namespace{
		constexpr ELogTags _tags{ ELogTags::Flow };
		constexpr uint _toldRejections{ 10 };//a node's, an entry:  the rest are counted in one line.

		//What a body sees beside the language (docs/flows/spec.md *The Function node*), over the node's C functions, which
		//no body reaches but through it.  It answers what makes a call's `node`:  each call has its own, so a send or a
		//done after an await or from a timer is its own message's.  `Date` is a function over the intrinsic's prototype
		//whose `now`, whose no-argument constructor and whose plain call take the flow's clock:  the intrinsic, which reads
		//the wall clock, is no longer reachable.  `Error.prepareStackTrace` and `Error.stackTraceLimit` go first:
		//quickjs-ng calls a function set in one and the `valueOf` of an object set in the other while it builds an error's
		//backtrace, and drops what they throw, the interrupt among them.
		constexpr sv _preamble{ R"js((function( native ){
	delete Error.prepareStackTrace;
	delete Error.stackTraceLimit;
	const { send, done, error, log, status, get, set, keys, env, clone, now, setTimer, clearTimer, id, name } = native;
	const Wall = Date;
	function FlowDate( ...at ){
		if( !new.target )
			return new Wall( now() ).toString();
		return Reflect.construct( Wall, at.length ? at : [now()], new.target );
	}
	Object.defineProperty( FlowDate, "name", { value: "Date", configurable: true } );
	Object.defineProperty( FlowDate, "length", { value: 7, configurable: true } );
	FlowDate.prototype = Wall.prototype;
	Object.defineProperty( Wall.prototype, "constructor", { value: FlowDate, writable: true, configurable: true } );
	FlowDate.now = now;
	FlowDate.parse = Wall.parse;
	FlowDate.UTC = Wall.UTC;
	const store = scope => ( { get: key => get( scope, key ), set: ( key, value ) => { set( scope, key, value ); }, keys: () => keys( scope ) } );
	const flow = Object.freeze( store(1) ), global = Object.freeze( store(2) );
	const warn = text => { log( 1, text ); }, info = text => { log( 0, text ); }, show = to => { status( to ); };
	Object.assign( globalThis, {
		Date: FlowDate,
		context: Object.freeze( { ...store(0), flow, global } ),
		flow, global,
		env: Object.freeze( { get: env } ),
		RED: Object.freeze( { util: Object.freeze( { cloneMessage: clone } ) } ),
		setTimeout: ( f, ms, ...args ) => setTimer( f, ms, false, args ),
		setInterval: ( f, ms, ...args ) => setTimer( f, ms, true, args ),
		clearTimeout: clearTimer,
		clearInterval: clearTimer,
		__block: () => {}
	} );
	return call => ( { id, name,
		send: ( msg, clone ) => { send( call, msg, clone ); },
		done: () => { done( call ); },
		error: ( err, msg ) => { error( call, err, msg ); },
		warn, log: info, status: show } );
}))js" };

		//A value as text, by its `toString`:  none, with the exception pending, where that threw.
		Ω text( JSContext* ctx, JSValueConst v )ι->optional<string>{
			size_t size{};
			let chars = JS_ToCStringLen( ctx, &size, v );
			if( !chars )
				return nullopt;
			string y{ chars, size };
			JS_FreeCString( ctx, chars );
			return y;
		}
		Ω idOf( const jobject& o )ι->MessageId{
			let v = o.if_contains( "_id" );
			boost::system::error_code ec;
			let y = v ? v->to_number<MessageId>( ec ) : 0;
			return ec ? 0 : y;
		}
		Ω source( const jobject& props, sv key )ε->optional<string>{
			let v = Prop::Find( props, key );
			THROW_IF( v && !v->is_string(), "`{}` must be text, JavaScript.", key );
			return v ? optional<string>{ v->get_string() } : nullopt;
		}
		Ω outputs( const jobject& props )ε->uint{
			let y = FunctionOutputs( props );
			THROW_IF( !y, "{}", y.error() );
			return *y;
		}
		//As Node-RED reads a body:  one that calls `node.done` finishes its messages itself.
		Ω callsDone( sv code )ι->bool{
			constexpr sv name{ "node.done" };
			for( auto at = code.find(name); at!=sv::npos; at = code.find(name, at+1) ){
				if( let next = code.find_first_not_of(" \t\r\n", at+name.size()); next!=sv::npos && code[next]=='(' )
					return true;
			}
			return false;
		}

		//A message a body was called with, until the body has returned, its promise settled, and the flow's hop is closed.
		struct Call final{
			uint32_t Seq;//what the call's `node` carries, never 0, which is On Start's and On Stop's:  32 bits, as it crosses.
			MessageId Id;
			Message Msg;//as received:  what a catch takes, and what a message sent as itself keeps of it.
			JSValue Promise{ JS_UNDEFINED };//what the body returned, until it is read.
			bool Open{ true };//the flow's hop:  the message is the node's still.
			bool Spent{};//its `_id` has gone out.
			bool Done{};//the body called `node.done`.
			optional<string> Failed;//what the call ended on.
		};
		struct Timer final{
			JSValue Callback;
			vector<JSValue> Args;
			Duration Every;//zero:  once.
			INode::TimerId Id;
		};

		//The Function node:  a JSContext of its flow's runtime, with the intrinsics JS_NewContext adds less `performance`,
		//whose `now` is the wall clock, its body compiled once as a strict async function of `msg`, Node-RED's way, so
		//a body may await, and what it returns is sent when its promise settles.  A message whose body still awaits is
		//held, `queue` of them at most, the oldest dropped past it.  Everything of a body runs in an Entry.
		struct Function final : INode{
			Function( const NodeDef& def, sp<Runtime> rt )ε:
				_rt{ move(rt) },
				_id{ def.Id },
				_name{ def.Name },
				_outputs{ outputs(def.Props) }{
				let code = source( def.Props, "code" );
				THROW_IF( !code, "`code` must be text, the body." );
				let onStart = source( def.Props, "onStart" );
				let onStop = source( def.Props, "onStop" );
				_usesDone = callsDone( *code );
				try{
					_ctx = JS_NewContextRaw( _rt->Rt );
					let added = _ctx && !JS_AddIntrinsicBaseObjects( _ctx ) && !JS_AddIntrinsicDate( _ctx ) && !JS_AddIntrinsicEval( _ctx )
						&& !JS_AddIntrinsicRegExp( _ctx ) && !JS_AddIntrinsicJSON( _ctx ) && !JS_AddIntrinsicProxy( _ctx ) && !JS_AddIntrinsicMapSet( _ctx )
						&& !JS_AddIntrinsicTypedArrays( _ctx ) && !JS_AddIntrinsicPromise( _ctx ) && !JS_AddIntrinsicWeakRef( _ctx ) && !JS_AddIntrinsicAToB( _ctx );
					THROW_IF( !added, "The flow's Function nodes need more than `function.memory`." );
					JS_SetContextOpaque( _ctx, this );
					Runtime::Entry entry{ *_rt };
					Install();
					_body = Compile( "code", *code, "msg,node" );
					if( onStart )
						_onStart = Compile( "onStart", *onStart, "node" );
					if( onStop )
						_onStop = Compile( "onStop", *onStop, "node" );
				}
				catch( ... ){
					Free();
					throw;
				}
			}
			~Function(){ Free(); }

			//On Start, which may send, set a context and start a timer.  What it throws, or rejects with before its jobs
			//are done, is the node's refusal;  one that awaits a timer goes on behind the flow's first messages.
			α Start()ε->void override{
				_live = true;
				if( JS_IsUndefined(_onStart) )
					return;
				_starting = true;
				auto ran = Run( [&]{ return Tab(_onStart, _started); } );
				_startFailure = move( ran.Failure );
				Finish( move(ran.Result) );
				_starting = false;
				if( let failure = std::exchange(_startFailure, {}); failure && !_rt->Halted() )//a stop under it is no refusal.
					THROW( "On Start:  {}", *failure );
			}
			α Stop()ι->void override{
				_stopped = true;
				if( JS_IsUndefined(_onStop) )
					return;
				auto ran = Run( [&]{ return Tab(_onStop, _stopping); }, true );
				if( ran.Failure )
					_errors.push_back( Ƒ("On Stop:  {}", *ran.Failure) );
				Finish( move(ran.Result), true );
			}
			α Receive( Message& m )ε->void override{
				if( !++_seq )
					++_seq;
				let seq = _seq;
				_current = seq;
				_calls.push_back( Call{seq, m.Id(), move(m)} );
				auto ran = Run( [&]()->optional<string>{
					auto& call = _calls.back();
					JSValue argv[2]{ ToJs(_ctx, call.Msg.Json), JS_UNDEFINED };
					argv[1] = JS_IsException( argv[0] ) ? JS_EXCEPTION : NewNode( seq );
					JSValue y = JS_IsException( argv[1] ) ? JS_EXCEPTION : JS_Call( _ctx, _body, JS_UNDEFINED, 2, argv );
					JS_FreeValue( _ctx, argv[0] );
					JS_FreeValue( _ctx, argv[1] );
					if( JS_IsException(y) )
						return _rt->TakeError( _ctx );
					call.Promise = y;
					return nullopt;
				} );
				if( auto call = ran.Failure ? Find( seq ) : nullptr )
					call->Failed = move( ran.Failure );
				Finish( move(ran.Result) );
				_current = 0;
				if( auto call = Find(seq); call && call->Open )
					Hold();
				while( _calls.size()>QueueLimit() ){//the oldest call still awaited goes, its message with it where the node holds it, as a queue's does.
					auto& oldest = _calls.front();
					if( oldest.Open )
						Drop( oldest.Id );
					Release( oldest );
					_calls.erase( _calls.begin() );
				}
			}
		private:
			struct Outcome final{ optional<string> Failure; Ran Result; };
			Ω Of( JSContext* ctx )ι->Function&{ return *static_cast<Function*>( JS_GetContextOpaque(ctx) ); }
			α Label()Ι->str{ return _name.empty() ? _id : _name; }
			α Find( uint32_t seq )ι->Call*{
				let p = seq ? find( _calls, seq, &Call::Seq ) : _calls.end();
				return p==_calls.end() ? nullptr : &*p;
			}
			α Release( Call& call )ι->void{
				if( JS_IsUndefined(call.Promise) )
					return;
				_rt->Handled( call.Promise );
				JS_FreeValue( _ctx, call.Promise );
				call.Promise = JS_UNDEFINED;
			}
			α Free()ι->void{
				if( !_ctx )
					return;
				for( auto&& [_, timer] : _timers )
					Free( timer );
				_timers.clear();
				for( auto& call : _calls )
					Release( call );
				_calls.clear();
				for( auto value : {&_started, &_stopping, &_node, &_body, &_onStart, &_onStop} )
					JS_FreeValue( _ctx, std::exchange(*value, JS_UNDEFINED) );
				_rt->Forget( _ctx );
				JS_FreeContext( _ctx );
				_ctx = nullptr;
			}
			α Free( Timer& timer )ι->void{
				JS_FreeValue( _ctx, timer.Callback );
				for( let& arg : timer.Args )
					JS_FreeValue( _ctx, arg );
			}

			//The node's C functions, each under `name` of what the preamble takes, and the preamble run over them.
			α Install()ε->void{
				struct Def final{ const char* Name; JSCFunction* Call; int Args; };
				const std::array<Def,13> natives{{
					{"send", Native<&Function::NativeSend>, 3}, {"done", Native<&Function::NativeDone>, 1}, {"error", Native<&Function::NativeError>, 3},
					{"log", Native<&Function::NativeLog>, 2}, {"status", Native<&Function::NativeStatus>, 1},
					{"get", Native<&Function::NativeGet>, 2}, {"set", Native<&Function::NativeSet>, 3}, {"keys", Native<&Function::NativeKeys>, 1},
					{"env", Native<&Function::NativeEnv>, 1}, {"clone", Native<&Function::NativeClone>, 1}, {"now", Native<&Function::NativeNow>, 0},
					{"setTimer", Native<&Function::NativeSetTimer>, 4}, {"clearTimer", Native<&Function::NativeClearTimer>, 1}
				}};
				JSValue native = JS_NewObject( _ctx );
				bool made = !JS_IsException( native );
				let define = [&]( const char* name, JSValue value ){//`value` is taken, set or not.
					if( made && !JS_IsException(value) )
						made = JS_SetPropertyStr( _ctx, native, name, value )>=0;
					else{
						JS_FreeValue( _ctx, value );
						made = false;
					}
				};
				for( let& entry : natives )
					define( entry.Name, JS_NewCFunction(_ctx, entry.Call, entry.Name, entry.Args) );
				define( "id", JS_NewStringLen(_ctx, _id.data(), _id.size()) );
				define( "name", JS_NewStringLen(_ctx, _name.data(), _name.size()) );
				JSValue setup = made ? JS_Eval( _ctx, _preamble.data(), _preamble.size(), "flows", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT ) : JS_EXCEPTION;
				JSValue node = JS_IsException( setup ) ? JS_EXCEPTION : JS_Call( _ctx, setup, JS_UNDEFINED, 1, &native );
				JS_FreeValue( _ctx, setup );
				JS_FreeValue( _ctx, native );
				THROW_IF( JS_IsException(node), "The node's sandbox could not be made:  {}", _rt->TakeError(_ctx) );
				_node = node;
			}
			//`code` as the body of a strict async function of `params`, on its own first line, so an error's line is the
			//code's own.
			α Compile( sv name, sv code, sv params )ε->JSValue{
				let source = Ƒ( "(async function({}){{{}\n}})", params, code );
				JSValue y = JS_Eval( _ctx, source.c_str(), source.size(), string{name}.c_str(), JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT );
				THROW_IF( JS_IsException(y), "`{}`:  {}", name, _rt->TakeError(_ctx) );
				if( !JS_IsFunction(_ctx, y) ){
					JS_FreeValue( _ctx, y );
					THROW( "`{}` does not read as a function's body.", name );
				}
				return y;
			}
			α NewNode( uint32_t seq )ι->JSValue{
				JSValue arg = JS_NewUint32( _ctx, seq );
				return JS_Call( _ctx, _node, JS_UNDEFINED, 1, &arg );
			}
			//On Start's call or On Stop's, its promise kept until it is read.
			α Tab( JSValueConst tab, JSValue& promise )ι->optional<string>{
				JSValue node = NewNode( 0 );
				JSValue y = JS_IsException( node ) ? JS_EXCEPTION : JS_Call( _ctx, tab, JS_UNDEFINED, 1, &node );
				JS_FreeValue( _ctx, node );
				if( JS_IsException(y) )
					return _rt->TakeError( _ctx );
				promise = y;
				return nullopt;
			}

			//One entry into the runtime:  `call`, which answers what it ended on, then the jobs it left.  An interrupt
			//under `call` is what it ended on, whatever the body made of it.
			α Run( auto&& call, bool stopping=false )ι->Outcome{
				Outcome y;
				Runtime::Entry entry{ *_rt, stopping };
				y.Failure = call();
				if( entry.Interrupted() )
					y.Failure = entry.Why();
				else
					entry.Drain( _ctx );
				y.Result = entry.Result();
				return y;
			}
			//What an entry left, read in an entry of its own, since the reading runs the bodies' getters:  each node whose
			//code ran has its promises read, what they returned sent and its errors raised;  a rejection nothing took is its
			//node's error, `_toldRejections` of them a node and entry, the rest counted;  and an entry is posted for the jobs a
			//cut drain left, and for what an interrupted reading did not read, which no call could, the interrupt's stack limit
			//in place.
			α Finish( Ran&& ran, bool stopping=false )ι->void{
				bool more;
				{
					Runtime::Entry entry{ *_rt, stopping };
					vector<Function*> nodes{ this };
					let add = [&]( JSContext* ctx )->Function&{
						auto& node = Of( ctx );
						if( find(nodes, &node)==nodes.end() )
							nodes.push_back( &node );
						return node;
					};
					for( let ctx : std::exchange(_rt->Unread, {}) )
						add( ctx );
					for( let ctx : ran.Touched )
						add( ctx );
					for( auto& [ctx, error] : ran.Failures )
						add( ctx )._errors.push_back( move(error) );
					if( ran.InDrain && !ran.Halted && ran.Culprit ){//what an interrupted job belonged to is not known:  each message its node holds ends on it.
						if( auto& node = add(ran.Culprit); !ran.Killed || !node.FailHeld(ran.Why) )
							node._errors.push_back( ran.Why );
					}
					for( uint i=0; i<nodes.size(); ++i ){
						if( !nodes[i]->Settle() ){
							for( ; i<nodes.size(); ++i )
								_rt->Unread.push_back( nodes[i]->_ctx );
						}
					}
					if( !entry.Interrupted() ){//else they wait for the next, which reads them.
						flat_map<JSContext*,uint> told, untold;
						for( auto& rejection : _rt->TakeRejected() ){
							if( !ran.Interrupted || !JS_IsUncatchableError(rejection.Reason) ){//the interrupt's own error, which a promise took, is told once, as the interrupt.
								auto& node = add( rejection.Ctx );
								if( told[rejection.Ctx]<_toldRejections && !entry.Interrupted() ){
									++told[rejection.Ctx];
									node._errors.push_back( Ƒ("A promise was rejected and nothing took it:  {}", ErrorText(rejection.Ctx, rejection.Reason)) );
								}
								else
									++untold[rejection.Ctx];
							}
							JS_FreeValue( rejection.Ctx, rejection.Promise );
							JS_FreeValue( rejection.Ctx, rejection.Reason );
						}
						for( let [ctx, count] : untold )
							Of( ctx )._errors.push_back( Ƒ("{} more promises were rejected and nothing took them.", count) );
					}
					for( auto node : nodes )
						node->Report();
					more = !stopping && !ran.Halted && ( entry.Interrupted() || JS_IsJobPending(_rt->Rt) );
				}
				if( more && !std::exchange(_rt->Continuing, true) ){
					Post( [this]{
						_rt->Continuing = false;
						auto ran = Run( []()->optional<string>{ return nullopt; } );
						Finish( move(ran.Result) );
					} );
				}
			}
			//Those whose body still awaits, and On Start's while the node starts, its refusal:  one that has returned has its
			//answer.
			α FailHeld( str why )ι->bool{
				bool held{};
				if( _starting && !JS_IsUndefined(_started) && JS_PromiseState(_ctx, _started)==JS_PROMISE_PENDING ){
					if( !_startFailure )
						_startFailure = why;
					held = true;
				}
				for( auto& call : _calls ){
					if( !call.Open || call.Failed || JS_IsUndefined(call.Promise) || JS_PromiseState(_ctx, call.Promise)!=JS_PROMISE_PENDING )
						continue;
					call.Failed = why;
					held = true;
				}
				return held;
			}
			//Each call's promise, read once it has settled:  what it was fulfilled with is sent, what it was rejected with is
			//the node's error with the call's message, and the message is finished, unless the body is to call `node.done`,
			//or has, which finishes it whether or not its promise has settled.  False where the entry was interrupted before
			//all were read:  the rest are the next entry's.
			α Settle()ι->bool{
				bool read{ true };
				for( auto tab : {&_started, &_stopping} ){
					if( JS_IsUndefined(*tab) || JS_PromiseState(_ctx, *tab)==JS_PROMISE_PENDING || !(read = read && !_rt->Interrupted()) )
						continue;
					if( JS_PromiseState(_ctx, *tab)==JS_PROMISE_REJECTED ){
						JSValue reason = JS_PromiseResult( _ctx, *tab );
						auto error = ErrorText( _ctx, reason );
						JS_FreeValue( _ctx, reason );
						if( tab==&_started && _starting ){
							if( !_startFailure )//an interrupt is what it ended on, whatever the promise says.
								_startFailure = move( error );
						}
						else
							_errors.push_back( Ƒ("{}:  {}", tab==&_started ? "On Start" : "On Stop", error) );
					}
					_rt->Handled( *tab );
					JS_FreeValue( _ctx, std::exchange(*tab, JS_UNDEFINED) );
				}
				for( uint i=0; i<_calls.size(); ++i ){//by index:  what is sent runs getters, which may call back in.
					auto& call = _calls[i];
					if( !call.Failed && !JS_IsUndefined(call.Promise) ){
						if( !(read = read && !_rt->Interrupted()) )
							continue;
						let state = JS_PromiseState( _ctx, call.Promise );
						if( state==JS_PROMISE_PENDING ){
							if( call.Open && call.Done ){
								if( call.Seq!=_current )
									Done( call.Id );
								call.Open = false;
							}
							continue;
						}
						JSValue result = state==JS_PROMISE_NOT_A_PROMISE ? JS_DupValue( _ctx, call.Promise ) : JS_PromiseResult( _ctx, call.Promise );
						if( state==JS_PROMISE_REJECTED )
							call.Failed = ErrorText( _ctx, result );
						else if( !Deliver(result, &call, false) )
							call.Failed = _rt->TakeError( _ctx );
						JS_FreeValue( _ctx, result );
						Release( call );
					}
					if( call.Failed ){
						Release( call );
						auto error = *std::exchange( call.Failed, {} );
						if( error==OutOfMemory )//what the call held in cycles goes now, not at the next threshold.
							JS_RunGC( _rt->Rt );
						if( std::exchange(call.Open, false) )
							Fail( move(call.Msg), move(error) );
						else
							_errors.push_back( move(error) );
					}
					else if( call.Open && (call.Done || (!_usesDone && JS_IsUndefined(call.Promise))) ){
						if( call.Seq!=_current )//the call in hand is finished as Receive returns.
							Done( call.Id );
						call.Open = false;
					}
				}
				std::erase_if( _calls, []( let& call ){ return !call.Open && JS_IsUndefined( call.Promise ); } );
				return read;
			}
			α Report()ι->void{
				for( auto& error : std::exchange(_errors, {}) ){
					if( !_stopped )
						Error( move(error) );
					else
						ERRT( _tags, "[{}.{}]{}", _rt->Flow, Label(), error );
				}
			}

			//What a body returned or sent:  a message, or a list per output of a message or a list of them.  Each goes out
			//as a clone, under a new `_id` with the call's message as its `_parent`, but the first when `clone` is false, a
			//return's or a `node.send( msg, false )`'s, which is the call's own message going on.  False, with the exception
			//pending, for what is no message.
			α Deliver( JSValueConst v, Call* call, bool clone )ι->bool{
				if( JS_IsNull(v) || JS_IsUndefined(v) )
					return true;
				bool own{ !clone };
				if( !JS_IsArray(v) )
					return SendOne( v, 0, call, own );
				int64_t ports{};
				if( JS_GetLength(_ctx, v, &ports)<0 )
					return false;
				bool sent{ true };
				for( int64_t port=0; sent && port<std::min<int64_t>(ports, _outputs); ++port ){
					JSValue out = JS_GetPropertyInt64( _ctx, v, port );
					sent = !JS_IsException( out );
					if( sent && JS_IsArray(out) ){
						int64_t count{};
						sent = JS_GetLength( _ctx, out, &count )>=0;
						if( sent && count>(int64_t)QueueLimit() ){
							JS_ThrowRangeError( _ctx, "An output was sent more messages at once than `queue` holds." );
							sent = false;
						}
						for( int64_t i=0; sent && i<count; ++i ){
							JSValue m = JS_GetPropertyInt64( _ctx, out, i );
							sent = !JS_IsException( m ) && ( JS_IsNull(m) || JS_IsUndefined(m) || SendOne(m, port, call, own) );
							JS_FreeValue( _ctx, m );
						}
					}
					else if( sent && !JS_IsNull(out) && !JS_IsUndefined(out) )
						sent = SendOne( out, port, call, own );
					JS_FreeValue( _ctx, out );
				}
				return sent;
			}
			α SendOne( JSValueConst m, uint port, Call* call, bool& own )ι->bool{
				jvalue json;
				if( !JS_IsObject(m) || JS_IsFunction(_ctx, m) || JS_IsArray(m) ){
					JS_ThrowTypeError( _ctx, "A message is an object." );
					return false;
				}
				if( !ToJson(_ctx, m, json, _rt->Limits.Memory) )
					return false;
				if( !json.is_object() ){
					JS_ThrowTypeError( _ctx, "A message is an object." );
					return false;
				}
				auto& o = json.get_object();
				let itself = std::exchange( own, false ) && call && call->Open && !call->Spent;
				if( itself ){
					let& was = call->Msg.Json;
					o["_id"] = call->Id;
					if( let parent = was.if_contains("_parent") )
						o["_parent"] = *parent;
					else
						o.erase( "_parent" );
					if( let time = o.contains("_time") ? nullptr : was.if_contains("_time") )
						o["_time"] = *time;
					call->Spent = true;
					Send( Message{move(o)}, port );
					return true;
				}
				auto out = Make( call ? call->Id : idOf(o) );
				for( auto& member : o ){
					if( member.key()!="_id" && member.key()!="_parent" )
						out.Json[member.key()] = move( member.value() );
				}
				Send( move(out), port );
				return true;
			}

			//A timer's callback, in an entry of its own:  what it throws is the node's error with no message.
			α Fire( uint32_t id )ι->void{
				let p = _timers.find( id );
				if( p==_timers.end() )
					return;
				let repeats = p->second.Every>Duration::zero();
				Timer timer{ JS_DupValue(_ctx, p->second.Callback), {}, {}, {} };
				for( let& arg : p->second.Args )
					timer.Args.push_back( JS_DupValue(_ctx, arg) );
				if( !repeats ){
					Free( p->second );
					_timers.erase( p );
				}
				auto ran = Run( [&]()->optional<string>{
					JSValue y = JS_Call( _ctx, timer.Callback, JS_UNDEFINED, (int)timer.Args.size(), timer.Args.data() );
					Free( timer );
					if( JS_IsException(y) )
						return _rt->TakeError( _ctx );
					JS_FreeValue( _ctx, y );
					return nullopt;
				} );
				if( ran.Failure )
					_errors.push_back( move(*ran.Failure) );
				Finish( move(ran.Result) );
				if( let again = repeats ? _timers.find( id ) : _timers.end(); again!=_timers.end() )
					again->second.Id = Schedule( again->second.Every, [this, id]{ Fire( id ); } );
			}

			//A C function of the node as quickjs-ng calls it:  nothing of C++ crosses its frames, and none answers before
			//the flow has taken the node, which a body that closes its own function early would otherwise reach at a deploy's check.
			template<auto F> Ω Native( JSContext* ctx, JSValueConst, int, JSValueConst* argv )ι->JSValue{
				auto& self = Of( ctx );
				if( !self._live )
					return JS_ThrowPlainError( ctx, "The node has not started." );
				try{
					return ( self.*F )( argv );
				}
				catch( const std::exception& e ){
					return JS_ThrowPlainError( ctx, "%s", e.what() );
				}
			}
			α CallOf( JSValueConst seq )ι->Call*{
				uint32_t y{};
				JS_ToUint32( _ctx, &y, seq );
				return Find( y );
			}
			α NativeSend( JSValueConst* argv )ε->JSValue{//( call, msg, clone )
				let clone = !JS_IsBool( argv[2] ) || JS_ToBool( _ctx, argv[2] )>0;
				return Deliver( argv[1], CallOf(argv[0]), clone ) ? JS_UNDEFINED : JS_EXCEPTION;
			}
			α NativeDone( JSValueConst* argv )ε->JSValue{//( call ):  read when the entry's promises are.
				if( auto call = CallOf(argv[0]) )
					call->Done = true;
				return JS_UNDEFINED;
			}
			α NativeError( JSValueConst* argv )ε->JSValue{//( call, err, msg ):  with a message, what the catch nodes take;  without, logged and counted.
				auto error = text( _ctx, argv[1] );
				if( !error )
					return JS_EXCEPTION;
				if( !JS_IsObject(argv[2]) ){
					Error( move(*error) );
					return JS_UNDEFINED;
				}
				jvalue json;
				if( !ToJson(_ctx, argv[2], json, _rt->Limits.Memory) )
					return JS_EXCEPTION;
				if( !json.is_object() )
					return JS_ThrowTypeError( _ctx, "A message is an object." );
				auto& o = json.get_object();
				let id = idOf( o );
				if( let held = id ? find_if( _calls, [id]( let& call ){ return call.Open && call.Id==id; } ) : _calls.end(); held!=_calls.end() ){
					held->Open = false;
					Fail( Message{move(o)}, move(*error) );
					return JS_UNDEFINED;
				}
				let call = CallOf( argv[0] );
				auto m = Make( call ? call->Id : id );
				for( auto& member : o ){
					if( member.key()!="_id" && member.key()!="_parent" )
						m.Json[member.key()] = move( member.value() );
				}
				Fail( move(m), move(*error) );
				return JS_UNDEFINED;
			}
			α NativeLog( JSValueConst* argv )ε->JSValue{//( level, text ):  0 `node.log`, 1 `node.warn`;  a replay's, its capture.
				let line = text( _ctx, argv[1] );
				if( !line )
					return JS_EXCEPTION;
				if( Replaying() ){
					let call = Find( _current );
					Capture( call ? call->Id : 0, jvalue{*line} );
				}
				else
					LOG( JS_ToBool(_ctx, argv[0])>0 ? ELogLevel::Warning : ELogLevel::Information, _tags, "[{}.{}]{}", _rt->Flow, Label(), *line );
				return JS_UNDEFINED;
			}
			α NativeStatus( JSValueConst* argv )ε->JSValue{//( { fill, shape, text } ), or its text alone.
				NodeStatus status;
				if( JS_IsObject(argv[0]) ){
					for( let& [key, field] : {std::pair{"fill", &status.Fill}, std::pair{"shape", &status.Shape}, std::pair{"text", &status.Text}} ){
						JSValue v = JS_GetPropertyStr( _ctx, argv[0], key );
						let read = JS_IsUndefined( v ) || JS_IsNull( v ) ? optional<string>{ string{} } : JS_IsException( v ) ? nullopt : text( _ctx, v );
						JS_FreeValue( _ctx, v );
						if( !read )
							return JS_EXCEPTION;
						*field = move( *read );
					}
				}
				else if( !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]) ){
					auto read = text( _ctx, argv[0] );
					if( !read )
						return JS_EXCEPTION;
					status.Text = move( *read );
				}
				SetStatus( move(status) );
				return JS_UNDEFINED;
			}
			//( scope, key ):  0 the node's, 1 the flow's, 2 the gateway's.
			α NativeGet( JSValueConst* argv )ε->JSValue{
				let key = text( _ctx, argv[1] );
				if( !key )
					return JS_EXCEPTION;
				int32_t scope{};
				JS_ToInt32( _ctx, &scope, argv[0] );
				if( scope==2 ){
					let value = Global().Get( *key );
					return value.is_null() ? JS_UNDEFINED : ToJs( _ctx, value );
				}
				let value = ( scope==1 ? FlowContext() : Context() ).Get( *key );
				return value ? ToJs( _ctx, *value ) : JS_UNDEFINED;
			}
			α NativeSet( JSValueConst* argv )ε->JSValue{//( scope, key, value ):  `undefined` or null takes the key away.
				let key = text( _ctx, argv[1] );
				jvalue value;
				if( !key || !ToJson(_ctx, argv[2], value, _rt->Limits.Memory) )
					return JS_EXCEPTION;
				int32_t scope{};
				JS_ToInt32( _ctx, &scope, argv[0] );
				if( scope==2 )
					Global().Set( *key, move(value) );
				else
					( scope==1 ? FlowContext() : Context() ).Set( *key, move(value) );
				return JS_UNDEFINED;
			}
			α NativeKeys( JSValueConst* argv )ε->JSValue{
				int32_t scope{};
				JS_ToInt32( _ctx, &scope, argv[0] );
				jarray keys;
				for( auto& key : scope==2 ? Global().Keys() : ( scope==1 ? FlowContext() : Context() ).Keys() )
					keys.emplace_back( move(key) );
				return ToJs( _ctx, jvalue{move(keys)} );
			}
			//( name ):  what the host gave the flow, then Node-RED's names for the node and the flow.
			α NativeEnv( JSValueConst* argv )ε->JSValue{
				let name = text( _ctx, argv[0] );
				if( !name )
					return JS_EXCEPTION;
				if( let value = _rt->Env.if_contains(*name) )
					return ToJs( _ctx, *value );
				let own = *name=="NR_NODE_ID" ? &_id : *name=="NR_NODE_NAME" ? &_name : *name=="NR_FLOW_NAME" ? &_rt->Flow : nullptr;
				return own ? JS_NewStringLen( _ctx, own->data(), own->size() ) : JS_UNDEFINED;
			}
			α NativeClone( JSValueConst* argv )ε->JSValue{
				jvalue json;
				return ToJson( _ctx, argv[0], json, _rt->Limits.Memory ) ? ToJs( _ctx, json ) : JS_EXCEPTION;
			}
			α NativeNow( JSValueConst* )ε->JSValue{//the flow's clock, in ms.
				return JS_NewFloat64( _ctx, (double)duration_cast<milliseconds>(Now().time_since_epoch()).count() );
			}
			//( callback, ms, repeats, args ):  a wait under a millisecond is one, as Node's is, so an interval of zero does
			//not hold the clock.  A node has `queue` timers at most.
			α NativeSetTimer( JSValueConst* argv )ε->JSValue{
				if( !JS_IsFunction(_ctx, argv[0]) )
					return JS_ThrowTypeError( _ctx, "A timer's callback is a function." );
				if( _timers.size()>=QueueLimit() )
					return JS_ThrowRangeError( _ctx, "The node has as many timers as `queue` allows." );
				double ms{};
				int64_t count{};
				if( JS_ToFloat64(_ctx, &ms, argv[1]) || JS_GetLength(_ctx, argv[3], &count)<0 )
					return JS_EXCEPTION;
				let wait = milliseconds{ (int64_t)std::clamp(std::isnan(ms) ? 1.0 : ms, 1.0, 2147483647.0) };
				Timer timer{ JS_DupValue(_ctx, argv[0]), {}, JS_ToBool(_ctx, argv[2])>0 ? Duration{wait} : Duration::zero(), {} };
				for( int64_t i=0; i<count; ++i ){
					timer.Args.push_back( JS_GetPropertyInt64(_ctx, argv[3], i) );
					if( JS_IsException(timer.Args.back()) ){
						timer.Args.pop_back();
						Free( timer );
						return JS_EXCEPTION;
					}
				}
				do
					++_nextTimer;
				while( !_nextTimer || _timers.contains(_nextTimer) );
				let id = _nextTimer;
				timer.Id = Schedule( wait, [this, id]{ Fire( id ); } );
				if( timer.Id )
					_timers.emplace( id, move(timer) );
				else//the flow has stopped.
					Free( timer );
				return JS_NewUint32( _ctx, id );
			}
			α NativeClearTimer( JSValueConst* argv )ε->JSValue{
				uint32_t id{};
				JS_ToUint32( _ctx, &id, argv[0] );
				if( let p = _timers.find(id); p!=_timers.end() ){
					Cancel( p->second.Id );
					Free( p->second );
					_timers.erase( p );
				}
				return JS_UNDEFINED;
			}

			const sp<Runtime> _rt;
			JSContext* _ctx{};
			const string _id, _name;
			const uint _outputs;
			bool _usesDone{};
			JSValue _node{ JS_UNDEFINED };//what makes a call's `node`.
			JSValue _body{ JS_UNDEFINED }, _onStart{ JS_UNDEFINED }, _onStop{ JS_UNDEFINED };
			JSValue _started{ JS_UNDEFINED }, _stopping{ JS_UNDEFINED };//On Start's promise and On Stop's, until each is read.
			bool _live{};//the flow has taken the node:  its C functions answer.
			bool _starting{}, _stopped{};
			optional<string> _startFailure;
			uint32_t _seq{}, _current{};//the last call, and the one Receive is in.
			vector<Call> _calls;//oldest first.
			flat_map<uint32_t,Timer> _timers;
			uint32_t _nextTimer{};//a body's timer ids, as they cross:  never 0, nor one in use.
			vector<string> _errors;//the node's, with no message, for the entry's end.
		};
	}
}
namespace Jde::Opc{
	α Flows::MakeFunction( const NodeDef& def, sp<Js::Runtime> sandbox )ε->up<INode>{ return mu<Js::Function>( def, move(sandbox) ); }
	α Flows::FunctionOutputs( const jobject& props )ι->std::expected<uint,string>{
		constexpr uint most{ 100 };
		let v = Prop::Find( props, "outputs" );
		boost::system::error_code ec;
		let y = v ? v->to_number<uint>( ec ) : 1;
		if( ec || y>most )
			return std::unexpected{ Ƒ("`outputs` must be a whole number, {} at most.", most) };
		return y;
	}
}