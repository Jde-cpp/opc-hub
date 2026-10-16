#include <jde/access/server/awaits/GroupRightsAwait.h>
#include <jde/db/meta/Table.h>
#include <jde/access/Authorize.h>
#include "../serverInternal.h"
#include "RightsSourceJson.h"

#define let const auto
namespace Jde::Access::Server{
	//Synchronous, as UserRightsAwait is:  the walk is in-memory, and GroupRights takes and releases the shared lock before the resume.
	α GroupRightsAwait::Suspend()ι->void{
		jarray y;
		try{
			const GroupPK groupPK{ _query.AsNumber<GroupPK::Type>("id", _sl) };
			THROW_IFX( !groupPK.Value, Exception(_sl, ELogLevel::Debug, "groupRights needs a group id.") );
			Authorizer().TestUser( _executer, _sl );//the acl check passes anyone, the anonymous included, while acl is unenforced.
			GetTable( "acl" ).Authorize( ERights::Read, _executer, _sl );
			y = RightsJson( Authorizer().GroupRights(groupPK) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			return;
		}
		Resume( jvalue{move(y)} );
	}
}