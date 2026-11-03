#include "TrustServerQLAwait.h"
#include <jde/access/Authorize.h>
#include <jde/fwk/settings.h>
#include <jde/opc/ServerTrust.h>
#include "../UAClient.h"
#include "GatewayQL.h"
#define let const auto

namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ ELogTags::App };
	//A thumbprint is the SHA-1 as upper hex, no separators (Crypto::Certificate) - and it names a file, so anything else is refused
	//before it can reach outside the rejected directory.
	Ω validThumbprint( sv thumbprint )ι->bool{ return thumbprint.size() && std::ranges::all_of( thumbprint, []( char c ){ return std::isxdigit((unsigned char)c); } ); }
	//The ssl/servers dir trustServer moves into - the rejected directory's parent - must be among /gateway/trustedCertDirs, or a
	//move there trusts nothing (ServerTrust reads only those).  Normalized string compare:  fs::equivalent needs both to exist, and
	//the target may not yet.
	Ω inTrustedCertDirs( const fs::path& dir )ι->bool{
		let target = dir.lexically_normal();
		for( let& configured : Settings::FindStringArray("/gateway/trustedCertDirs") ){
			if( fs::path{configured}.lexically_normal()==target )
				return true;
		}
		return false;
	}

	α TrustServerQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			QL().Authorizer().Test( "gateway", "serverConnections", Access::ERights::Administer, UserPK(), _sl );//an admin's decision (spec *Open questions* #8).
			const string slug{ _query.As<jstring>("opc") };
			const string thumbprint{ _query.As<jstring>("thumbprint") };
			THROW_IFX( slug.empty(), Exception(_sl, {EHttpStatus::BadRequest}, "An opc slug is required.") );
			THROW_IFX( !validThumbprint(thumbprint), Exception(_sl, {EHttpStatus::BadRequest}, "A thumbprint is required, the SHA-1 hex rejectedCertificate answered.") );
			let rejectedDir = ServerTrust::RejectedDir( "/gateway" );
			let source = rejectedDir/Ƒ( "{}.der", thumbprint );
			std::error_code ec;
			THROW_IFX( !fs::exists(source, ec), Exception(_sl, {EHttpStatus::NotFound}, "No rejected certificate '{}' to trust - it may have been trusted already.", thumbprint) );
			let trustedDir = rejectedDir.parent_path();//ssl/servers, the rejected directory's sibling (spec *Trust*).
			THROW_IFX( !inTrustedCertDirs(trustedDir), Exception(_sl, {EHttpStatus::BadRequest}, "'{}' is not among /gateway/trustedCertDirs, so moving the certificate there would trust nothing - add it to that setting.", trustedDir.string()) );
			fs::create_directories( trustedDir, ec );
			let target = trustedDir/Ƒ( "{}.der", thumbprint );
			fs::rename( source, target, ec );
			if( ec ){//a cross-device rename (the rejected dir on another mount) falls back to copy + remove.
				fs::copy_file( source, target, fs::copy_options::overwrite_existing, ec );
				THROW_IFX( ec, Exception(_sl, {EHttpStatus::InternalServerError}, "Could not move '{}' to '{}':  {}", source.string(), target.string(), ec.message()) );
				fs::remove( source, ec );
			}
			INFO( "Trusted server certificate '{}' for '{}':  moved to '{}'.", thumbprint, slug, target.string() );
			UAClient::SetRejectedThumbprint( slug, {} );//the standing rejection is gone the moment the file moves;  the reconnect clears it too on success.
			UAClient::ConnectionEdited( DB::Key{slug}, false );//the trust list is loaded per connect, so reconnect the slug on it.
			Resume( jvalue{true} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
		co_return;//no co_await above - co_return makes Query the coroutine its Task return type needs.
	}
}