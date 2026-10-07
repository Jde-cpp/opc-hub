#include <jde/web/server/IWebsocketSession.h>
#include <jde/fwk/log/log.h>
#include <jde/ql/ql.h>
#include <jde/ql/LocalSubscriptions.h>
#include "ServerImpl.h"
#include <jde/web/server/SubscribeLog.h>
#include <jde/app/proto/Common.pb.h>

#define let const auto

namespace Jde::Web::Server{
	//The session's QL listener: a change to any subscription this socket made is written back over it.
	struct SocketServerListener final: QL::IListener{
		SocketServerListener( sp<IWebsocketSession> session )ι: QL::IListener{ Ƒ("[{}]Socket", session->Id()) }, _session{ session }{}
		α OnChange( const jvalue& j, QL::SubscriptionId clientId )ε->void{ _session->WriteSubscription(j, clientId); }
		sp<IWebsocketSession> _session;
	};

	IWebsocketSession::IWebsocketSession( sp<IRestStream>&& stream, beast::flat_buffer&& buffer, TRequestType request, tcp::endpoint&& userEndpoint, uint32 connectionIndex )ι:
		_userEndpoint{ userEndpoint },
		_id{ connectionIndex },
		_stream{ stream->CreateSocketStream(move(buffer)) },
		_initialRequest{ move(request) }
	{}

	α IWebsocketSession::Run()ι->void{
		auto stream = StreamPtr();
		if( !stream )
			return;//closed before we started.
		net::dispatch( stream->Strand(), [self=shared_from_this(), stream]()mutable{//_listener & _initialRequest are strand-confined; Internal::Stop can Close concurrently right after RunSocketSession emplaces us.
			if( !self->StreamPtr() )
				return;//Close ran first - OnClose already stopped the listener; setting one now would leak the session in a sp cycle.
			self->LogRead( "Run", 0 );
			self->_listener = ms<SocketServerListener>( self );
			stream->DoAccept( move(self->_initialRequest), self );
		});
	}

	α IWebsocketSession::OnAccept( beast::error_code ec )ι->void{
		LogRead( "OnAccept", 0 );
		if( ec ){
			CodeException( static_cast<std::error_code>(ec), ELogTags::SocketServerRead );
			Close();
			return;
		}
		SendAck( Id() );
		DoRead();
	}

	α IWebsocketSession::DoRead()ι->void{
		if( auto stream = StreamPtr(); stream )
			stream->DoRead( shared_from_this() );
	}

	α IWebsocketSession::Write( string&& m )ι->void{
		if( auto stream = StreamPtr(); stream )
			stream->Write( move(m), shared_from_this() );
	}

	//Takes the text formatted, not a format and its args:  the caller's Ƒ keeps `{:x}`-style specs out of the entry's own text (a
	//stored entry is re-rendered from string arguments - issue #268), and a variadic overload could not carry the caller's SRCE.
	α IWebsocketSession::LogRead( string&& what, RequestId requestId, ELogLevel level, ELogTags tags, SL sl )ι->void{
		Logging::Log( level, tags, sl, "[{:x}.{:x}]{}", Id(), requestId, move(what) );
	}

	α IWebsocketSession::LogWrite( string&& what, RequestId requestId, ELogLevel level, SL sl )ι->void{
		Logging::Log( level, ELogTags::SocketServerWrite, sl, "[{:x}.{:x}]{}", Id(), requestId, move(what) );
	}

	α IWebsocketSession::LogWriteException( const runtime_error& e, RequestId requestId, ELogLevel level, SL sl )ι->void{
		if( let p = dynamic_cast<const Exception*>(&e); p ){
			p->SetLevel( ELogLevel::NoLog );
			sl = p->_sl;
		}
		Exception{ sl, level, "[{}.{}]{}", Ƒ("{:x}", Id()), hex(requestId), e.what() };
	}
	α IWebsocketSession::LogWriteException( str e, RequestId requestId, ELogLevel level, SL sl )ι->void{
		Exception{ sl, level, "[{}.{}]{}", Ƒ("{:x}", Id()), hex(requestId), move(e) };
	}

