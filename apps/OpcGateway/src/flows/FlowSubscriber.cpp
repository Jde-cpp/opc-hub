#include "FlowSubscriber.h"
#include <jde/access/Authorize.h>
#include <jde/app/client/IAppClient.h>
#include "../GatewayAppClient.h"
#include "../types/proto/opc.FromServer.h"
#include "FlowHost.h"

#define let const auto

namespace Jde::Opc::Gateway{
	FlowSubscriber::FlowSubscriber( wp<GatewaySocketSession> session, Jde::RequestId requestId, sp<FlowFeed> feed, bool hops )ι:
		_session{ move(session) }, _requestId{ requestId }, _feed{ move(feed) }, _hops{ hops }
	{}
	α FlowSubscriber::End()ι->void{
		if( !_ended.exchange(true) )
			_feed->Remove( this );
	}
	α FlowSubscriber::OnAck( const string& ack )ι->void{
		if( auto session = _ended ? nullptr : _session.lock(); session )
			session->WriteFlow( shared_from_this(), FromServer::FlowSubscribeAckTrans(ack, _requestId) );
	}
	α FlowSubscriber::OnActivity( const string& activity )ι->void{
		if( auto session = _ended ? nullptr : _session.lock(); session )
			session->WriteFlow( shared_from_this(), FromServer::FlowActivityTrans(activity, _requestId) );
	}
	α FlowSubscriber::OnEnded( EHttpStatus status, string what )ι->void{
		if( auto session = _session.lock(); session )
			session->FlowEnded( shared_from_this(), status, move(what) );
	}

	α GatewaySocketSession::FlowSubscribe( FromClient::FlowSubscribe&& request, Jde::RequestId requestId )ι->void{
		try{
			LogRead( Ƒ("FlowSubscribe: flow: {}, hops: {}", request.flow(), request.hops()), requestId );
			if( !Session() ){
				WriteException( Exception{"Send sessionId before subscribing."}, requestId );
				return;
			}
			if( !request.flow() )
				throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::BadRequest}, "FlowSubscribe needs flow, a flows id." };
			auto& host = FlowHost::Instance();
			let state = host.Find( request.flow() );
			AppClient()->Acl()->Test( "gateway", "flows", state ? state->Row.Guid : string{}, Access::ERights::Read, UserPK(), SRCE_CUR );//the table's for a flow that isn't there, before a caller learns of an id.
			host.TestEnabled( SRCE_CUR );
			if( !state || state->Row.Deleted )
				throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::NotFound}, "Flow {} {}.", request.flow(), state ? "is deleted" : "not found" };
			auto feed = state->Running ? host.Feed( request.flow() ) : nullptr;
			if( !feed )//stopped since, when it was running.
				throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::Conflict}, "Flow '{}' is not running{}.", state->Row.Name, state->Fault ? Ƒ(":  {}", state->Fault->Message) : string{} };
			auto subscriber = ms<FlowSubscriber>( SharedFromThis(), requestId, move(feed), request.hops() );
			{//claimed here, on the strand, so a second subscribe with the id is refused before either joins.
				lg _{ _histMutex };
				if( _closed )
					return;
				if( Holds(requestId) )
					throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::BadRequest}, "Subscription {} is live:  a FlowSubscribe's request id names it.", hex(requestId) };
				_flowSubscriptions.emplace( requestId, subscriber );
			}
			FromServer::FlowSubscribeAck ack;
			try{
				ack = subscriber->Join( state->Row.Name );
			}
			catch( runtime_error& ){
				RemoveFlow( requestId, subscriber.get() );
				throw;
			}
			//On the strand, as this is:  what the feed sends from the join on is dispatched to it, so written behind this.
			IWebsocketSession::Write( FromServer::FlowSubscribeAckTrans(ack.SerializeAsString(), requestId) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}
	α GatewaySocketSession::WriteFlow( sp<FlowSubscriber> subscriber, string&& message )ι->void{
		Dispatch( [this, self=SharedFromThis(), subscriber=move(subscriber), message=move(message)]()mutable{
			if( !subscriber->Ended() )
				IWebsocketSession::Write( move(message) );
		} );
	}
	α GatewaySocketSession::FlowEnded( sp<FlowSubscriber> subscriber, EHttpStatus status, string what )ι->void{
		Dispatch( [this, self=SharedFromThis(), subscriber=move(subscriber), status, what=move(what)]{
			if( subscriber->Ended() )
				return;
			let requestId = subscriber->Request();
			RemoveFlow( requestId, subscriber.get() );
			WriteException( Exception{SRCE_CUR, ExceptionArgs{status}, "{}", what}, requestId );
		} );
	}
	α GatewaySocketSession::RemoveFlow( Jde::RequestId subscription, const FlowSubscriber* only )ι->sp<FlowSubscriber>{
		sp<FlowSubscriber> subscriber;
		{
			lg _{ _histMutex };
			if( auto p = _flowSubscriptions.find(subscription); p!=_flowSubscriptions.end() && (!only || p->second.get()==only) ){
				subscriber = move( p->second );
				_flowSubscriptions.erase( p );
			}
		}
		if( subscriber )
			subscriber->End();
		return subscriber;
	}
	α GatewaySocketSession::FlowUnsubscribe( Jde::RequestId subscription, Jde::RequestId requestId )ι->void{
		LogRead( Ƒ("FlowUnsubscribe: {:x}", subscription), requestId );
		if( RemoveFlow(subscription) )
			WriteComplete( requestId );
		else
			WriteException( Ƒ("No subscription {:x}.", subscription), requestId, SRCE_CUR );
	}
}