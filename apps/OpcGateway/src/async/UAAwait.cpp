#include "UAAwait.h"
#include "../UAClient.h"

#define let const auto

namespace Jde::Opc{
	α Gateway::SubmitUA( const sp<UAClient>& client, RequestId& requestId, sv what, bool keepAlive, absl::AnyInvocable<StatusCode()> submit, absl::AnyInvocable<void(UAClientException&&)> fail, SL sl )ι->void{
		client->PostUA( [&client, &requestId, what=string{what}, keepAlive, submit=std::move(submit), fail=std::move(fail), sl]()mutable{
			if( let sc = submit(); sc ){
				UAClient::RemoveIfDisconnected( sc, client );
				fail( UAClientException{sc, client->Handle(), requestId, sl, ELogLevel::Debug} );
			}
			else
				client->Process( requestId, what, keepAlive );
		});
	}
	α Gateway::ClearUA( const sp<UAClient>& client, RequestId requestId )ι->void{
		client->ClearRequest( requestId );
	}
	α Gateway::UAAsyncService( const sp<UAClient>& client, void* request, uint16 requestType, UA_ClientAsyncServiceCallback callback, uint16 responseType, void* userdata, RequestId& requestId )ι->StatusCode{
		return __UA_Client_AsyncService( *client, request, &UA_TYPES[requestType], callback, &UA_TYPES[responseType], userdata, &requestId );
	}
}