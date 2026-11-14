#include "ServerCnnctn.h"
#include <jde/fwk/settings.h>
#include <jde/db/IDataSource.h>
#include <jde/db/Row.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/generators/Statement.h>
#include <jde/db/generators/WhereClause.h>
#include <jde/opc/ClientSecurity.h>
#include "../GatewayAppClient.h"
#include "../opcInternal.h"
#include "../drivers/IDriver.h"
#include "OpenProtocolRow.h"

#define let const auto

namespace Jde::Opc::Gateway{
	ServerCnnctn::ServerCnnctn( DB::Row&& r )ε:
		Id{ r.Get<uint32>(0) },
		Url{ r.TakeString(1) },
		CertificateUri{ r.TakeString(2) },
		DefaultBrowseNs{ r.GetOpt<uint16_t>(4).value_or(1) },//unset means 1 - what the SPA reads a NULL as, and so spells a namespace-1 segment bare (reviews/m3-closing.md #3, ruled 09-21; gateway-review #26 had unified both ctors on 0).
		IsDefault{ r.GetBit(3) },
		Name{ r.TakeString(5) },
		Deleted{ r.GetOpt<TimePoint>(7) },
		Slug{ r.TakeString(6) },
		SecurityMode{ r.TakeString(8) },
		SecurityPolicy{ r.TakeString(9) },
		SessionTimeout{ r.GetOpt<uint32>(10) },
		RequestTimeout{ r.GetOpt<uint32>(11) },
		PingInterval{ r.GetOpt<uint32>(12) },
		Ttl{ r.GetOpt<uint32>(13) },
		Locale{ r.TakeString(14) },
		Kind{ r.TakeString(15) },
		Driver{ r.TakeString(16) }
	{
		SupplyDriver();
	}
	ServerCnnctn::ServerCnnctn( jobject&& o )ε:
		Id{ Json::FindNumber<uint32>(o, "id").value_or(0) },
		Url{ Json::FindDefaultSV(o, "url") },
		CertificateUri{ Json::FindDefaultSV(o, "certificateUri") },//the QL's names, not the columns':  a serverConnection{…} result is what arrives here (security-matrix #7).
		DefaultBrowseNs{ Json::FindNumber<NsIndex>(o, "defaultBrowseNs").value_or(1) },//as the row ctor.
		Description{ Json::FindDefaultSV(o, "description") },
		IsDefault{ Json::FindBool(o, "isDefault").value_or(false) },//the value, not the optional:  braces take an optional<bool> for "has one", which the old, never-matching key hid.
		Name{ Json::FindDefaultSV(o, "name") },
		Deleted{ Json::FindTimePoint(o, "deleted") },
		Slug{ Json::FindDefaultSV(o, "slug") },
		SecurityMode{ Json::FindDefaultSV(o, "securityMode") },
		SecurityPolicy{ Json::FindDefaultSV(o, "securityPolicy") },
		SessionTimeout{ Json::FindNumber<uint32>(o, "sessionTimeout") },
		RequestTimeout{ Json::FindNumber<uint32>(o, "requestTimeout") },
		PingInterval{ Json::FindNumber<uint32>(o, "pingInterval") },
		Ttl{ Json::FindNumber<uint32>(o, "ttl") },
		Locale{ Json::FindDefaultSV(o, "locale") },
		Kind{ Json::FindDefaultSV(o, "kind") },
		Driver{ Json::FindDefaultSV(o, "driver") }
	{
		SupplyDriver();
	}
	α ServerCnnctn::SupplyDriver()ι->void{
		if( !OwnServer() )
			return;
		let& ssl = AppClient()->SslSettings;
		CertificateUri = ssl ? ssl->Certificate.SanUri() : string{};
		DefaultBrowseNs = DriverBrowseNs;
	}
	α ServerCnnctn::HasDriver()Ι->bool{ return FindDriver( Kind )!=nullptr; }
	α ServerCnnctn::FlowPort()ι->PortType{ return Settings::FindNumber<PortType>( "/gateway/flows/server/port" ).value_or( 4860 ); }
	α ServerCnnctn::DriverPortBase()ι->uint{ return Settings::FindNumber<uint>( "/gateway/drivers/portBase" ).value_or( 4870 ); }
	α ServerCnnctn::DriverPort( SL sl )Ε->PortType{
		let portBase = DriverPortBase();
		let port = portBase+Id;
		THROW_IFSL( port>65535, "Connection '{}' has no driver port:  /gateway/drivers/portBase, {}, plus its id, {}, is past 65535.", Slug, portBase, Id );
		return (PortType)port;
	}
	α ServerCnnctn::SlugError( sv slug, SL sl )ι->up<Exception>{
		if( auto e = FileSlugError(slug, sl); e )
			return e;
		//the web's routes under a gateway, apps/gateways/<instance>/<segment>, would shadow the connection's page:  the watch list
		//(419-watch #12), the historian's lists (#337), the calendars (#391), OEE's lists (#404, #409), its dashboard (#406) and the
		//flows (#455).  As ServerCnnctn.fieldError.
		constexpr std::array reserved{ "watch"sv, "templates"sv, "groups"sv, "calendars"sv, "machines"sv, "lines"sv, "reasons"sv, "oee"sv, "flows"sv };
		return std::ranges::contains( reserved, slug ) ? mu<Exception>( Ƒ("Slug '{}' is not valid:  it is the url segment of a list on the gateway's page.", slug), ExceptionArgs{EHttpStatus::BadRequest}, sl ) : nullptr;
	}
	α ServerCnnctn::SettingsError( const ServerCnnctn& row, SL sl )ι->up<Exception>{
		using ClientSecurity::PolicyName;
		let refuse = [sl]( string what ){ return mu<Exception>( move(what), ExceptionArgs{EHttpStatus::BadRequest}, sl ); };
		//0 is the default only to open62541, and not the one the help gives:  5 s for the request timeout, not /gateway/requestTimeout;
		//and a TTL of 0 closes the client after every request.  The ping interval's 0 turns the keep-alive off, which is the operator's to ask.
		for( let& [name, value] : { std::pair{"sessionTimeout"sv, row.SessionTimeout}, std::pair{"requestTimeout"sv, row.RequestTimeout}, std::pair{"ttl"sv, row.Ttl} } ){
			if( value==0u )
				return refuse( Ƒ("'{}' of 0 is not allowed:  leave it empty for the default.", name) );
		}
		let& mode = row.SecurityMode;
		if( mode.size() && mode!=ModeNone && mode!=ModeSign && mode!=ModeSignAndEncrypt )
			return refuse( Ƒ("Security mode '{}' is not one of None, Sign or SignAndEncrypt.", mode) );
		let& policy = row.SecurityPolicy;
		let securedPolicy = ClientSecurity::Carries( policy );
		if( policy.size() && !securedPolicy && policy!=PolicyNoneUri ){
			string names{ PolicyName(PolicyNoneUri) };
			for( let uri : ClientSecurity::SecuredPolicyUris() )
				names += Ƒ( ", {}", PolicyName(uri) );
			return refuse( Ƒ("Security policy '{}' is not one the gateway carries:  {} - as http://opcfoundation.org/UA/SecurityPolicy#<name>.", policy, names) );
		}
		let securedMode = mode==ModeSign || mode==ModeSignAndEncrypt;
		if( (securedMode || securedPolicy) && row.CertificateUri.empty() )
			return refuse( Ƒ("Security {} '{}' needs a certificate URI:  the server's applicationUri, which selects its secured endpoints and puts the gateway's certificate on the channel.", securedMode ? "mode" : "policy", securedMode ? mode : PolicyName(policy)) );
		if( securedPolicy && mode==ModeNone )
			return refuse( Ƒ("Security policy '{}' secures the channel, which security mode None leaves open:  pick Sign or SignAndEncrypt, or a policy of None.", PolicyName(policy)) );
		if( policy==PolicyNoneUri && row.Secured() )
			return refuse( Ƒ("Security policy None leaves the channel open, which security mode {} secures:  {}.", row.EffectiveMode(), mode.size() ? "pick a secured policy, or security mode None" : "a certificate URI means Sign & Encrypt unless the mode is set to None") );
		return nullptr;
	}
	α ServerCnnctn::KindError( const ServerCnnctn& row, SL sl )ι->up<Exception>{
		let refuse = [sl]( string what ){ return mu<Exception>( move(what), ExceptionArgs{EHttpStatus::BadRequest}, sl ); };
		let& kind = row.Kind;
		if( kind==KindFlow ){
			if( row.Slug!=FlowSlug )
				return refuse( Ƒ("Kind '{}' is the flow server's connection alone, '{}', which the gateway makes and keeps.", KindFlow, FlowSlug) );
			if( row.Driver.size() )
				return refuse( Ƒ("A {} connection takes no driver settings:  they are a driver kind's, {}'s.", KindFlow, KindOpenProtocol) );
			return row.Url==FlowUrl() ? nullptr : refuse( Ƒ("Url '{}' is not the flow server's, {}:  /gateway/flows/server/port.", row.Url, FlowUrl()) );
		}
		if( row.Slug==FlowSlug )
			return refuse( Ƒ("Slug '{}' is the flow server's connection, of kind '{}', which the gateway makes and keeps.", FlowSlug, KindFlow) );
		if( kind.empty() || kind==KindUa ){
			if( row.Driver.size() )
				return refuse( Ƒ("A {} connection takes no driver settings:  they are a driver kind's, {}'s.", KindUa, KindOpenProtocol) );
			return row.Url.starts_with( OpenProtocolRow::Scheme ) ? refuse( Ƒ("Url '{}' is an Open Protocol controller's, which a {} connection does not dial:  set the kind to {}.", row.Url, KindUa, KindOpenProtocol) ) : nullptr;
		}
		if( kind!=KindOpenProtocol )
			return refuse( Ƒ("Kind '{}' is not one of {}, {} or {}.", kind, KindUa, KindOpenProtocol, KindFlow) );
		try{
			(void)OpenProtocolRow{ row, sl };
		}
		catch( Exception& e ){
			return e.Move();
		}
		return nullptr;
	}
	α ServerCnnctn::ToJson()Ι->jobject{
		jobject o;
		o.emplace( "id", Id );
		o.emplace("url", Url);
		o.emplace("certificateUri", CertificateUri);
		o.emplace("isDefault", IsDefault);
		o.emplace("defaultBrowseNs", DefaultBrowseNs);
		o.emplace("name", Name);
		o.emplace("slug", Slug);
		o.emplace( "description", Description );
		o.emplace( "deleted", Deleted ? jvalue{ToIsoString(*Deleted)} : jvalue{} );
		let text = []( str s ){ return s.empty() ? jvalue{} : jvalue{s}; };
		let number = []( optional<uint32> n ){ return n ? jvalue{*n} : jvalue{}; };
		o.emplace( "securityMode", text(SecurityMode) );
		o.emplace( "securityPolicy", text(SecurityPolicy) );
		o.emplace( "sessionTimeout", number(SessionTimeout) );
		o.emplace( "requestTimeout", number(RequestTimeout) );
		o.emplace( "pingInterval", number(PingInterval) );
		o.emplace( "ttl", number(Ttl) );
		o.emplace( "locale", text(Locale) );
		o.emplace( "kind", text(Kind) );
		o.emplace( "driver", text(Driver) );
		return o;
	}

