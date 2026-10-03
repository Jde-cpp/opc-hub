#include <jde/access/types/Resource.h>

#define let const auto
namespace Jde::Access{
	Resource::Resource( ResourcePK pk, jobject j )ι:
		PK{ pk },
		Schema{ string{Json::FindDefaultSV(j, "schemaName")} },
		Slug{ string{Json::FindDefaultSV(j, "slug")} },
		Criteria{ string{Json::FindDefaultSV(j, "criteria")} },
		IsDeleted{ Json::FindTimePoint(j, "deleted") }
	{}
	Resource::Resource( jobject j )ι:
		Resource{ Json::FindNumber<ResourcePK>(j, "id").value_or(0), j }
	{}
}