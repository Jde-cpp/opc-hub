#include <jde/ql/LocalSubscriptions.h>
#include <absl/synchronization/mutex.h>
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Sql.h>
#include <jde/db/generators/WhereClause.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/db/names.h>
#include <jde/ql/types/MutationQL.h>
#include <jde/ql/types/Parser.h>

#define let const auto
namespace Jde::QL{
	constexpr ELogTags _tags{ ELogTags::QL };
	struct TableOp final{
		TableOp( string tableName, EMutationQL type )ι: TableName{ move(tableName) }, Type{ type }{}
		string TableName;
		EMutationQL Type;
		α operator<( const TableOp& rhs )Ι->bool{
			return Type==rhs.Type ? TableName<rhs.TableName : Type<rhs.Type;
		}
	};
	struct ListenerSubs final{
		ListenerSubs( SubscriptionId id, TableQL&& fields, sp<IListener> listener )ι: Id{ id }, Fields{ move(fields) }, Listener{ listener }{}
		SubscriptionId Id;
		TableQL Fields;
		sp<IListener> Listener;
	};
	absl::Mutex _serverMutex;
	flat_map<TableOp,vector<ListenerSubs>> _serverSubs ABSL_GUARDED_BY(_serverMutex);

	//A mutation result only carries the row's id when the insert went through a proc; otherwise recover it so subscribers see the
	//same shape.  every failure here used to escape to OnMutation's catch _before_ the first
	//OnChange, so one awkward column cost *every* subscriber the notification.  Now a failed lookup costs the id field alone.
	Ω findId( const MutationQL& m, const jobject& args )ι->optional<uint>{
		let table = m.DBTable;
		let pk = table ? table->FindPK() : sp<DB::Column>{};
		if( !pk )
			return {};
		DB::WhereClause where; //the generator, not hand-rolled sql:  placeholders & qualified names then follow the driver's Syntax.
		vector<sp<DB::Column>> nullColumns; //held back: `col is null` matches nothing when the column was server-defaulted (created/updated), so only narrow with them when the plain lookup is ambiguous.
		for( let& [key, value] : args ){
			if( value.is_object() || value.is_array() )
				continue;
			let column = table->FindColumn( DB::Names::FromJson(key) );
			if( !column || column==pk )
				continue;
			if( value.is_null() ){
				nullColumns.push_back( column );
				continue;
			}
			try{
				where.Add( column, DB::Value{column->Type, value} ); //Value{EType,jvalue} throws for Guid/VarBinary/TimeSpan/… and on json-kind mismatches - such a column just doesn't narrow the search.
			}
			catch( const std::exception& ){
				TRACE( "[{}.{}]'{}' does not convert to the column type - not narrowing the id lookup with it.", table->Name, column->Name, serialize(value) );
			}
		}
		if( where.Empty() )
			return {};
		optional<uint> y;
		try{
			auto select = [&]()ε{
				vector<uint> ids;
				table->Schema->DS()->Select( DB::Sql{Ƒ("select {} from {} {}", pk->FQName(), table->SqlName(), where.ToString()), where.Params()}, [&ids](DB::Row&& r){ ids.push_back( r.Get<uint>(0) ); } );
				return ids;
			};
			auto ids = select();
			if( ids.size()>1 && nullColumns.size() ){ //eg restoreResource(slug:x, criteria:null): the mutation's own `is null` predicates pick between rows differing only in the null column.
				for( let& column : nullColumns )
					where.Add( column, DB::Value{} );
				ids = select();
			}
			if( ids.size()==1 )
				y = ids.front();
			else //0 rows was ScalerSync's throw; 2+ used to broadcast whichever row came last.
				WARN( "[{}]The id lookup matched {} rows for '{}' - notifying subscribers without an id.", table->Name, ids.size(), m.CommandName );
		}
		catch( const std::exception& e ){
			WARN( "[{}]The id lookup failed for '{}' ({}) - notifying subscribers without an id.", table->Name, m.CommandName, e.what() );
		}
		return y;
	}

	//#53: the subscription's own arguments are a predicate, and the generic fan-out never evaluated one - only the log path did
	//(SubscribeLog::Write, column by column).  So `resourcesCreated(schemaName:"opc")` was delivered every createResource, and
	//the args the parser keeps were projection-only decoration.  A column the mutation says nothing about is not a mismatch, it
	//is unknowable, and is delivered as before;  paging keys are not predicates.
	Ω toUnderlying( const jvalue& v )ι->optional<DB::Value::Underlying>{
		using enum boost::json::kind;
		switch( v.kind() ){
		case string: return DB::Value::Underlying{ std::string{v.get_string()} };
		case int64: return DB::Value::Underlying{ (_int)v.get_int64() };
		case uint64: return DB::Value::Underlying{ (uint)v.get_uint64() };
		case double_: return DB::Value::Underlying{ v.get_double() };
		case bool_: return DB::Value::Underlying{ v.get_bool() };
		case null: return DB::Value::Underlying{ nullptr };
		default: return {};//an object or array in the payload is not a scalar to compare against.
		}
	}
	Ω passesFilter( const TableQL& fields, const jobject& available )ι->bool{
		for( let& [name,filters] : fields.Filter().ColumnFilters ){
			if( name=="orderBy" || name=="limit" || name=="offset" || name=="skip" )
				continue;//paging, not a predicate.  subscriptionId is consumed by Subscription's constructor.
			let p = available.if_contains( name );
			if( !p )
				continue;
			if( let value = toUnderlying(*p); value && !Filter::Test(*value, filters, _tags) )
				return false;
		}
		return true;
	}