	α ServerCnnctnAwait::Execute()ι->DB::SelectAwait::Task{
		let view = GetViewPtr( "server_connections" );
		DB::WhereClause where;
		if( !_includeDeleted )
			where.Add( view->GetColumnPtr("deleted"), nullptr );
		if( _key ){
			if( _key->IsPK() )
				where.Add( view->GetColumnPtr("server_connection_id"), _key->PK() );
			else{
				if( _key->NK().size() )
					where.Add( view->GetColumnPtr("slug"), _key->NK() );
				else
					where.Add( view->GetColumnPtr("is_default"), true );
			}
		}
		auto statement = DB::Statement{ {view->GetColumns({"server_connection_id", "url", "certificate_uri", "is_default", "default_browse_ns", "name", "slug", "deleted", "security_mode", "security_policy", "session_timeout", "request_timeout", "ping_interval", "ttl", "locale", "kind", "driver"})}, {view}, move(where) };
		try{
			vector<ServerCnnctn> y;
			auto rows = co_await DS()->SelectAsync( statement.Move() );
			for( auto&& row : rows )
				y.push_back( ServerCnnctn{move(row)} );
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
namespace Jde::Opc{
	α Gateway::FileSlugError( sv slug, SL sl )ι->up<Exception>{
		let alnum = []( char c ){ return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9'); };
		let valid = !slug.empty() && alnum( slug.front() ) && std::ranges::all_of( slug, [&]( char c ){ return alnum(c) || c=='.' || c=='_' || c=='-'; } );
		return valid ? nullptr : mu<Exception>( Ƒ("Slug '{}' is not valid:  letters, digits, '.', '_' and '-' only, starting with a letter or digit.", slug), ExceptionArgs{EHttpStatus::BadRequest}, sl );
	}
}