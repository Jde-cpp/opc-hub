#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/fwk/crypto/Certificate.h>
#include <jde/fwk/crypto/PublicKey.h>
#include <jde/db/awaits/ScalerAwait.h>

namespace Jde::Access::Server{
	struct LoginAwait final : TAwait<UserPK>{
		using base = TAwait<UserPK>;
		LoginAwait( Crypto::PublicKey publicKey, vector<byte> certificate, string&& description, SRCE )ι;
		//The host's own certificate, enrolled with no trust anchor:  its `ssl` block names it and the host holds its key.  The hub,
		//which never logs in to itself, so its collector's certificate token has a user (HistHost::Collector).
		Ω Own( vector<byte> certificate, string&& description, SRCE )ε->LoginAwait;
		α Suspend()ι->void override;
	private:
		LoginAwait( Crypto::PublicKey publicKey, vector<byte> certificate, string&& description, bool own, SL sl )ι;
		α LoginTask()ι->TAwait<optional<UserPK::Type>>::Task;
		α InsertUser( string&& modulusHex, uint32_t exponent, Crypto::Certificate&& info, string name )ι->DB::ScalerAwait<UserPK::Type>::Task;
		vector<byte> _certificate;
		string _description;
		bool _own;
		Crypto::PublicKey _publicKey;
	};
}