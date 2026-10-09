#pragma once
#include "../usings.h"
#include <jde/fwk/str.h>
#include "../client.h"
#include <jde/fwk/chrono.h>
#include <jde/fwk/io/protobuf.h>

namespace Jde::Web::Client{
	struct IClientSocketSession;
	struct TimedPromiseType{
		α Log( SessionPK sessionId, steady_clock::time_point start, SL sl )ι->void{
			if( ShouldTrace(ELogTags::SocketClientRead) && ResponseMessage.size() ){
				const auto msg = Str::Format(ResponseMessage, MessageArgs).substr( 0, MaxLogLength() );
				LOGSL( ELogLevel::Trace, sl, ELogTags::SocketClientRead, "[{:x}]SocketReceive - {} - {}", sessionId, msg, Chrono::ToString(steady_clock::now() - start) );
			}
		}

		sv ResponseMessage;
		vector<string> MessageArgs;
	};
	//A request in flight:  the typed handle its answer resumes, and how to fail it without knowing that type.
	struct PendingTask{
		std::any Handle;
		function<void(Exception&&)> Fail;
	};

	struct IClientSocketVoidAwait{
		IClientSocketVoidAwait( string&& request, RequestId requestId, sp<IClientSocketSession> session )ι:
			_request{ move(request) }, _requestId{ requestId }, _session{ session }, _start{ steady_clock::now() }{}

		α Suspend( PendingTask&& task )ι->void;
	protected:
		α SessionId()ι->SessionPK;
		string _request;
		const RequestId _requestId;
		sp<IClientSocketSession> _session;
		steady_clock::time_point _start;
	};

	template<class T>
	struct TTimedTask final{
		struct promise_type : IExpectedPromise<TTimedTask<T>,T>, TimedPromiseType{};
	};

	template<class T>
	struct ClientSocketAwait final : IClientSocketVoidAwait, TAwait<T,TTimedTask<T>>{
		using base = TAwait<T,TTimedTask<T>>;
		ClientSocketAwait( string&& request, RequestId requestId, sp<IClientSocketSession> session, SRCE )ι;
		//The one place a request is serialised.  A template, so a bare `{}` first argument still picks the string overload.
		template<std::derived_from<google::protobuf::MessageLite> TProto>
		ClientSocketAwait( const TProto& request, RequestId requestId, sp<IClientSocketSession> session, SRCE )ι:
			ClientSocketAwait{ Protobuf::ToString(request), requestId, move(session), sl }{}
		ClientSocketAwait( ClientSocketAwait&& )=default;
		α Suspend()ι->void override{ IClientSocketVoidAwait::Suspend( {base::_h, [h=base::_h]( Exception&& e ){ h.promise().SetExp( move(e) ); h.resume(); }} ); }
		α await_resume()ε->T override;
	};

	Τ ClientSocketAwait<T>::ClientSocketAwait( string&& request, RequestId requestId, sp<IClientSocketSession> session, SL sl )ι:
		IClientSocketVoidAwait{ move(request), requestId, session }, base{ sl }
	{}

	Ŧ ClientSocketAwait<T>::await_resume()ε->T{
		base::CheckException();
		typename base::TPromise* p = base::Promise();
		THROW_IF( !p, "Not Connected" );
		if( auto e = p->MoveExp(); e )
			e->Throw();
		ASSERT( p->Value() );
		p->Log( SessionId(), _start, base::_sl );
		return move( *p->Value() );
	}
}
