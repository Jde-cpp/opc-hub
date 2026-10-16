#pragma once
#include <jde/access/Authorize.h>

namespace Jde::Access::Server{
	//`sources` in userRights, groupRights and resourceRights - one wire format, which access-service.ts reads for all three.  In IdentityRightsAwait.cpp.
	α SourcesJson( const vector<Authorize::RightsSource>& sources )ι->jarray;
}