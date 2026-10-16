#pragma once
#include <jde/access/Authorize.h>

namespace Jde::Access::Server{
	//`sources` in userRights and resourceRights - one wire format, which access-service.ts reads for both.  In UserRightsAwait.cpp.
	α SourcesJson( const vector<Authorize::RightsSource>& sources )ι->jarray;
	//userRights' and groupRights' rows - one shape, which EffectiveRight.fromRows reads for both.  In UserRightsAwait.cpp.
	α RightsJson( const vector<Authorize::ResourceRights>& resources )ι->jarray;
}