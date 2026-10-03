#include <jde/access/server/awaits/UserRightsAwait.h>
#include <jde/db/meta/Table.h>
#include <jde/fwk/chrono.h>
#include <jde/access/Authorize.h>
#include "../serverInternal.h"

#define let const auto
namespace Jde::Access::Server{
	//Synchronous:  the walk is in-memory, so the value is resumed from Suspend itself (await_suspend has stored the handle by
	//then, as AclQLSelectAwait's ResumeExp relies on).  Authorize's shared lock is taken and released inside UserRights, before
	//the resume - the continuation may run the request's next table inline and take it again on this thread.
	α UserRightsAwait::Suspend()ι->void{
		jarray y;
		try{
			const UserPK userPK{ _query.AsNumber<UserPK::Type>("id", _sl) };
			THROW_IFX( !userPK.Value, Exception(_sl, ELogLevel::Debug, "userRights needs a user id.") );
			if( _executer!=userPK )//one's own rights are always readable - "why am I denied" should not itself need a grant.
				GetTable( "acl" ).Authorize( ERights::Read, _executer, _sl );
			for( let& resource : Authorizer().UserRights(userPK) ){
				jobject jresource{ {"id", resource.PK.Value} };
				if( resource.Cached ){
					jresource["schemaName"] = resource.Cached->Schema;
					jresource["slug"] = resource.Cached->Slug;
					jresource["criteria"] = resource.Cached->Criteria;
					jresource["deleted"] = resource.Cached->IsDeleted ? jvalue{ ToIsoString(*resource.Cached->IsDeleted) } : jvalue{ nullptr };
				}
				jarray sources;
				for( let& source : resource.Sources ){
					jarray path;
					for( let group : source.Groups )
						path.push_back( jobject{ {"id", group.Value}, {"type", "group"} } );
					for( let role : source.Roles )
						path.push_back( jobject{ {"id", role.Value}, {"type", "role"} } );
					sources.push_back( jobject{ {"permissionId", source.Permission.Value}, {"allowed", underlying(source.Allowed)}, {"denied", underlying(source.Denied)}, {"path", move(path)} } );
				}
				y.push_back( jobject{
					{"resource", move(jresource)},
					{"allowed", underlying(resource.Rights.Allowed)},
					{"denied", underlying(resource.Rights.Denied)},
					{"effective", underlying(resource.Rights.Effective())},
					{"sources", move(sources)}
				} );
			}
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			return;
		}
		Resume( jvalue{move(y)} );
	}
}
