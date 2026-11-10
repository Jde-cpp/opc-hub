#include "OeeSubscriber.h"
#include <jde/access/Authorize.h>
#include <jde/app/client/IAppClient.h>
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"
#include "../types/proto/opc.FromServer.h"
#include "OeeHost.h"

#define let const auto

namespace Jde::Opc::Gateway{
	OeeSubscriber::OeeSubscriber( wp<GatewaySocketSession> session, Jde::RequestId requestId, sp<OeeFeed> feed )ι:
		_session{ move(session) }, _requestId{ requestId }, _feed{ move(feed) }
	{}
	α OeeSubscriber::End()ι->void{
		if( !_ended.exchange(true) )
			_feed->Remove( this );
	}
	α OeeSubscriber::Live()ι->void{
		lg _{ _mutex };
		_live = true;
		if( auto held = std::exchange(_held, nullopt); held && !_ended )
			Send( *held );
	}
	α OeeSubscriber::OnStatus( const FromServer::OeeStatus& status )ι->void{
		if( _ended )
			return;
		lg _{ _mutex };
		if( _live )
			Send( status );
		else
			_held = status;
	}
	α OeeSubscriber::OnEnded( EHttpStatus status, string what )ι->void{
		if( auto session = _session.lock(); session )
			session->OeeEnded( shared_from_this(), status, move(what) );
	}
	α OeeSubscriber::Send( const FromServer::OeeStatus& status )ι->void{
		if( auto session = _session.lock(); session )
			session->WriteOee( shared_from_this(), FromServer::OeeStatusTrans(status, _requestId) );
	}

	α GatewaySocketSession::OeeSubscribe( FromClient::OeeSubscribe&& request, Jde::RequestId requestId )ι->void{
		try{
			LogRead( Ƒ("OeeSubscribe: machine: {}", request.machine()), requestId );
			if( !Session() ){
				WriteException( Exception{"Send sessionId before subscribing."}, requestId );
				return;
			}
			if( !request.machine() )
				throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::BadRequest}, "OeeSubscribe needs machine, an oee_machines id." };
			auto& oee = OeeHost::Instance();
			let machine = oee.Find( request.machine() );
			if( !machine ){
				AppClient()->Acl()->Test( "gateway", "histGroups", Access::ERights::Read, UserPK(), SRCE_CUR );//the table's, before a caller learns of an id.
				throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::NotFound}, "Machine {} not found.", request.machine() };
			}
			HistHost::Instance().Gate( machine->Row.Group, Access::ERights::Read, false, UserPK(), SRCE_CUR );//as the reads (spec *Authorization*).
			oee.TestEnabled( SRCE_CUR );
			auto feed = oee.Feed( request.machine() );
			if( !feed )
				throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::ServiceUnavailable}, "The gateway's OEE has stopped." };
			auto subscriber = ms<OeeSubscriber>( SharedFromThis(), requestId, move(feed) );
			{//claimed here, on the strand, so a second subscribe with the id is refused before either joins.
				lg _{ _histMutex };
				if( _closed )
					return;
				if( _histSubscriptions.contains(requestId) || !_oeeSubscriptions.try_emplace(requestId, subscriber).second )
					throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::BadRequest}, "Subscription {} is live:  an OeeSubscribe's request id names it.", hex(requestId) };
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
	α GatewaySocketSession::WriteOee( sp<OeeSubscriber> subscriber, FromServer::Transmission&& message )ι->void{
		Dispatch( [this, self=SharedFromThis(), subscriber=move(subscriber), message=move(message)]()mutable{
			if( !subscriber->Ended() )
				Write( move(message) );
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