#pragma once
#include <jde/tests/AccessHelpers.h>
#include "../src/access/OpcAuthorize.h"
#include "../src/access/UAAccess.h"

//What the suites share:  a session as the access-control plugin sees one, the calls open62541 makes on it, an issued token
//as a client presents one, and the nodeset they load.
namespace Jde::Opc::Server::Tests{
	//The pumps nodeset the tests config lists under /opcServer/configFiles - the same file the emulator loads.
	Ξ PumpsNodeset()ε->fs::path{
		for( const auto& p : Settings::FindPathArray("/opcServer/configFiles") ){
			if( p.filename()=="pumps.NodeSet2.xml" )
				return p;
		}
		THROW( "pumps.NodeSet2.xml is not in /opcServer/configFiles." );
	}
	//`slug`'s user, created the first time:  the suites share one db in a run.
	Ξ FindOrCreateUser( App::Client::IAppClient& app, const string& slug )ε->UserPK{
		auto user = app.QuerySync( "user(slug:$slug, deleted:null){id}", {{"slug", slug}} );
		if( user.empty() )
			user = app.QuerySync<jobject>( "createUser( slug:$slug, name:$name ){id}", {{"slug", slug}, {"name", slug+" name"}} );
		return UserPK{ Json::AsNumber<UserPK::Type>(user, "id") };
	}
	//`user`'s session context, as ActivateSession installs one:  unexpiring and with no hub session, unless given.
	Ξ Session( UserPK user, TimePoint expiration=TimePoint::max(), SessionPK id={} )ι->UAAccess::SessionContext{ return { {}, expiration, id, user }; }
	//`node`'s UserAccessLevel, as open62541 asks the plugin for it.
	Ξ AccessLevel( const UA_NodeId& node, UAAccess::SessionContext& session )ι->EAccess{
		return (EAccess)UAAccess::GetUserAccessLevel( GetUAServer().Ptr(), nullptr, nullptr, &session, &node, nullptr );
	}
	Ξ AccessLevel( const UA_NodeId& node, UserPK user )ι->EAccess{ auto session = Session( user ); return AccessLevel( node, session ); }
	Ξ MayBrowse( const UA_NodeId& node, UserPK user )ι->bool{
		auto session = Session( user );
		return UAAccess::AllowBrowseNode( GetUAServer().Ptr(), nullptr, nullptr, &session, &node, nullptr );
	}
	//An issued token, as a client presents it to ActivateSession:  the token and the extension object around it, freed with this.
	struct IssuedIdentity final : noncopyable{
		IssuedIdentity( str token )ι{
			UA_IssuedIdentityToken_init( &_token );
			_token.tokenData = UA_BYTESTRING_ALLOC( token.c_str() );
			UA_ExtensionObject_init( &_identity );
			UA_ExtensionObject_setValueNoDelete( &_identity, &_token, &UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] );
		}
		~IssuedIdentity(){ UA_IssuedIdentityToken_clear( &_token ); }
		//ActivateSession for the session whose context `slot` holds.
		α Activate( void*& slot )ι->UA_StatusCode{
			auto& accessControl = UA_Server_getConfig( GetUAServer().Ptr() )->accessControl;//ActivateSession reads ac->context before it branches.
			return UAAccess::ActivateSession( GetUAServer().Ptr(), &accessControl, nullptr, nullptr, nullptr, &_identity, &slot );
		}
	private:
		UA_IssuedIdentityToken _token;
		UA_ExtensionObject _identity;
	};
}