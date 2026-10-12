#include "HistSubscriber.h"
#include <jde/fwk/process/execution.h>
#include <jde/access/Authorize.h>
#include <jde/app/client/IAppClient.h> //!important
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/proto/opc.Common.h>
#include "../GatewayAppClient.h"
#include "../types/proto/opc.FromServer.h"
#include "HistHost.h"

#define let const auto

namespace Jde::Opc::Gateway{
	HistSubscriber::HistSubscriber( wp<GatewaySocketSession> session, Jde::RequestId requestId, wp<HistGroupHost> host, flat_map<Hist::NodeIndex,uint32> slots, vector<Hist::NodeIndex> members, UA_DateTime from )ι:
		_session{ move(session) }, _requestId{ requestId }, _host{ move(host) }, _slots{ move(slots) }, _members{ move(members) }, _from{ from }
	{}
	α HistSubscriber::Register( const sp<HistSubscriber>& self, SL sl )ε->void{
		auto subscription = Host( sl )->Group()->Subscribe( self, move(_members), _from, sl );
		lg _{ _pageMutex };
		_subscription = move( subscription );
	}
	α HistSubscriber::Next( uint limit, SL sl )ε->Hist::ReadResult{
		lg _{ _pageMutex };
		return _ended || !_subscription ? Hist::ReadResult{} : _subscription->Next( limit, sl );
	}
	α HistSubscriber::End()ι->void{
		_ended = true;
		lg _{ _pageMutex };
		if( _subscription )
			_subscription->End();
	}
	α HistSubscriber::Host( SL sl )Ε->sp<HistGroupHost>{
		auto host = _host.lock();
		THROW_IFSL( !host, "The group is no longer one this gateway historizes." );
		return host;
	}
	α HistSubscriber::Slot( Hist::NodeIndex index )Ι->optional<uint32>{
		auto p = _slots.find( index );
		return p==_slots.end() ? optional<uint32>{} : p->second;
	}
	α HistSubscriber::Page( Hist::ReadResult&& result, bool more )Ι->FromServer::HistSnapshot{
		FromServer::HistSnapshot y;
		for( let& v : result.Values ){
			if( let slot = Slot(v.Value.node_index()); slot )
				*y.add_values() = FromServer::ToProto( v.Value, *slot, v.Bound );
		}
		y.set_more( more );
		return y;
	}
	α HistSubscriber::Live()ι->void{
		lg _{ _queueMutex };
		_live = true;
		if( !_queued.empty() )
			Send( std::exchange(_queued, {}) );
	}
	α HistSubscriber::OnRecords( vector<Hist::Proto::HistoryRecord>&& records )ι->void{
		if( _ended )
			return;
		lg _{ _queueMutex };
		if( _live )
			Send( move(records) );
		else
			std::ranges::move( records, std::back_inserter(_queued) );
	}
	α HistSubscriber::Send( vector<Hist::Proto::HistoryRecord>&& records )ι->void{
		auto session = _session.lock();
		if( !session )
			return;
		FromServer::HistRecords y;
		for( let& r : records ){
			if( r.has_modification() ){
				if( let slot = Slot(r.modification().node_index()); slot )
					*y.add_modifications() = FromServer::ToProto( r.modification(), *slot );
			}
			else if( let slot = Slot(r.value().node_index()); slot )
				*y.add_values() = FromServer::ToProto( r.value(), *slot, false );
		}
		session->Write( FromServer::HistRecordsTrans(move(y), _requestId) );
	}

	//The registration and its snapshot, taken on the executor:  the group's locks, and the files the snapshot opens under
	//them, aren't the socket strand's to wait on.
	struct HistSubscribeAwait final : AnyVoidAwait{
		HistSubscribeAwait( sp<HistSubscriber> subscriber, SRCE )ι:AnyVoidAwait{ sl }, _subscriber{ move(subscriber) }{}
	protected:
		α Suspend()ι->void override{
			Post( [this]{
				try{
					_subscriber->Register( _subscriber, _sl );
					Resume();
				}
				catch( runtime_error& e ){
					ResumeExp( move(e) );
				}
			} );
		}
	private:
		sp<HistSubscriber> _subscriber;
	};
	//A page of the snapshot, read on the executor as a group's QL reads are:  the request's strand isn't held for the scan.
	struct HistPageAwait final : AnyAwait<Hist::ReadResult>{
		HistPageAwait( sp<HistSubscriber> subscriber, uint limit, SRCE )ι:AnyAwait<Hist::ReadResult>{ sl }, _subscriber{ move(subscriber) }, _limit{ limit }{}
	protected:
		α Suspend()ι->void override{
			Post( [this]{
				try{
					Resume( _subscriber->Next(_limit, _sl) );
				}
				catch( runtime_error& e ){
					ResumeExp( move(e) );
				}
			} );
		}
	private:
		sp<HistSubscriber> _subscriber;
		uint _limit;
	};

