#pragma once

namespace Jde::DB{ struct AppSchema; struct IDataSource; struct Table; }
namespace QL{ struct LocalQL; }
namespace Jde::Access{ struct Authorize; }
namespace Jde::Access::Server{
	α LocalQL()ι->QL::LocalQL&;
	α Authorizer()ι->Authorize&;
	α DS()ι->DB::IDataSource&;
	α GetTablePtr( str name, SRCE )ε->sp<DB::Table>;
	α GetTable( str name, SRCE )ε->const DB::Table&;
	α AccessSchema()ι->DB::AppSchema&;
	α PublishUserCreated( UserPK userPK, string name )ι->void;//the userCreated event for a user the login procs made - see the definition
}