#pragma once
#include <jde/access/usings.h>

namespace Jde::DB{ struct AppSchema; struct IDataSource; struct Table; }
namespace Jde::Access{
	struct Authorize;
	enum class ESubscription : uint16{
		Created=0x1, Updated=0x2, Deleted=0x4, Restored=0x8, Purged=0x10, Added=0x20, Removed=0x40,
		User=0x80, Group=0x100, Permission=0x200, Role=0x400, Resources=0x800, Acl=0x1000
	};

	α GetSchema()ι->DB::AppSchema&;
	α GetSchemaPtr()ι->sp<DB::AppSchema>;
	α SetSchema( sp<DB::AppSchema> schema )ι->void;
	α InstanceSchemaName( str schemaName, str opcServerInstance )ι->string;
}