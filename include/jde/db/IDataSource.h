#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/fwk/co/BlockAwait.h>
#include "awaits/DBAwait.h"
#include "awaits/ExecuteAwait.h"
#include "awaits/QueryAwait.h"
#include "awaits/ScalerAwait.h"
#include "awaits/SelectAwait.h"
#include "meta/Column.h"
#include "meta/Table.h"
#include "Row.h"
#include "generators/InsertClause.h"

namespace Jde::DB{
	struct IServerMeta; struct Sql; struct Syntax;

	struct ΓDB IDataSource : std::enable_shared_from_this<IDataSource>{
		virtual ~IDataSource()=default;
		β Disconnect()ε->void = 0;
		α CatalogName( SRCE )ε->string;
		α SchemaName( SRCE )ε->string;
		β SchemaNameConfig( SL=SRCE_CUR )ι->string{ return {}; } //schema name in connection string.
		β RequiresSync()ι->bool{ return false; } //sqlite :memory:=true
		//True when a query completes on the calling thread - sqlite is in-process, there is no socket to await.  The
		//awaitables use it to finish in await_ready instead of suspending and resuming inline, which would leave the
		//caller's frame under the resumed continuation and nest one level per await.
		β CompletesInline()Ι->bool{ return false; }
		β SetConfig( const jobject& config )ε->void=0;
		β AtCatalog( sv catalog, SRCE )ε->sp<IDataSource>;
		β AtSchema( sv schema, SRCE )ε->sp<IDataSource>;
		β ServerMeta()ι->IServerMeta& =0;

		Ŧ ScalerSync( Sql&& sql, SRCE )ε->T;
		Ŧ ScalerSyncOpt( Sql&& sql, SRCE )ε->optional<T>;
		Ŧ Scaler( Sql&& sql, SRCE )ι{ return ScalerAwait<T>{ shared_from_this(), move(sql), sl }; }
		Ŧ ScalerOpt( Sql&& sql, SRCE )ι{ return ScalerAwaitOpt<T>{ shared_from_this(), move(sql), sl }; }

		α Select( Sql&& s, SRCE )ε->vector<Row>;
		α Select( Sql&& s, RowΛ f, SRCE )ε->uint;
		α SelectAsync( Sql&& sql, SRCE )ι->SelectAwait{ return SelectAwait{ shared_from_this(), move(sql), sl }; }
		template<class K=uint,class V=string>
		α SelectEnum( const Table& table, optional<steady_clock::duration> duration=Cache::DefaultDuration(), SRCE )ε->CacheAwait<flat_map<K,V>>{ return SelectMap<K,V>( {Ƒ("select {}, name from {}", table.GetPK()->Name, table.SqlName())}, table.Name, duration, sl ); }
		ẗ SelectEnumSync( const Table& table, optional<steady_clock::duration> duration=Cache::DefaultDuration(), SRCE )ε->flat_map<K,V>{
			return BlockAwait<CacheAwait<flat_map<K,V>>,flat_map<K,V>>( SelectEnum<K,V>(table, duration, sl) );
		}

		ẗ SelectMap( Sql&& sql, string cacheName, optional<steady_clock::duration> duration=Cache::DefaultDuration(), SRCE )ι->CacheAwait<flat_map<K,V>>;

		[[nodiscard]] α Execute( Sql&& sql, SRCE )ε->ExecuteAwait{ return ExecuteAwait{shared_from_this(), move(sql), sl}; }
		α ExecuteSync( Sql&& sql, SRCE )ε->uint;
		//outValue: a non-Null one asks for the first column of the first row back.  On a *proc* (`Sql::IsProc`) it also
		//declares the trailing placeholder an OUT param - `call p(?,?,?)` where the last `?` is the sequence the proc
		//assigns - and both drivers strip it before binding.  On plain SQL nothing is stripped and the value is simply
		//the row the statement returned; the EValue itself is only ever tested against Null (#47).
		α ExecuteScalerSync( Sql&& sql, EValue outValue, SRCE )ε->DB::Value;
		Ŧ InsertSeq( DB::InsertClause&& sql, SRCE )ι{ return ScalerAwait<T>{ shared_from_this(), move(sql), sl }; }
		Ŧ InsertSeqSync( DB::InsertClause&& insert, SRCE )ε->T{ return static_cast<T>( InsertSeqSyncUInt(move(insert), sl) ); }
		β Query( Sql&& sql, bool outParams=false, SRCE )ε->QueryAwait=0;
		β Syntax()ι->const Syntax& = 0;

	protected:
		//C1: one statement-execution primitive per driver, and the five sync wrappers over it implemented once here.
		//They were line-for-line identical in all three drivers and had already drifted twice - odbc dropped `outValue`
		//in ExecuteScalerSync (binding the last param INPUT instead of OUTPUT), and MySQL ignored `Log` (#48).
		struct Params final{
			α HasOut()Ι->bool{ return OutValue!=EValue::Null; } //a proc's trailing placeholder is the OUT param - see ExecuteScalerSync and #47.
			RowΛ* Function{};
			EValue OutValue{ EValue::Null };
			bool Log{ true };
			bool Sequence{};
		};
		β Execute( Sql&& sql, SL sl, Params exeParams )ε->uint =0;
		β InsertSeqSyncUInt( DB::InsertClause&& insert, SL sl )ε->uint=0;
		optional<string> _catalog; //db catalog name ie jde
		string _schema;  //db schema name ie dbo
	};
#define let const auto
	Ŧ IDataSource::ScalerSyncOpt( Sql&& sql, SL sl )ε->optional<T>{
		optional<T> y;
		Select( move(sql), [&y](Row&& row){ y = row.GetOpt<T>(0); }, sl );
		return y;
	}
	Ŧ IDataSource::ScalerSync( Sql&& sql, SL sl )ε->T{
		auto y = ScalerSyncOpt<T>( move(sql), sl );
		THROW_IFSL( !y, "No value returned from scaler query." );
		return *y;
	}

	namespace zInternal{
		ẗ ProcessMapRow( flat_map<K,V>& y, Row&& row )ε{ y.emplace( row.Get<K>(0), row.Get<V>(1) ); }
	}

	ẗ IDataSource::SelectMap( Sql&& sql, string cacheName, optional<steady_clock::duration> duration, SL sl )ι->CacheAwait<flat_map<K,V>>{
		return CacheAwait<flat_map<K,V>>( shared_from_this(), move(sql), zInternal::ProcessMapRow<K,V>, move(cacheName), duration, sl );
	}
}
#undef let