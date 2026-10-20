#pragma once
#include <absl/functional/any_invocable.h>
#include <open62541/client_highlevel_async.h>
#include <jde/fwk/co/Await.h>
#include "../types/UAClientException.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//The bodies UAAwait and UAServiceAwait share, out of line so this header needs no UAClient - Browse.h, which UAClient.h
	//includes, uses it.  On the client's strand `submit` sends the request and fills requestId:  a refusal goes through
	//RemoveIfDisconnected, as UACε's does, so a channel that went is deregistered, and reaches `fail`;  an accepted request is
	//pumped as `what`.
	α SubmitUA( const sp<UAClient>& client, RequestId& requestId, sv what, bool keepAlive, absl::AnyInvocable<StatusCode()> submit, absl::AnyInvocable<void(UAClientException&&)> fail, SL sl )ι->void;
	α ClearUA( const sp<UAClient>& client, RequestId requestId )ι->void;
	α UAAsyncService( const sp<UAClient>& client, void* request, uint16 requestType, UA_ClientAsyncServiceCallback callback, uint16 responseType, void* userdata, RequestId& requestId )ι->StatusCode;

	//A UA service call:  the client, the request id, the strand submit.  `this` outlives the suspension - only the completion,
	//or a refused submit, resumes it.
	template<class T>
	struct UAAwait : TAwait<T>{
		using base = TAwait<T>;
		UAAwait( sp<UAClient> client, SL sl )ι:base{ sl }, _client{ move(client) }{}
	protected:
		α Submit( sv what, absl::AnyInvocable<StatusCode()> submit, bool keepAlive=false )ι->void{
			SubmitUA( _client, _requestId, what, keepAlive, std::move(submit), [this]( UAClientException&& e ){ this->ResumeExp( std::move(e) ); }, this->_sl );
		}
		α ClearRequest()ι->void{ ClearUA( _client, _requestId ); }
		sp<UAClient> _client;
		RequestId _requestId{};
	};

	//One service through open62541's generic asynchronous call:  its typed helpers are synchronous, or take one node, and would
	//hold the strand.  The awaitable names what to encode (Request) and turns the decoded answer into its result (Convert),
	//throwing for a refusal;  open62541 clears the answer once the completion returns.
	template<class T, class TResponse, uint16 RequestType, uint16 ResponseType>
	struct UAServiceAwait : UAAwait<T>{
		using base = UAAwait<T>;
		UAServiceAwait( sp<UAClient> client, sv what, SL sl )ι:base{ move(client), sl }, _what{ what }{}
		α Suspend()ι->void override{
			this->Submit( _what, [this]{ return UAAsyncService( this->_client, Request(), RequestType, OnResponse, ResponseType, this, this->_requestId ); } );
		}
	protected:
		β Request()ι->void* = 0;//borrowed until the submit returns:  the service encodes it then.
		β Convert( TResponse&& response )ε->T = 0;
	private:
		Ω OnResponse( UA_Client* /*ua*/, void* await, UA_UInt32 /*requestId*/, void* response )ι->void{
			ASSERT( await );
			static_cast<UAServiceAwait*>( await )->OnComplete( *static_cast<TResponse*>(response) );
		}
		α OnComplete( TResponse& response )ι->void{
			this->ClearRequest();
			try{
				this->Resume( Convert(move(response)) );
			}
			catch( runtime_error& e ){
				this->ResumeExp( move(e) );
			}
		}
		sv _what;
	};
}