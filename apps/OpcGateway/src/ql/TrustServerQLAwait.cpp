#include "TrustServerQLAwait.h"
#include <jde/fwk/str.h>
#include <jde/access/Authorize.h>
#include <jde/opc/ServerTrust.h>
#include "../UAClient.h"
#include "GatewayQL.h"
#define let const auto

namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ ELogTags::App };
	//A thumbprint is the SHA-1 as upper hex, no separators (Crypto::Certificate) - and it names a file, so anything else is refused
	//before it can reach outside the rejected directory.
	Ω validThumbprint( sv thumbprint )ι->bool{ return thumbprint.size() && std::ranges::all_of( thumbprint, []( char c ){ return std::isxdigit((unsigned char)c); } ); }
	Ω normal( const fs::path& dir )ι->fs::path{//"a/b/" and "a/b" alike
		let y = dir.lexically_normal();
		return y.has_filename() ? y : y.parent_path();
	}
	//The dir trustServer moves into, /gateway/serversDir, must be among the directories the verifier trusts (ServerTrust::TrustedDirs,
	//the override included), or a move there trusts nothing.  fs::equivalent where both exist - a trailing separator, a symlink, a
	//case difference on Windows - else the normalized paths.
	Ω inTrustedCertDirs( const fs::path& dir )ι->bool{
		let target = normal( dir );
		for( let& configured : ServerTrust::TrustedDirs("/gateway") ){
			std::error_code ec;
			if( normal(configured)==target || fs::equivalent(configured, dir, ec) )
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
			//the named connection's standing rejection, what its page offered:  the slug is what the log credits and what reconnects.
			THROW_IFX( UAClient::RejectedThumbprint(slug)!=thumbprint, Exception(_sl, {EHttpStatus::NotFound}, "'{}' has no standing rejection of certificate '{}' - it may have been trusted already.", slug, thumbprint) );
			let rejectedDir = ServerTrust::RejectedDir( "/gateway" );
			let source = rejectedDir/Ƒ( "{}.der", thumbprint );
			std::error_code ec;
			THROW_IFX( !fs::exists(source, ec), Exception(_sl, {EHttpStatus::NotFound}, "No rejected certificate '{}' to trust - it may have been trusted already.", thumbprint) );
			let trustedDir = ServerTrust::ServersDir( "/gateway" );//ssl/servers (spec *Trust*).
			THROW_IFX( !inTrustedCertDirs(trustedDir), Exception(_sl, {EHttpStatus::BadRequest}, "'{}' (/gateway/serversDir) is not among /gateway/trustedCertDirs, so moving the certificate there would trust nothing - add it to that setting.", trustedDir.string()) );
			fs::create_directories( trustedDir, ec );
			let target = trustedDir/Ƒ( "{}.der", thumbprint );
			fs::rename( source, target, ec );
			if( ec ){//a cross-device rename (the rejected dir on another mount) falls back to copy + remove.
				fs::copy_file( source, target, fs::copy_options::overwrite_existing, ec );
				THROW_IFX( ec, Exception(_sl, {EHttpStatus::InternalServerError}, "Could not move '{}' to '{}':  {}", source.string(), target.string(), ec.message()) );
				fs::remove( source, ec );
			}
			//The file is every slug's that rejected the certificate - two connections to one server:  each one's standing rejection goes
			//with it, and each reconnects now, on the trust list loaded per connect, rather than on its own retry.
			let slugs = UAClient::ClearRejectedThumbprint( thumbprint );
			INFO( "Trusted server certificate '{}' from '{}':  moved to '{}' - reconnecting {}.", thumbprint, slug, target.string(), Str::Join(slugs, ", ") );
			for( let& rejecting : slugs )
				UAClient::ConnectionEdited( DB::Key{rejecting}, false );
			Resume( jvalue{true} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
		co_return;//no co_await above - co_return makes Query the coroutine its Task return type needs.
	}
}