	//comes from a mutation.
	α Subscriptions::OnMutation( const MutationQL& m, jvalue result )ι->void{
		OnMutation( m, move(result), nullptr );
	}
	α Subscriptions::OnMutation( const MutationQL& m, jvalue result, function<bool(QL::TableQL&)> isApplicable )ι->void{
		try{
			vector<ListenerSubs> matches;
			{
				rl _{ _serverMutex };
				auto subs = _serverSubs.find( {m.TableName(), m.Type} );
				if( subs==_serverSubs.end() )
					return;//everything is pushed.
				matches = subs->second;
			}//callbacks & db access below happen outside the lock - listeners can (un)subscribe from OnChange.
			jobject available;
			for( auto& sub : matches ){
				if( isApplicable && !isApplicable(sub.Fields) )
					continue;
				if( available.empty() ){
					if( let array = result.try_as_array(); array && array->size() )
						result = ( *array )[0];
					let args = m.ExtrapolateVariables();
					available = result.is_object() ? Json::Combine( result.get_object(), args ) : args;
					//match every scalar arg that maps to a column - slug alone is ambiguous (e.g. resources rows differing only by criteria).
					//Args only: the result's fields are server-formatted (a `$now` default needn't byte-match what was written) and today carry nothing but id/rowCount anyway.
					if( !available.contains("id") ){
						if( let id = findId(m, args); id )
							available["id"] = *id;
					}
				}
				if( !passesFilter(sub.Fields, available) )
					continue;//#53: the subscriber asked for a subset of these events.
				jobject j;
				auto value = sub.Fields.TrimColumns( available );
				j[sub.Fields.JsonName] = move( value );
				try{
					sub.Listener->OnChange( j, sub.Id );
				}
				catch( const runtime_error& e ){
					WARN( "[{}]{}.{} listener threw on its notification: {}", sub.Listener->Name, m.TableName(), ToString(m.Type), e.what() );
				}
			}
		}
		catch( const runtime_error& e ){
			ERR( "[{}]{} fan-out failed before its subscribers were notified: {}", m.TableName(), ToString(m.Type), e.what() );
		}
	}

	α Subscriptions::StopListen( sp<IListener> listener, vector<SubscriptionId> ids, SL sl )ι->jarray{
		jarray y;
		ul _{ _serverMutex };
		for( auto tableOp = _serverSubs.begin(); tableOp!=_serverSubs.end(); ){ //TableOp,vector<ListenerSubs>
			auto&& listenerSubs = tableOp->second;
			for( auto listenerSub = listenerSubs.begin(); listenerSub!=listenerSubs.end(); ){
				if( listenerSub->Listener==listener && (ids.empty() || find(ids, listenerSub->Id)!=ids.end()) ){
					y.push_back( listenerSub->Id );
					listenerSub = listenerSubs.erase( listenerSub );
				}else
					++listenerSub;
			}
			tableOp = listenerSubs.empty() ? _serverSubs.erase( tableOp ) : next( tableOp );
		}
		TRACESL( "[{}]StopListen: {}", listener ? listener->Name : string{"null"}, serialize(y) );
		return y;
	}
	//#9: a subscription is a standing read of the table - the same rows the client's query would return, delivered as they
	//change - so it takes the same Read.  Nothing on the subscribe path checked anything before this:  the ops are the only
	//Authorize sites in ql, and a notification never goes through one.  Read rather than ERights::Subscribe because no acl
	//grants Subscribe today, so requiring it would refuse every existing subscriber.
	Ω authorize( const TableQL& table, UserPK executer, SL sl )ε->void{
		if( let dbTable = table.DBTable(); dbTable )
			dbTable->Authorize( Access::ERights::Read, executer, sl );
		for( let& t : table.Tables )
			authorize( t, executer, sl );
	}
	α Subscriptions::Listen( sp<IListener> listener, vector<Subscription>&& subs, UserPK executer, SL sl )ε->void{
		if( executer.Value!=UserPK::System ){
			for( let& s : subs )
				authorize( s.Fields, executer, sl );
		}
		Listen( listener, move(subs) );
	}
	α Subscriptions::Listen( sp<IListener> listener, vector<Subscription>&& subs )ι->void{
		ul _{ _serverMutex };
		for( auto&& s : subs ){
			TRACE( "[{}]Listen:  '{}'.'{}'", listener->Name, s.TableName, ToString(s.Type) );
			_serverSubs.try_emplace( {move(s.TableName), s.Type} ).first->second.emplace_back( ListenerSubs{s.Id,move(s.Fields), listener} );
		}
	}
}