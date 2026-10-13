#pragma once
//#238:  the sqlite fixture - the `qlTests` schema (config/qlTests-meta.jsonnet) main.cpp opens on the data source
//config/args/sqlite mounts, `:memory:` under ctest, and the LocalQL the op tests drive the stock crud ops through.  No
//authorizer:  the schema's is null, so Table::Authorize is a no-op and every op runs as the stock op it is - who may run one is
//Jde.Access.Tests' subject.  Beside the doubles, not instead of them:  NullQL and UnitSchema stay what the parser/await tests use.
#include <gtest/gtest.h>
#include <jde/db/Value.h>
#include <jde/ql/LocalQL.h>
#include "RecordingListener.h"

namespace Jde::DB{ struct AppSchema; struct IDataSource; }
namespace Jde::QL::Tests{
	constexpr UserPK System{ UserPK::System };

	//LocalQL leaves the custom/log/status hooks pure.  Every custom hook declines, so a mutation falls through to the stock op.
	struct DataSourceQL final : LocalQL{
		DataSourceQL( vector<sp<DB::AppSchema>> schemas )ι:LocalQL{ move(schemas), {} }{}
		α CustomQuery( TableQL&, Creds, SL )ι->up<TAwait<jvalue>> override{ return nullptr; }
		α CustomMutation( MutationQL&, Creds, SL )ι->up<TAwait<jvalue>> override{ return nullptr; }
		α LogQuery( TableQL&&, Creds, SL )ε->up<TAwait<jvalue>> override{ return nullptr; }
		α LogSettingsQuery( TableQL&&, Creds, SL )ε->up<TAwait<jvalue>> override{ return nullptr; }
		α StatusQuery( TableQL&&, Creds, SL )ε->jobject override{ return {}; }
	};
	α SetQL( sp<DataSourceQL> ql )ι->void;//main.cpp, once the schema is synced.
	α QL()ι->LocalQL&;
	α QLPtr()ι->sp<LocalQL>;
	α Schemas()ι->const vector<sp<DB::AppSchema>>&;
	α DS()ι->DB::IDataSource&;

	//A whole document through the engine, as a client's goes - a select or a mutation, raw (no command-name wrapper).
	α Query( string ql, jobject vars={}, UserPK executer=System, SRCE )ε->jvalue;
	α QueryObject( string ql, jobject vars={}, UserPK executer=System, SRCE )ε->jobject;
	α QueryArray( string ql, jobject vars={}, UserPK executer=System, SRCE )ε->jarray;
	α Create( string ql, jobject vars={}, UserPK executer=System, SRCE )ε->uint;//a `create…{ id }` mutation's new id.
	α Count( sv table, string where={}, vector<DB::Value> params={} )ε->uint;//`select count(*)` over one of the schema's tables, by its unprefixed name.
	α Purge( sv table, sv keyColumn, uint id )ε->void;//`delete from` one of the schema's tables, by its unprefixed name - a test's own rows.

	//The enum's rows every op test names - created once per run by main.cpp, shared by all of them, so never purged.
	constexpr sv Gadget{ "Gadget" };
	constexpr sv Gizmo{ "Gizmo" };
	α EnsureItemTypes()ε->void;

	//The refusal's message, or "" when nothing was refused - of any call, or of a whole document through Query.
	α WhatOf( std::invocable auto&& f )ι->string{
		try{ f(); }
		catch( const runtime_error& e ){ return e.what(); }
		return {};
	}
	α WhatOf( string ql )ι->string;

	//The op tests' base:  the subscriptions a test listens with, stopped in TearDown, and the items it creates, purged there.
	struct OpsFixture : ::testing::Test{
		α TearDown()->void override;
		α Listen( sv text )ε->sp<RecordingListener>;
		α Track( uint id )ι->uint{ return _items.emplace_back( id ); }
		α Item( sv slug, sv itemType=Gadget, sv extra={} )ε->uint;
	protected:
		vector<uint> _items;
	private:
		vector<sp<RecordingListener>> _listeners;
	};
}