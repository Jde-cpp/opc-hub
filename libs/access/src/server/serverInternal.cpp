#include <jde/access/server/accessServer.h>
#include <mutex>
#include <jde/db/meta/AppSchema.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/crypto/TrustStore.h>
#include <jde/fwk/settings.h>
#include <jde/ql/LocalQL.h>
#include <jde/ql/LocalSubscriptions.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/MutationQL.h>
#include <jde/access/awaits/ConfigureAwait.h>
#include "serverInternal.h"
#include "jde/access/server/awaits/AclAwait.h"
#include "jde/access/server/awaits/RoleAwait.h"
#include "jde/access/server/awaits/ProfileAwait.h"
#include "jde/access/server/awaits/UserRightsAwait.h"
#include "awaits/GroupAwait.h"
#include "awaits/UserAwait.h"
#include "../accessInternal.h"


namespace Jde::Access{
	constexpr ELogTags _tags{ ELogTags::Access };
	static sp<QL::LocalQL> _ql;
	static std::mutex _anchorMutex;
	static flat_map<fs::path, fs::file_time_type> _anchorFiles;//mtime at load - failed files are recorded too, retried only when they change.
	static flat_set<fs::path> _missingDirs;//warned about once - every failed verification rescans, and a dir that is never created (a product not installed) would warn each time (reviews/install-issues.md, "Noise in a production log").

	Ω loadTrustAnchors( Crypto::TrustStore& trust )ι->bool{//loads new/changed certs from /access/trustedCertDirs; true if an anchor was added.
		std::lock_guard _{ _anchorMutex };
		bool added{};
		uint skipped{};//files passed over for their extension - see the warning below (install-issues #33).
		for( const string& sdir : Settings::FindStringArray("/access/trustedCertDirs") ){
			try{
				const fs::path dir{ sdir };
				if( !fs::is_directory(dir) ){//normal pre-provisioning state - no client cert has been anchored yet.
					const auto level = _missingDirs.emplace( dir ).second ? ELogLevel::Warning : ELogLevel::Debug;//not inline in LOG - the macro evaluates its level twice.
					LOG( level, _tags, "Trusted certificate directory does not exist: '{}' - no client certificate is anchored from it until it does (rescanned on a failed verification).", sdir );
					continue;
				}
				_missingDirs.erase( dir );
				for( const auto& entry : fs::directory_iterator(dir) ){
					if( !Crypto::IsCertificateFile(entry.path()) ){//install-issues #33:  a client publishes DER, and skipping it in silence made a copied-in certificate do nothing.
						++skipped;
						DBGT( _tags, "Not a certificate, skipped: '{}' ({} are read).", entry.path().string(), Crypto::CertificateExtensions );
						continue;
					}
					const auto mtime = entry.last_write_time();
					if( auto it = _anchorFiles.find(entry.path()); it!=_anchorFiles.end() && it->second==mtime )
						continue;
					_anchorFiles[entry.path()] = mtime;
					try{
						trust.AddCertificate( Crypto::ReadCertificate(entry.path()) );
						added = true;
						INFO( "Added trust anchor: '{}'.", entry.path().string() );
					}
					catch( const std::exception& e ){
						CRITICAL( "Could not load trust anchor '{}': {}", entry.path().string(), e.what() );
					}
				}
			}
			catch( const std::exception& e ){
				CRITICAL( "Could not scan trusted certificate directory '{}': {}", sdir, e.what() );
			}
		}
		//install-issues #33:  a directory holding only files this build would not read is indistinguishable, from the operator's
		//side, from one they have not filled yet - the key login is refused either way and nothing says why.  _anchorFiles holds
		//every file ever attempted, loaded or not, so empty-and-skipped is exactly "you copied something in and it was ignored".
		//Once per rescan that anchored nothing, not per file:  a failed verification rescans, so per-file would repeat under a flood.
		if( _anchorFiles.empty() && skipped )
			WARNT( _tags, "No trust anchors under /access/trustedCertDirs - {} file{} passed over for {} extension.  {} are read.", skipped, skipped==1 ? " was" : "s were", skipped==1 ? "its" : "their", Crypto::CertificateExtensions );
		return added;
	}
	α Server::AccessSchema()ι->DB::AppSchema&{ return GetSchema(); }
	α Server::LocalQL()ι->QL::LocalQL&{ ASSERT(_ql);  return *_ql; }
	//The login procs (user_insert_login, user_insert_key) create users outside the mutation path, so the userCreated event every
	//client's AccessListener subscribes to (EventsSubscribeAwait) never fired for them - a user born after a client's Configure
	//was absent from that client's snapshot, and denied on a protected node tree, until the client restarted (opcserver-review3
	//#16's user-snapshot gap).  The same fan-out the createUser mutation gets from IMutationAwait::Publish, by hand:  the id in
	//the args is what the subscription's `{id}` is trimmed from.  Called after the server's own CreateUser, so an in-process
	//listener finds the user already there.
	α Server::PublishUserCreated( UserPK userPK )ι->void{
		try{
			QL::MutationQL m{ "createUser", jobject{{"id", userPK.Value}}, ms<jobject>(), {}, false, LocalQL().Schemas(), false };
			QL::Subscriptions::OnMutation( m, jvalue{userPK.Value} );
		}
		catch( const std::exception& e ){
			WARNT( _tags, "[{}]userCreated not published - a client configured before this login will not see the user until it reloads: {}", userPK.Value, e.what() );
		}
	}
	α Server::Authorizer()ι->Access::Authorize&{ return LocalQL().Authorizer(); }

