#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/db/Row.h>
#include <jde/db/Key.h>
#include <jde/db/awaits/SelectAwait.h>
#include <jde/opc/usings.h>

namespace Jde::Opc::Gateway{
	struct ServerCnnctn final{
		ServerCnnctn( str address )ι:Url{address}{}
		ServerCnnctn( DB::Row&& r )ε;
		ServerCnnctn( jobject&& o )ε;
		α ToJson()Ι->jobject;
		//reviews/install-issues.md #48:  the slug is the `slug\user` login's prefix, a certificate's file name (OpcHub.<slug>.pem) and a
		//url segment - the one rule the insert (ServerCnnctnMAwait) and testConnection apply;  the web form checks the same (ServerCnnctn.slugPattern).
		//Null for a valid slug.
		Ω SlugError( sv slug, SL sl )ι->up<Exception>;
		//docs/clients/spec.md *Connection settings*:  what the row says of the channel, checked at every save (ServerCnnctnMAwait, an
		//update's over the merged row) and by testConnection.  A mode or policy that is not one the gateway knows;  Sign or
		//Sign & Encrypt, pinned by the mode or by a secured policy, without the certificate URI that puts the gateway's certificate on
		//the channel;  a policy of None on a channel that is secured, or a secured one on a mode of None;  a session timeout, request
		//timeout or TTL of 0.  Null for a row that connects.  The cross-column half - a secured mode or policy without the URI, a mode and a
		//policy that contradict - is a before-update trigger per dialect as well (#520, config/sql/<dialect>), so two updates that each
		//passed against the saved row cannot together store a row it refuses.
		Ω SettingsError( const ServerCnnctn& row, SL sl )ι->up<Exception>;
		static constexpr uint32 MaxDurationMs{ 2147483647 };//SQL Server's int, the proc's parameter type:  about 24 days.  MergeSettings refuses more.
		static constexpr sv ModeNone{ "None" }, ModeSign{ "Sign" }, ModeSignAndEncrypt{ "SignAndEncrypt" };//UA_MessageSecurityMode's names (SecurityModeName), what security_mode holds.
		static constexpr sv PolicyNoneUri{ "http://opcfoundation.org/UA/SecurityPolicy#None" };
		//The channel's mode as this row has the client ask for it:  the pinned one, else the floor - Sign & Encrypt with a certificate
		//URI, None without (UAClient::Configuration).
		α EffectiveMode()Ι->sv{ return SecurityMode.size() ? sv{SecurityMode} : CertificateUri.empty() ? ModeNone : ModeSignAndEncrypt; }
		α Secured()Ι->bool{ return EffectiveMode()!=ModeNone; }//Sign or Sign & Encrypt:  the gateway's certificate goes on the channel.
		ServerCnnctnPK Id{};//0 for a connection not saved - createHandle keys the log handle on the url then.
		string Url;
		string CertificateUri;
		NsIndex DefaultBrowseNs{1};
		string Description;
		bool IsDefault{};
		string Name;
		optional<TimePoint> Deleted;
		ServerCnnctnNK Slug;
		//The spec's *Connection settings*, null meaning the gateway's own rule or setting:  empty strings and nullopts here.
		string SecurityMode;//ModeNone, ModeSign or ModeSignAndEncrypt;  empty keeps the floor.
		string SecurityPolicy;//a policy uri the gateway carries (ClientSecurity::SecuredPolicyUris) or PolicyNoneUri;  empty takes the strongest shared.
		optional<uint32> SessionTimeout;//milliseconds, requestedSessionTimeout.
		optional<uint32> RequestTimeout;//milliseconds, UA_ClientConfig::timeout.
		optional<uint32> PingInterval;//milliseconds, over /gateway/pingInterval;  0 turns the keep-alive off.
		optional<uint32> Ttl;//milliseconds, over /gateway/ttl.
		string Locale;//comma-separated locale ids, "en-US,de".
	};

	struct ServerCnnctnAwait final: TAwaitEx<vector<ServerCnnctn>,DB::SelectAwait::Task>{
		ServerCnnctnAwait( optional<DB::Key> key=nullopt, bool includeDeleted=false):_key{key}, _includeDeleted{includeDeleted}{};
	private:
		α Execute()ι->DB::SelectAwait::Task override;
		optional<DB::Key> _key;
		bool _includeDeleted;
	};
}