	α GatewaySocketSession::HistSubscribe( FromClient::HistSubscribe&& request, Jde::RequestId requestId )ι->void{
		try{
			LogRead( Ƒ("HistSubscribe: group: {}, nodeCount: {}", request.group(), request.nodes_size()), requestId );
			if( !Session() ){
				WriteException( Exception{"Send sessionId before subscribing."}, requestId );
				return;
			}
			AppClient()->Acl()->Test( "gateway", "histGroups", std::to_string(request.group()), Access::ERights::Read, UserPK(), SRCE_CUR );
			auto& hist = HistHost::Instance();
			hist.TestEnabled( SRCE_CUR );
			auto host = hist.Find( request.group() );
			if( !host )
				throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::NotFound}, "Group {} isn't one this gateway historizes.", request.group() };
			vector<ExNodeId> nodes; nodes.reserve( request.nodes_size() );
			for( let& node : request.nodes() )
				nodes.push_back( ProtoUtils::ToExNodeId(node) );
			if( nodes.empty() )
				throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::BadRequest}, "HistSubscribe names no nodes." };
			let indexes = host->Indexes( nodes, SRCE_CUR );
			FromServer::HistSubscribeAck ack;
			vector<Hist::NodeIndex> members;
			flat_map<Hist::NodeIndex,uint32> slots;
			for( uint32 i=0; i<indexes.size(); ++i ){
				ack.add_statuses( indexes[i] ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADNODEIDUNKNOWN );
				if( indexes[i] ){
					slots.emplace( *indexes[i], i );
					members.push_back( *indexes[i] );
				}
			}
			let from = request.has_from_time() ? UADateTime{ request.from_time() }.UA() : UADateTime{ hist.Historian()->Time().Now() }.UA();
			{
				lg _{ _histMutex };
				if( _histSubscriptions.contains(requestId) )
					throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::BadRequest}, "Subscription {:x} is live:  a HistSubscribe's request id names it.", requestId };
			}
			HistSubscribing( ms<HistSubscriber>(SharedFromThis(), requestId, move(host), move(slots), move(members), from), move(ack) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}
	α GatewaySocketSession::HistSubscribing( sp<HistSubscriber> subscriber, FromServer::HistSubscribeAck ack )ι->VoidTask{
		auto self = SharedFromThis();//keep alive
		let requestId = subscriber->Request();
		try{
			if( subscriber->HasMembers() )
				co_await HistSubscribeAwait{ subscriber, SRCE_CUR };
			{
				lg _{ _histMutex };
				if( _closed ){
					subscriber->End();
					co_return;
				}
				if( !_histSubscriptions.try_emplace(requestId, subscriber).second ){
					subscriber->End();
					throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::BadRequest}, "Subscription {:x} is live:  a HistSubscribe's request id names it.", requestId };
				}
			}
			Write( FromServer::HistSubscribeAckTrans(move(ack), requestId) );
			let limit = HistHost::Instance().Historian()->Config().ReadLimit;
			for( bool more{ true }; more && !subscriber->Ended(); ){
				auto page = co_await HistPageAwait{ subscriber, limit, SRCE_CUR };
				more = !page.Continuation.empty();
				Write( FromServer::HistSnapshotTrans(subscriber->Page(move(page), more), requestId) );
			}
			subscriber->Live();
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
			RemoveHist( requestId );
		}
	}
	α GatewaySocketSession::RemoveHist( Jde::RequestId subscription )ι->sp<HistSubscriber>{
		sp<HistSubscriber> subscriber;
		{
			lg _{ _histMutex };
			if( auto p = _histSubscriptions.find(subscription); p!=_histSubscriptions.end() ){
				subscriber = move( p->second );
				_histSubscriptions.erase( p );
			}
		}
		if( subscriber )
			subscriber->End();
		return subscriber;
	}
	α GatewaySocketSession::HistUnsubscribe( Jde::RequestId subscription, Jde::RequestId requestId )ι->void{
		LogRead( Ƒ("HistUnsubscribe: {:x}", subscription), requestId );
		if( RemoveHist(subscription) )
			WriteComplete( requestId );
		else
			WriteException( Ƒ("No subscription {:x}.", subscription), requestId, SRCE_CUR );
	}
}