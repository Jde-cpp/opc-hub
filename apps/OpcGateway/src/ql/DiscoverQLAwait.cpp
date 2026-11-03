#include "DiscoverQLAwait.h"
#include <jde/access/Authorize.h>
#include <jde/fwk/crypto/Certificate.h>
#include <jde/fwk/str.h>
#include <jde/opc/ClientSecurity.h>
#include "../async/AsyncRequest.h"
#include "../async/BlockingPool.h"
#include "GatewayQL.h"
#define let const auto

namespace Jde::Opc::Gateway{
	//The request's deadline:  its requestTimeout, positive and at most MaxTimeout, else the gateway's held to MaxTimeout.
	Ω requestTimeout( const QL::MutationQL& m, SL sl )ε->Duration{
		let v = m.FindPtr<jvalue>( "requestTimeout" );
		if( !v || v->is_null() )
			return std::min<Duration>( _requestTimeout, DiscoverQLAwait::MaxTimeout );
		let ms = v->try_to_number<uint32>();
		let maxMs = DiscoverQLAwait::MaxTimeout.count();
		THROW_IFX( !ms || *ms==0 || *ms>maxMs, Exception(sl, {EHttpStatus::BadRequest}, "requestTimeout must be a whole number of milliseconds from 1 to {}.", maxMs) );
		return Duration{ std::chrono::milliseconds{*ms} };
	}

	α DiscoverQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			QL().Authorizer().Test( "gateway", "serverConnections", Access::ERights::Update, UserPK(), _sl );
			const string url{ Str::Trim(sv{_query.As<jstring>("url")}) };
			THROW_IFX( url.empty(), Exception(_sl, {EHttpStatus::BadRequest}, "A url is required.") );
			let servers = _query.Find<bool>( "servers" ).value_or( false );
			let timeout = requestTimeout( _query, _sl );
			let h = Discovery::UrlHandle( url );
			//On the discovery pool, rows and all:  the frame holds `url` and the request until the resume.  Run with no selection too,
			//so a caller that selects nothing still hears a url that isn't reached.
			jvalue y = co_await DiscoveryAwait<jvalue>{ [&, this]()->jvalue{
				if( servers ){
					let found = Discovery::FindServers( url, timeout, h, _sl );
					return _query.ResultRequest ? ServerRows( *_query.ResultRequest, found ) : jarray{};
				}
				let found = Discovery::GetEndpoints( url, timeout, h, _sl );
				return _query.ResultRequest ? EndpointRows( *_query.ResultRequest, found ) : jarray{};
			}, _sl };
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α DiscoverQLAwait::ServerRows( const QL::TableQL& q, const vector<Discovery::Server>& servers )ι->jarray{
		jarray y;
		for( let& server : servers ){
			jobject row;
			if( q.FindColumn("applicationUri") )
				row["applicationUri"] = server.ApplicationUri;
			if( q.FindColumn("productUri") )
				row["productUri"] = server.ProductUri;
			if( q.FindColumn("applicationName") )
				row["applicationName"] = server.ApplicationName;
			if( q.FindColumn("applicationType") )
				row["applicationType"] = Discovery::ApplicationTypeName( server.Type );
			if( q.FindColumn("gatewayServerUri") )
				row["gatewayServerUri"] = server.GatewayServerUri;
			if( q.FindColumn("discoveryProfileUri") )
				row["discoveryProfileUri"] = server.DiscoveryProfileUri;
			if( q.FindColumn("discoveryUrls") ){
				jarray urls;
				for( let& url : server.DiscoveryUrls )
					urls.emplace_back( url );
				row["discoveryUrls"] = move( urls );
			}
			y.push_back( move(row) );
		}
		return y;
	}

	//certificate{ subject issuer notBefore notAfter uri thumbprint fingerprint }:  what Crypto::Certificate reads of the DER, null
	//for an endpoint that carries none, and the parse failure in `error` for one that carries something else.
	Ω certificateJson( const QL::TableQL& q, std::span<const byte> der )ι->jvalue{
		if( der.empty() )
			return nullptr;
		try{
			const Crypto::Certificate cert{ der };
			jobject y;
			if( q.FindColumn("subject") )
				y["subject"] = cert.DistinguishedName;
			if( q.FindColumn("issuer") )
				y["issuer"] = cert.Issuer;
			if( q.FindColumn("notBefore") )
				y["notBefore"] = ToIsoString<std::chrono::seconds>( cert.NotBefore );
			if( q.FindColumn("notAfter") )
				y["notAfter"] = ToIsoString<std::chrono::seconds>( cert.Expiration );
			if( q.FindColumn("uri") )
				y["uri"] = cert.SanUri();
			if( q.FindColumn("thumbprint") )
				y["thumbprint"] = cert.Thumbprint;
			if( q.FindColumn("fingerprint") )
				y["fingerprint"] = cert.Fingerprint;
			return y;
		}
		catch( const std::exception& e ){
			return jobject{ {"error", e.what()} };
		}
	}

	α DiscoverQLAwait::EndpointRows( const QL::TableQL& q, const vector<Discovery::Endpoint>& endpoints )ι->jarray{
		jarray y;
		for( let& ep : endpoints ){
			jobject row;
			if( q.FindColumn("url") )
				row["url"] = ep.Url;
			if( q.FindColumn("applicationUri") )
				row["applicationUri"] = ep.ApplicationUri;
			if( q.FindColumn("policy") )
				row["policy"] = ep.PolicyUri;
			if( q.FindColumn("mode") )
				row["mode"] = SecurityModeName( ep.Mode );
			if( q.FindColumn("securityLevel") )
				row["securityLevel"] = ep.SecurityLevel;
			if( let tokens = q.FindTable("tokenPolicies"); tokens ){
				jarray policies;
				for( let& utp : ep.TokenPolicies ){
					jobject policy;
					if( tokens->FindColumn("type") )
						policy["type"] = TokenTypeName( utp.Type );
					if( tokens->FindColumn("policy") )
						policy["policy"] = utp.Policy;
					if( tokens->FindColumn("policyId") )
						policy["policyId"] = utp.PolicyId;
					policies.push_back( move(policy) );
				}
				row["tokenPolicies"] = move( policies );
			}
			if( let cert = q.FindTable("certificate"); cert )
				row["certificate"] = certificateJson( *cert, ep.Certificate );
			y.push_back( move(row) );
		}
		return y;
	}
}