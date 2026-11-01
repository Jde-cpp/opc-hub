#pragma once
#include "../UAClient.h"

namespace Jde::Opc::Gateway{
	//Runs a synchronous UA_Client call on the client's strand and resumes the awaiter with the result:  the connection
	//attributes, the config's own fields - local reads that only need serializing with run_iterate.  A synchronous
	//service (the tests' activateCurrentSession and deleteSingle) drives the client's event loop until the answer and
	//holds the strand for the round trip, which the connection manager's net thread serves (#526);  the gateway's own
	//went asynchronous - TranslateBrowsePathsAwait, DataTypesAwait - so nothing of its stalls a client's data changes.
	Τ struct UAStrandAwait final : TAwait<T>{
		using base = TAwait<T>;
		UAStrandAwait( sp<UAClient> client, function<T()> f, SRCE )ι: base{sl}, _client{move(client)}, _f{move(f)}{}
		α Suspend()ι->void override{
			_client->PostUA( [this]{
				try{
					this->Resume( _f() );
				}
				catch( runtime_error& e ){
					this->ResumeExp( move(e) );
				}
			});
		}
	private:
		sp<UAClient> _client;
		function<T()> _f;
	};
}
