#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include "../types/ServerCnnctn.h"
#include "../auth/OpcServerSession.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//Connects to `cnnctn` as ConnectAwait would to the saved row of that slug - the same issued certificate and the same credential -
	//and resumes with the client, or with why the connect failed (UAClient::ConnectForTest).  Nothing is registered:  the client is
	//the caller's alone, to read from and let go.  An AnyAwait, so any coroutine can co_await it.  testConnection's.
	struct TestConnectAwait final : AnyAwait<sp<UAClient>>{
		using base = AnyAwait<sp<UAClient>>;
		TestConnectAwait( ServerCnnctn&& cnnctn, Credential cred, SRCE )ι:base{sl}, _cnnctn{move(cnnctn)}, _cred{move(cred)}{}
	protected:
		α Suspend()ι->void override;
	private:
		ServerCnnctn _cnnctn;
		Credential _cred;
	};
}