#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/db/awaits/ScalerAwait.h>

namespace Jde::Access::Server{
	struct LoginAwait final : TAwait<UserPK>{
		using base = TAwait<UserPK>;
		LoginAwait( Crypto::PublicKey publicKey, vector<byte> certificate, string&& description, SRCE )ι;
		α Suspend()ι->void override;
	private:
		α LoginTask()ι->TAwait<optional<UserPK::Type>>::Task;
		α InsertUser( string&& modulusHex, uint32_t exponent, Crypto::Certificate&& info, string name )ι->DB::ScalerAwait<UserPK::Type>::Task;
		vector<byte> _certificate;
		string _description;
		Crypto::PublicKey _publicKey;
	};
}