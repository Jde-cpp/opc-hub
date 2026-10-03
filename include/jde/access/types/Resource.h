#pragma once
#include <jde/access/usings.h>
#include <jde/fwk/co/Await.h>

namespace Jde::Access{
	struct Resource{
		Resource()ι=default;
		Resource( ResourcePK pk, jobject j )ι;
		Resource( jobject j )ι;
		Access::ResourcePK PK{};
		string Schema;
		string Slug;
		string Criteria;
		optional<TimePoint> IsDeleted;
	};
}