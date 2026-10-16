#include <jde/access/server/awaits/IdentityRightsAwait.h>
#include <jde/db/meta/Table.h>
#include <jde/fwk/chrono.h>
#include <jde/access/Authorize.h>
#include "../serverInternal.h"
#include "RightsSourceJson.h"

#define let const auto
namespace Jde::Access{
	α Server::SourcesJson( const vector<Authorize::RightsSource>& sources )ι->jarray{
		jarray y;
		for( let& source : sources ){
			jarray path;
			for( let group : source.Groups )
				path.push_back( jobject{ {"id", group.Value}, {"type", "group"} } );
			for( let role : source.Roles )
				path.push_back( jobject{ {"id", role.Value}, {"type", "role"} } );
			y.push_back( jobject{ {"permissionId", source.Permission.Value}, {"allowed", underlying(source.Allowed)}, {"denied", underlying(source.Denied)}, {"path", move(path)} } );
		}
		return y;
	}
}
namespace Jde::Access::Server{
	//userRights' and groupRights' rows - one shape, which EffectiveRight.fromRows reads for both.
	Ω RightsJson( const vector<Authorize::ResourceRights>& resources )ι->jarray{
		jarray y;
		for( let& resource : resources ){
			jobject jresource{ {"id", resource.PK.Value} };
			if( resource.Cached ){
				jresource["schemaName"] = resource.Cached->Schema;
				jresource["slug"] = resource.Cached->Slug;
				jresource["criteria"] = resource.Cached->Criteria;
				jresource["deleted"] = resource.Cached->IsDeleted ? jvalue{ ToIsoString(*resource.Cached->IsDeleted) } : jvalue{ nullptr };
			}
			y.push_back( jobject{
				{"resource", move(jresource)},
				{"allowed", underlying(resource.Rights.Allowed)},
				{"denied", underlying(resource.Rights.Denied)},
				{"effective", underlying(resource.Rights.Effective())},
				{"sources", SourcesJson(resource.Sources)}
			} );
		}
		return y;
	}

	//Synchronous:  the walk is in-memory, so the value is resumed from Suspend itself (await_suspend has stored the handle by
	//then, as AclQLSelectAwait's ResumeExp relies on).  Authorize's shared lock is taken and released inside UserRights and
	//GroupRights, before the resume - the continuation may run the request's next table inline and take it again on this thread.
	α IdentityRightsAwait::Suspend()ι->void{
		jarray y;
		try{
			let isGroup = _query.JsonName=="groupRights";
			let id = _query.AsNumber<IdentityPK::Type>( "id", _sl );
			THROW_IFX( !id, Exception(_sl, ELogLevel::Debug, "{} needs a {} id.", _query.JsonName, isGroup ? "group" : "user") );
			Authorizer().TestUser( _executer, _sl );//the acl check passes anyone, the anonymous included, while acl is unenforced.
			if( isGroup || _executer.Value!=id )//one's own rights are always readable - "why am I denied" should not itself need a grant.
				GetTable( "acl" ).Authorize( ERights::Read, _executer, _sl );
			y = RightsJson( isGroup ? Authorizer().GroupRights(GroupPK{id}) : Authorizer().UserRights(UserPK{id}) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			return;
		}
		Resume( jvalue{move(y)} );
	}
}