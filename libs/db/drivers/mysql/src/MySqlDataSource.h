#pragma once
#include "exports.h"
#include "usings.h"
#include <jde/db/Value.h>
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Syntax.h>
#include "MySqlSyntax.h"

extern "C" ΓMY Jde::DB::IDataSource* GetDataSource();

namespace Jde::DB::MySql{
	struct MySqlServerMeta; struct Session;
	struct MySqlDataSource final : IDataSource{
		~MySqlDataSource() override;
		α Query( Sql&& sql, bool outParams, SRCE )ε->QueryAwait override;

		α ServerMeta()ι->IServerMeta& override;
		α Syntax()ι->const DB::Syntax& override{ return MySqlSyntax::Instance(); }

		α AtSchema( sv schema, SRCE )ε->sp<IDataSource> override;
		α SchemaNameConfig( SRCE )ι->string override;
		α SetConfig( const jobject& config )ε->void override;
		α Disconnect()ε->void override;
		α ConnectionParams()ι->const mysql::connect_params&{ return _cs; }
	private:
		α Execute( Sql&& sql, SL sl, Params exeParams )ε->uint override; //C1: the one primitive; IDataSource implements the sync wrappers over it.
		α InsertSeqSyncUInt( DB::InsertClause&& insert, SL sl )ε->uint override;
		α AcquireSession( SL sl )ε->up<Session>; //pooled or fresh connection.
		α ReleaseSession( up<Session>&& session )ι->void;
		up<MySqlServerMeta> _schemaProc;
		mysql::connect_params _cs;
		vector<up<Session>> _idleSessions; mutex _idleSessionsMutex;
	};
}