#include "TestConnectAwait.h"
#include "../UAClient.h"
#include "../types/UAClientException.h"
#include "BlockingPool.h"

namespace Jde::Opc::Gateway{
	α TestConnectAwait::Suspend()ι->void{
		BlockingPool::Run( [this]{//the build and the connect - the certificate issue, the trust scan, the host probe - off the executor (#353).
			auto client = ms<UAClient>( move(_cnnctn), _cred );
			client->ConnectForTest( [this]( sp<UAClient> client, up<UAClientException> error ){//on the executor;  the last use of `this`:  Resume may destroy it.
				if( error )
					ResumeExp( move(error) );
				else
					Resume( move(client) );
			});
		}, [this]( up<Exception> e ){ ResumeExp( move(e) ); }, _sl );//the certificate could not be issued or read, or open62541 refused the url before connecting.
	}
}