#pragma once
#include "helpers.h"
#include "../../src/UAClient.h"

namespace Jde::Opc::Gateway::Tests{
	//A connect a test looks at either way:  the client, or what it failed with.
	α TryConnect( const ServerCnnctnNK& slug, Credential cred )ι->std::expected<sp<UAClient>,up<Exception>>;
	//The detached form, for a test that launches several connects at once:  `client` or `error` set, then `done` raised.  Pointers,
	//not a capturing lambda:  its captures would die with the temporary at the first suspension.
	α Connect( ServerCnnctnNK slug, Credential cred, sp<UAClient>* client, up<Exception>* error, atomic_flag* done )ι->ConnectAwait::Task;

	//A fixture whose tests connect:  the client one made or what it failed with, and the client removed after each test.
	struct ClientFixture : ::testing::Test{
	protected:
		α TearDown()ι->void override;
		//Connects into _client, else _exception:  "" when it connected, else what it failed with.  A `cell` logs the outcome - the
		//negotiated policy and mode, or the failure - as a security-matrix row, `MATRIX|<cell>|<outcome>`.
		α Attempt( const ServerCnnctnNK& slug, Credential cred, sv cell={} )ι->string;
		up<Exception> _exception;
		sp<UAClient> _client;
	};
}