	α IWebsocketSession::Close()ι->void{//safe from any thread (e.g. Internal::Stop's shutdown thread) - hops to the stream's strand; every close path ends in OnClose, which stops the listener.
		if( auto stream = StreamPtr(); stream )
			stream->Close( shared_from_this() );
	}
	//_pendingQueriesMutex held.  The entry stays, its timer cancelled, for AddTimeout's resumption to erase - so that does not then
	//log "No pending query" against something another path removed.
	Ω take( std::pair<QueryClientAwait::Handle,sp<DurationTimer>>& entry )ι->QueryClientAwait::Handle{
		auto h = entry.first;
		entry.first = nullptr;
		entry.second->Cancel();
		return h;
	}
	//Not under _pendingQueriesMutex: a resumed coroutine can come back through _pendingQueries.
	Ω failPending( QueryClientAwait::Handle h, Exception&& e )ι->void{
		h.promise().SetExp( move(e) );
		h.resume();
	}
	//requestId's waiting handle, taken out of its entry: null if it was already taken, nullopt if there is no entry.
	//erase: AddTimeout's own resumption, which removes the entry instead.
	α IWebsocketSession::TakePending( RequestId requestId, bool erase )ι->optional<QueryClientAwait::Handle>{
		lg _{ _pendingQueriesMutex };
		auto it = _pendingQueries.find( requestId );
		if( it==_pendingQueries.end() )
			return nullopt;
		if( !erase )
			return take( it->second );
		auto h = it->second.first;
		_pendingQueries.erase( it );
		return h;
	}

	α IWebsocketSession::OnClose()ι->void{
		LogRead( "ServerSocket::OnClose.", 0 );
		Internal::RemoveSocketSession( Id() );
		if( auto log = Logging::FindLogger<SubscribeLog>(); log )
			log->Unsubscribe( Id() );
		if( _listener ){
			QL::Subscriptions::StopListen( _listener );
			_listener = nullptr;
		}
		//S3: a query still in flight would otherwise sit out its full timeout on a socket that is already gone.
		vector<QueryClientAwait::Handle> pending;
		{
			lg l{ _pendingQueriesMutex };
			pending.reserve( _pendingQueries.size() );
			for( auto it = _pendingQueries.begin(); it!=_pendingQueries.end(); ++it ){//iterator, not a structured binding: flat_map hands back a temporary.
				if( auto h = take(it->second); h )
					pending.push_back( h );
			}
		}
		for( auto h : pending )
			failPending( h, Exception{SRCE_CUR, {ELogTags::SocketServerWrite}, "[{}]Socket closed with the query still pending.", Ƒ("{:x}", Id())} );
		lg _{ _streamMutex };
		_stream = nullptr;
	}

