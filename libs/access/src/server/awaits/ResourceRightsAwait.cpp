#include <jde/access/server/awaits/ResourceRightsAwait.h>
#include <jde/db/meta/Table.h>
#include <jde/fwk/chrono.h>
#include <jde/access/Authorize.h>
#include "../serverInternal.h"
#include "RightsSourceJson.h"

#define let const auto
namespace Jde::Access::Server{
	template<class T> Ω toJson( const flat_map<T,Authorize::HolderRights>& holders, bool effective )ι->jarray{
		jarray y;
		for( let& [pk, holder] : holders ){
			jobject row{ {"id", pk.Value}, {"allowed", underlying(holder.Rights.Allowed)}, {"denied", underlying(holder.Rights.Denied)} };
			if( effective )
				row["effective"] = underlying( holder.Rights.Effective() );
			row["sources"] = SourcesJson( holder.Sources );
			y.push_back( move(row) );
		}
		return y;
	}

	//Synchronous, as IdentityRightsAwait is:  the walk is in-memory, and Holders takes and releases the shared lock before the resume.
	α ResourceRightsAwait::Suspend()ι->void{
		jobject y;
		try{
			const ResourcePK resourcePK{ _query.AsNumber<ResourcePK::Type>("id", _sl) };
			THROW_IFX( !resourcePK.Value, Exception(_sl, ELogLevel::Debug, "resourceRights needs a resource id.") );
			Authorizer().TestUser( _executer, _sl );//the acl check passes anyone, the anonymous included, while acl is unenforced.
			GetTable( "acl" ).Authorize( ERights::Read, _executer, _sl );
			let holders = Authorizer().Holders( resourcePK );
			THROW_IFX( !holders, Exception(_sl, ExceptionArgs{EHttpStatus::NotFound}, "Resource '{}' not found.", resourcePK.Value) );
			let& resource = holders->Resource;
			y["resource"] = jobject{
				{"id", resource.PK.Value},
				{"schemaName", resource.Schema},
				{"slug", resource.Slug},
				{"criteria", resource.Criteria},
				{"deleted", resource.IsDeleted ? jvalue{ ToIsoString(*resource.IsDeleted) } : jvalue{ nullptr }}
			};
			y["roles"] = toJson( holders->Roles, false );
			y["groups"] = toJson( holders->Groups, false );
			y["users"] = toJson( holders->Users, true );//only a user's rights are enforced - a role's or group's are what it passes on.
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			return;
		}
		Resume( jvalue{move(y)} );
	}
}