	α Server::Authenticate( str loginName, uint providerId, str opcServer, SL sl )ι->AuthenticateAwait{
		return AuthenticateAwait{ loginName, providerId, opcServer, sl };
	}
	α Server::Trust()ε->Crypto::TrustStore&{
		static Crypto::TrustStore _trust = []{
			Crypto::TrustStore trust{ false };//operator anchors only - OS roots would let any public-CA cert holder enroll.
			loadTrustAnchors( trust );
			return trust;
		}();
		return _trust;
	}
	α Server::TrustVerify( std::span<const byte> der, SL sl )ε->void{
		auto& trust = Trust();
		try{
			trust.Verify( der, sl );
		}
		catch( ... ){
			if( !loadTrustAnchors(trust) )//an anchor copied in after startup shouldn't require a restart - rescan before failing.
				throw;
			trust.Verify( der, sl );
		}
	}
	α Server::DS()ι->DB::IDataSource&{ return LocalQL().DS(); }
	α Server::GetTablePtr( str name, SL sl )ε->sp<DB::Table>{ return LocalQL().GetTablePtr(name, sl); }
	α Server::GetTable( str name, SL sl )ε->const DB::Table&{ return LocalQL().GetTable(name, sl); }

	α Server::Configure( vector<sp<DB::AppSchema>>&& schemas, sp<QL::LocalQL> localQL, UserPK executer, sp<Authorize> authorizer, sp<AccessListener> listener )ε->ConfigureAwait{
		auto accessSchema = find_if( schemas, [](const sp<DB::AppSchema>& x){return x->Name=="access";} );
		THROW_IF( accessSchema==schemas.end(), "Access schema not found in schemas" );
		SetSchema( *accessSchema );
		_ql = localQL;
		QL::Hook::Add( mu<GroupHook>() );//add before
		QL::SetSystemTables( {"userRights"} );//no view of its own - the parser admits the name and CustomQuery answers it (UserRightsAwait); here so the AppServer, the hub and the tests all get it.
		return ConfigureAwait{ localQL, move(schemas), authorizer, executer, move(listener), {}, false, true };//allSchemas: the server's cache is everyone's authority.
	}
	α Server::CustomQuery( QL::TableQL& q, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		up<TAwait<jvalue>> y;
		if( q.JsonName=="userRights" )//before the starts_with("user") branch below.
			y = mu<UserRightsAwait>( q, creds.UserPK(), sl );
		else if( q.DBTableName()=="acl" )
			y = mu<AclQLSelectAwait>( q, creds.UserPK(), sl );
		else if( q.JsonName.starts_with("role") && (q.FindTable("roles") || q.FindTable("permissionRights")) )
			y = mu<RoleAwait>( q, creds.UserPK(), sl );
		else if( q.JsonName.starts_with("user") && q.FindTable("groups") )
			y = mu<UserAwait>( move(q), creds.UserPK(), sl );
		else if( q.JsonName=="group" || q.JsonName=="groups" )
			y = mu<GroupAwait>( q, creds.UserPK(), sl );
		else if( q.DBTableName()=="profiles" )
			q.AddFilter( "identity_id", creds.UserPK().Value );//y stays null → stock select runs scoped to the executer. UserPK 0 matches nothing.
		return y;
	}
	α Server::CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		up<TAwait<jvalue>> y;
		using enum QL::EMutationQL;
		if( m.TableName()=="acl" && (m.Type==Purge || m.Type==Create) )
			y = mu<Access::Server::AclQLAwait>( move(m), creds.UserPK(), sl );
		else if( (m.Type==Add || m.Type==Remove) && m.TableName()=="roles" )
			y = mu<Access::Server::RoleMAwait>( move(m), creds.UserPK(), sl );
		else if( m.TableName()=="profiles" )//all types - stock UpdateAwait keys on id/name/slug, none of which profiles has - url is scoped to the executer here.
			y = mu<Access::Server::ProfileAwait>( move(m), creds.UserPK(), sl );
		return y;
	}
}