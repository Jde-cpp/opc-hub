#include <jde/access/server/awaits/LoginAwait.h>
#include <jde/access/server/accessServer.h>
#include <jde/access/usings.h>
#include <jde/db/IDataSource.h>
#include <jde/db/Value.h>
#include <jde/db/generators/InsertClause.h>
#include <jde/db/generators/Statement.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/access/Authorize.h>
#include "../serverInternal.h"

#define let const auto
namespace Jde::Access::Server{

	LoginAwait::LoginAwait( Crypto::PublicKey publicKey, vector<byte> certificate, string&& description, SL sl )ι:
		base{ sl }, _certificate{ move(certificate) }, _description{ move(description) }, _publicKey{ move(publicKey) }
	{}

	α LoginAwait::LoginTask()ι->TAwait<optional<UserPK::Type>>::Task{
		try{
			let userTable = GetTablePtr( "users" );
			let identityTable = GetTablePtr( "identities" );
			DB::WhereClause where;
			where.Add( userTable->GetColumnPtr("modulus"), DB::Value{_publicKey.ModulusHex()} );
			where.Add( userTable->GetColumnPtr("exponent"), DB::Value{_publicKey.ExponentInt()} );
			where.Add( userTable->GetColumnPtr("provider_id"), DB::Value{underlying(EProviderType::Key)} );
			DB::Statement statement{
				{ userTable->GetPK() },
				{ DB::Join{userTable->GetPK(), identityTable->GetPK()} },
				move( where )
			};
			auto sql = statement.Move();
			let userPK = co_await DS().ScalerOpt<UserPK::Type>( move(sql) );
			if( !userPK ){ //enrollment - the presented certificate must chain to a trust anchor and bind the jwt key; it is also the identity authority.
				THROW_IF( _certificate.empty(), "Public key not enrolled and no certificate presented." );
				TrustVerify( _certificate, _sl );
				THROW_IF( Crypto::ExtractPublicKey(_certificate, _sl)!=_publicKey, "Certificate public key does not match jwt key." );
				Crypto::Certificate info{ _certificate, _sl };
				THROW_IF( info.CommonName.empty(), "Certificate subject CN is required for enrollment." );
				auto name = info.Upn.size() ? info.Upn : info.Email.size() ? info.Email : info.CommonName;//UPN → email → CN.
				InsertUser( _publicKey.ModulusHex(), _publicKey.ExponentInt(), move(info), move(name) );
			}
			else
				ResumeScaler( {*userPK} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α LoginAwait::InsertUser( string&& modulusHex, uint32_t exponent, Crypto::Certificate&& info, string&& name )ι->DB::ScalerAwait<UserPK::Type>::Task{
		DB::InsertClause insert{ AccessSchema().Prefix+"user_insert_key",
			{ DB::Value{move(modulusHex)}, DB::Value{exponent}, DB::Value{underlying(EProviderType::Key)},
				DB::Value{ name }, //users.name
				DB::Value{ move(info.CommonName) }, //users.slug
				DB::Value{ move(_description) }, DB::Value{ move(info.Issuer) },
				DB::Value{ move(info.SubjectAltName) },
				DB::Value{ move(info.DistinguishedName) },
				info.Email.empty() ? DB::Value{ nullptr } : DB::Value{ move(info.Email) }, DB::Value{ info.Expiration },
				DB::Value{ move(info.Fingerprint) }} };
		try{
			UserPK userPK{ co_await DS().InsertSeq<UserPK::Type>(move(insert)) };
			Authorizer().CreateUser( userPK, name );
			PublishUserCreated( userPK, move(name) );//the clients' caches - CreateUser is the server's own
			ResumeScaler( userPK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α LoginAwait::Suspend()ι->void{
		LoginTask();
	}
}