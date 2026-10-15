#include "TestConnectAwait.h"
#include "../UAClient.h"
#include "../types/UAClientException.h"

namespace Jde::Opc::Gateway{
	α TestConnectAwait::Suspend()ι->void{
		try{
			auto client = ms<UAClient>( move(_cnnctn), _cred );
			client->ConnectForTest( [this]( sp<UAClient> client, up<UAClientException> error ){//the last use of `this`:  Resume may destroy it.
				if( error )
					ResumeExp( move(*error) );
				else
					Resume( move(client) );
			});
		}
		catch( runtime_error& e ){//the certificate could not be issued or read, or open62541 refused the url before connecting.
			ResumeExp( move(e) );
		}
	}
}