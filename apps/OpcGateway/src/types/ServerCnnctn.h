#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/db/Row.h>
#include <jde/db/Key.h>
#include <jde/db/awaits/SelectAwait.h>
#include <jde/opc/usings.h>

namespace Jde::Opc::Gateway{
	//A slug that names a file or a directory - a connection's certificate, a flow's trace - so never a path:  letters, digits,
	//'.', '_' and '-' only, starting with a letter or digit.  Null for a valid one.
	α FileSlugError( sv slug, SRCE )ι->up<Exception>;
	struct ServerCnnctn final{
		ServerCnnctn( str address )ι:Url{address}{}
		ServerCnnctn( DB::Row&& r )ε;
		ServerCnnctn( jobject&& o )ε;
		α ToJson()Ι->jobject;
		//reviews/install-issues.md #48:  the slug is the `slug\user` login's prefix, a certificate's file name (OpcHub.<slug>.pem) and a
		//url segment - the one rule the insert (ServerCnnctnMAwait) and testConnection apply;  the web form checks the same (ServerCnnctn.slugPattern).
		//Not `watch`, the watch list's segment beside the connections'.
		//Null for a valid slug.
		Ω SlugError( sv slug, SL sl )ι->up<Exception>;
		//docs/clients/spec.md *Connection settings*:  what the row says of the channel, checked at every save (ServerCnnctnMAwait, an
		//update's over the merged row) and by testConnection.  A mode or policy that is not one the gateway knows;  Sign or
		//Sign & Encrypt, pinned by the mode or by a secured policy, without the certificate URI that puts the gateway's certificate on
		//the channel;  a policy of None on a channel that is secured, or a secured one on a mode of None;  a session timeout, request
		//timeout or TTL of 0.  Null for a row that connects.  The cross-column half - a secured mode or policy without the URI, a mode and a
		//policy that contradict - is a trigger per dialect as well (#520, config/sql/<dialect>).
		Ω SettingsError( const ServerCnnctn& row, SL sl )ι->up<Exception>;
		//docs/open-protocol/spec.md *The row*:  what the row's kind asks of it, checked where SettingsError is.  A kind the gateway
		//does not know;  `driver` or an Open Protocol url on a UA row;  on an Open Protocol row, a url or a `driver` OpenProtocolRow refuses.
		//A flow row (docs/flows/spec.md *Derived variables*) is the flow server's alone:  the kind on another slug than FlowSlug, or
		//that slug under another kind, `driver`, and a url that is not the flow server's (FlowUrl).  Null for a row its kind takes.
		Ω KindError( const ServerCnnctn& row, SL sl )ι->up<Exception>;
		static constexpr sv KindUa{ "ua" }, KindOpenProtocol{ "open-protocol" }, KindFlow{ "flow" };//what `kind` holds.
		static constexpr NsIndex DriverBrowseNs{ 2 };//the one namespace of a server the gateway runs for itself, after UA's and the server's own.
		//A kind the gateway serves through a driver's own UA server (drivers/DriverHost.h), one that registers an IDriver, which the
		//row's clients dial in place of the url (DriverUrl).
		α HasDriver()Ι->bool;
		//A kind whose server is the gateway's own on loopback:  a driver's, or the flow server's (flows/FlowServer.h), which
		//registers none and is dialed at the row's url.  The row stores no certificate URI or default browse namespace for it:
		//the constructors supply the server's, the gateway's own applicationUri - the uri in the SAN of the process's pair,
		///http/ssl, which that server presents - and DriverBrowseNs.
		α OwnServer()Ι->bool{ return Kind==KindFlow || HasDriver(); }
		static constexpr sv FlowSlug{ "flows" };//the flow server's row, the one of KindFlow.
		Ω FlowPort()ι->PortType;///gateway/flows/server/port, 4860 where the block names none.
		Ω FlowUrl()ι->string{ return Ƒ( "opc.tcp://127.0.0.1:{}", FlowPort() ); }//what the flow row's url holds, and its clients dial.
		//Where a driver row's server listens:  loopback, at /gateway/drivers/portBase plus the row's id.  Throws past port 65535.
		α DriverPort( SRCE )Ε->PortType;
		α DriverUrl( SRCE )Ε->string{ return Ƒ( "opc.tcp://127.0.0.1:{}", DriverPort(sl) ); }
		Ω DriverPortBase()ι->uint;///gateway/drivers/portBase, 4870 where the block names none.
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
		string Kind;//KindUa, KindOpenProtocol or KindFlow;  empty, the column's null, is KindUa.
		string Driver;//the kind's own settings, a json object's text (OpenProtocolRow);  empty for none.
		//What an OwnServer row is supplied, the certificate URI and DriverBrowseNs, as the constructors supply a saved row's;
		//testConnection's too, whose row is the url's alone until its kind is merged.  Nothing for another kind.
		α SupplyDriver()ι->void;
	};

	struct ServerCnnctnAwait final: TAwaitEx<vector<ServerCnnctn>,DB::SelectAwait::Task>{
		ServerCnnctnAwait( optional<DB::Key> key=nullopt, bool includeDeleted=false):_key{key}, _includeDeleted{includeDeleted}{};
	private:
		α Execute()ι->DB::SelectAwait::Task override;
		optional<DB::Key> _key;
		bool _includeDeleted;
	};
}