	α IWebsocketSession::AddSubscription( string&& query, jobject vars, RequestId requestId, Jde::UserPK executer, SL sl )ε->flat_set<QL::SubscriptionId>{
		auto subs = QL::ParseSubscriptions( move(query), move(vars), Schemas(), sl );
		flat_set<QL::SubscriptionId> subscriptionIds;//client ids.
		vector<QL::Subscription> logSubs;
		for( auto sub=subs.begin(); sub!=subs.end();  ){
			if( !sub->Id )
				sub->Id = requestId;
			subscriptionIds.emplace( sub->Id );
			let isLog = sub->TableName=="logs";
			if( isLog )
				logSubs.emplace_back( move(*sub) );
			sub = isLog ? subs.erase( sub ) : next( sub );
		}
		if( !logSubs.empty() )
			Logging::GetLogger<SubscribeLog>().Add( shared_from_this(), move(logSubs) );
		if( _listener )
			QL::Subscriptions::Listen( _listener, move(subs), executer, sl );//gated: the ungated overload is for in-process registration.
		return subscriptionIds;
	}
	α IWebsocketSession::RemoveSubscription( vector<QL::SubscriptionId>&& ids, RequestId requestId, SL /*sl*/ )ι->void{
		try{
			QL::Subscriptions::StopListen( _listener, move(ids) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}
	α IWebsocketSession::Query( Proto::Query&& query, RequestId requestId, function<string(string&&, RequestId)>&& toProtoQuery )ι->QL::QLAwait<jvalue>::Task{
		let _ = shared_from_this();
		try{
			LogRead( Ƒ("GraphQL{}: {}", query.return_raw() ? "*" : "", query.text()), requestId );
			auto j = co_await QL::QLAwait<jvalue>( move(*query.mutable_text()), parse(move(*query.mutable_variables())).as_object(), UserPK(), LocalQL(), query.return_raw() );
			auto y = serialize( move(j) );
			LogWrite( Ƒ("GraphQL: {}", y.substr(0,100)), requestId );
			Write( toProtoQuery(move(y), requestId) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}

	α IWebsocketSession::AddTimeout( RequestId requestId, QueryClientAwait::Handle h, Duration timeout, SL sl )ι->TimerAwait::Task{
		//S3: the frame outlives the request.  QueryClientAwait keeps the session alive only until it resumes its caller, so once
		//QueryClientResults has done that the last reference can drop while this coroutine's resumption is still queued - and it
		//touches _pendingQueriesMutex below.  Hold a reference in the frame.
		let self = shared_from_this();
		auto timer = ms<DurationTimer>( timeout, sl );
		{
			lg l{ _pendingQueriesMutex };
			_pendingQueries.emplace( requestId, make_pair(h, timer) );
		}
		auto _ = co_await *timer;
		let pending = TakePending( requestId, true );
		if( pending && *pending )//null: answered, failed or closed ahead of the deadline.
			failPending( *pending, Exception{sl, {ELogTags::SocketServerWrite}, "Query {} timed out after {}", hex(requestId), Chrono::ToString(timeout)} );
		else if( !pending )
			CRITICALT( ELogTags::SocketServerRead, "[{}]No pending query", hex(requestId) );
	}
	α IWebsocketSession::QueryClient( QL::TableQL&& query, Jde::UserPK executer, QueryClientAwait::Handle h, SL sl )ι->void{
		let requestId = NextRequestId();
		AddTimeout( requestId, h, 10s, sl );
		SendQueryClient( move(query), executer, requestId );
	}
	α IWebsocketSession::QueryClientResults( string&& queryResult, RequestId requestId )ι->void{
		LogRead( Ƒ("QueryClientResults: {}", queryResult.substr(0,100)), requestId );
		let pending = TakePending( requestId );
		if( !pending || !*pending ){//no such request, or one already answered, failed or closed.
			LOG( pending ? ELogLevel::Warning : ELogLevel::Critical, ELogTags::SocketServerRead, "[{}]No pending query", hex(requestId) );
			return;
		}
		auto h = *pending;
		try{
			h.promise().SetValue( parse(move(queryResult)) );
		}
		catch( runtime_error& e ){
			h.promise().SetExp( Exception{SRCE_CUR, {ELogLevel::Warning}, move(e), "[{}]QueryClientResults parse exception", hex(requestId)} );
		}
		h.resume();
	}
	α IWebsocketSession::ResumeQueryException( RequestId requestId, Exception&& e )ι->bool{
		let pending = TakePending( requestId );
		if( !pending )
			return false;//not a query we issued - leave `e` intact for the caller's next router.
		if( *pending )
			failPending( *pending, move(e) );
		return true;
	}
	α IWebsocketSession::SetSessionId( SessionPK sessionId )ι->void{
		if( !_sessionInfo )
			_sessionInfo = ms<SessionInfo>();
		_sessionInfo->SessionId = sessionId;
	}
}