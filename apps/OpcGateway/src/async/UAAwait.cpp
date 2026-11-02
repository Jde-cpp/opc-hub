#include "UAAwait.h"
#include "../UAClient.h"

#define let const auto

namespace Jde::Opc::Gateway{
	namespace{
		//SubmitUA's strand half, which parks itself while the session re-activates (UAClient::Reactivating).
		struct Submission final{
			α operator()()ι->void{
				if( Client->Reactivating() )
					return Client->Park( std::move(*this) );
				if( let sc = Submit(); sc ){
					UAClient::RemoveIfDisconnected( sc, Client );
					Fail( UAClientException{sc, Client->Handle(), Id, Sl, ELogLevel::Debug} );
				}
				else
					Client->Process( Id, What, KeepAlive );
			}
			const sp<UAClient>& Client;
			RequestId& Id;
			string What;
			bool KeepAlive;
			absl::AnyInvocable<StatusCode()> Submit;
			absl::AnyInvocable<void(UAClientException&&)> Fail;
			SL Sl;
		};
	}
}
namespace Jde::Opc{
	α Gateway::SubmitUA( const sp<UAClient>& client, RequestId& requestId, sv what, bool keepAlive, absl::AnyInvocable<StatusCode()> submit, absl::AnyInvocable<void(UAClientException&&)> fail, SL sl )ι->void{
		client->PostUA( Submission{client, requestId, string{what}, keepAlive, std::move(submit), std::move(fail), sl} );
	}
	α Gateway::ClearUA( const sp<UAClient>& client, RequestId requestId )ι->void{
		client->ClearRequest( requestId );
	}
	α Gateway::UAAsyncService( const sp<UAClient>& client, void* request, uint16 requestType, UA_ClientAsyncServiceCallback callback, uint16 responseType, void* userdata, RequestId& requestId )ι->StatusCode{
		return __UA_Client_AsyncService( *client, request, &UA_TYPES[requestType], callback, &UA_TYPES[responseType], userdata, &requestId );
	}
}