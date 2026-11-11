#include "OeeSubscriber.h"
#include <jde/access/Authorize.h>
#include <jde/app/client/IAppClient.h>
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"
#include "../types/proto/opc.FromServer.h"
#include "OeeHost.h"
#include "OeeLineFeed.h"

#define let const auto

namespace Jde::Opc::Gateway{
	OeeSubscriber::OeeSubscriber( wp<GatewaySocketSession> session, Jde::RequestId requestId, sp<IOeeFeed> feed, UserPK user )ι:
		_session{ move(session) }, _requestId{ requestId }, _feed{ move(feed) }, _user{ user }
	{}
	α OeeSubscriber::End()ι->void{
		if( !_ended.exchange(true) )
			_feed->Remove( this );
	}
	α OeeSubscriber::Live()ι->void{
		lg _{ _mutex };
		_live = true;
		for( let& held : std::exchange(_held, {}) ){
			if( !_ended )
				Send( *held );
		}
		if( auto end = std::exchange(_end, nullopt); end )
			Ending( end->first, move(end->second) );
	}
	α OeeSubscriber::OnStatus( sp<const string> status )ι->void{
		if( _ended )
			return;
		lg _{ _mutex };
		if( _live )
			Send( *status );
		else
			_held.push_back( move(status) );
	}
	α OeeSubscriber::OnEnded( EHttpStatus status, string what )ι->void{
		{
			lg _{ _mutex };
			if( !_live ){//the ack isn't written:  the end follows it.
				_end = { status, move(what) };
				return;
			}
		}
		Ending( status, move(what) );
	}
	α OeeSubscriber::Ending( EHttpStatus status, string what )ι->void{
		if( auto session = _session.lock(); session )
			session->OeeEnded( shared_from_this(), status, move(what) );
	}
	α OeeSubscriber::Send( const string& status )ι->void{
		if( auto session = _session.lock(); session )
			session->WriteOee( shared_from_this(), FromServer::OeeStatusTrans(status, _requestId) );
	}

	α GatewaySocketSession::OeeSubscribe( FromClient::OeeSubscribe&& request, Jde::RequestId requestId )ι->void{
		try{
			LogRead( request.line() ? Ƒ("OeeSubscribe: line: {}", request.line()) : Ƒ("OeeSubscribe: machine: {}", request.machine()), requestId );
			if( !Session() ){
				WriteException( Exception{"Send sessionId before subscribing."}, requestId );
				return;
			}
			if( (request.machine()!=0)==(request.line()!=0) )
				throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::BadRequest}, "OeeSubscribe needs machine, an oee_machines id, or line, an oee_lines id:  one of the two." };
			auto& oee = OeeHost::Instance();
			sp<IOeeFeed> feed;
			if( request.line() ){//Read on the group of every machine it reaches, as its reads (spec *Authorization*).
				let reached = oee.Reach( request.line(), UserPK(), SRCE_CUR );
				if( reached.Line.Deleted )
					throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::NotFound}, "Line '{}' is deleted.", reached.Line.Name };
				oee.TestEnabled( SRCE_CUR );
				feed = oee.LineFeed( request.line() );
			}
			else{
				let machine = oee.Find( request.machine() );
				if( !machine ){
					AppClient()->Acl()->Test( "gateway", "histGroups", Access::ERights::Read, UserPK(), SRCE_CUR );//the table's, before a caller learns of an id.
					throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::NotFound}, "Machine {} not found.", request.machine() };
				}
				HistHost::Instance().Gate( machine->Row.Group, Access::ERights::Read, false, UserPK(), SRCE_CUR );//as the reads (spec *Authorization*).
				oee.TestEnabled( SRCE_CUR );
				feed = oee.Feed( request.machine() );
			}
			if( !feed ){//purged since its check, or OEE stopped.
				oee.TestEnabled( SRCE_CUR );
				throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::NotFound}, "{} {} not found.", request.line() ? "Line" : "Machine", request.line() ? request.line() : request.machine() };
			}
			auto subscriber = ms<OeeSubscriber>( SharedFromThis(), requestId, move(feed), UserPK() );
			{//claimed here, on the strand, so a second subscribe with the id is refused before either joins.
				lg _{ _histMutex };
				if( _closed )
					return;
				if( Holds(requestId) )
					throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::BadRequest}, "Subscription {} is live:  an OeeSubscribe's request id names it.", hex(requestId) };
				_oeeSubscriptions.emplace( requestId, subscriber );
			}
			OeeSubscribing( move(subscriber) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}
	α GatewaySocketSession::OeeSubscribing( sp<OeeSubscriber> subscriber )ι->VoidTask{
		auto self = SharedFromThis();//keep alive
		let requestId = subscriber->Request();
		try{
			let status = co_await subscriber->Join();
			Write( FromServer::OeeSubscribeAckTrans(status, requestId) );//answers the OeeSubscribe even once it has ended:  nothing follows it then.
			subscriber->Live();
		}
		catch( runtime_error& e ){
			RemoveOee( requestId, subscriber.get() );//an OeeUnsubscribe may have ended it already, and a later OeeSubscribe claimed the id.
			WriteException( move(e), requestId );
		}
	}
	α GatewaySocketSession::WriteOee( sp<OeeSubscriber> subscriber, string&& message )ι->void{
		Dispatch( [this, self=SharedFromThis(), subscriber=move(subscriber), message=move(message)]()mutable{
			if( !subscriber->Ended() )
				IWebsocketSession::Write( move(message) );
		} );
	}
	α GatewaySocketSession::OeeEnded( sp<OeeSubscriber> subscriber, EHttpStatus status, string what )ι->void{
		Dispatch( [this, self=SharedFromThis(), subscriber=move(subscriber), status, what=move(what)]{
			if( subscriber->Ended() )
				return;
			let requestId = subscriber->Request();
			RemoveOee( requestId, subscriber.get() );
			WriteException( Exception{SRCE_CUR, ExceptionArgs{status}, "{}", what}, requestId );
		} );
	}
	α GatewaySocketSession::RemoveOee( Jde::RequestId subscription, const OeeSubscriber* only )ι->sp<OeeSubscriber>{
		sp<OeeSubscriber> subscriber;
		{
			lg _{ _histMutex };
			if( auto p = _oeeSubscriptions.find(subscription); p!=_oeeSubscriptions.end() && (!only || p->second.get()==only) ){
				subscriber = move( p->second );
				_oeeSubscriptions.erase( p );
			}
		}
		if( subscriber )
			subscriber->End();
		return subscriber;
	}
	α GatewaySocketSession::OeeUnsubscribe( Jde::RequestId subscription, Jde::RequestId requestId )ι->void{
		LogRead( Ƒ("OeeUnsubscribe: {:x}", subscription), requestId );
		if( RemoveOee(subscription) )
			WriteComplete( requestId );
		else
			WriteException( Ƒ("No subscription {:x}.", subscription), requestId, SRCE_CUR );
	}
}