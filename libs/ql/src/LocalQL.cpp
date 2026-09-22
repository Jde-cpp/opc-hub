#include <jde/ql/LocalQL.h>
#include <jde/db/Row.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/ql/ql.h>
#include <jde/ql/LocalSubscriptions.h>
#include "ops/InsertAwait.h"

#define let const auto

namespace Jde::QL{
	α LocalQL::DS()ι->DB::IDataSource&{ ASSERT(!_schemas.empty() && _schemas.front()->DS()); return *_schemas.front()->DS(); }
	α LocalQL::GetTablePtr( str tableName, SL sl )ε->sp<DB::Table>{
		for( const auto& schema : _schemas ){
			if( auto t = schema->FindView(tableName); t )
				return t;
		}
		THROWSL( "Table not found:  {}", tableName );
	}
	α LocalQL::GetTable( str tableName, SL sl )ε->DB::Table&{
		return *GetTablePtr( tableName, sl );
	}

	//Always finished in await_ready, so it parks its own result:  IQL::Subscribe's contract is a up<TAwait<…>>, and that family
	//cannot pre-complete with a value (the promise is the mailbox) - the one place ql still parks one by hand.
	struct SubscribeQueryAwait : TAwait<vector<SubscriptionId>>{
		using Await = TAwait<jobject>;
		using base = TAwait<vector<SubscriptionId>>;
		SubscribeQueryAwait( vector<Subscription>&& sub, sp<IListener> listener, UserPK executer, SRCE )ι:
			base{sl}, _executer{executer}, _listener{listener}, _subscriptions{move(sub)}{}
		α await_ready()ι->bool override{
			try{
				for_each( _subscriptions, [&]( Subscription& sub ){
					if( !sub.Id )
						sub.Id = Subscription::NextId();
					_result.push_back( sub.Id );
				} );
				Subscriptions::Listen( _listener, move(_subscriptions), _executer, _sl );
			}
			catch( Exception& e ){
				_exception = e.Move();
			}
			return true;
		}
		α Suspend()ι->void override{}
		α await_resume()ε->vector<SubscriptionId> override{
			if( _exception )
				_exception->Throw();//not `throw move(*_exception)`, which slices to the base (#28).
			return _result;
		}
	private:
		up<Exception> _exception;
		UserPK _executer;
		sp<IListener> _listener;
		vector<Subscription> _subscriptions;
		vector<SubscriptionId> _result;
	};
	α LocalQL::Subscribe( string&& query, jobject variables, sp<IListener> listener, UserPK executer, SL sl )ε->up<TAwait<vector<SubscriptionId>>>{
		return mu<SubscribeQueryAwait>( ParseSubscriptions(move(query), variables, _schemas, sl), listener, executer, sl );
	}
	α LocalQL::Upsert( string query, jobject variables, UserPK executer )ε->jarray{
		auto result = QL::Parse( move(query), variables, _schemas ); THROW_IF( !result.IsMutation(), "Query is not a mutation" );
		jarray y;
		for( auto&& m : result.Mutations() ){
			if( m.Type==EMutationQL::Add ){//a membership or a permission, not a keyed row:  the key names the role it goes ON, which exists - so the create-if-missing rule below would skip every add.  It runs, as add-if-missing (RoleMAwait skips a member the role holds, and with AddIfMissing a permission on a resource the role already has one on) - and through this IQL, whose CustomMutation is what answers addRole; the create branch's QLAwait carries none and would fall to the stock add.
				m.AddIfMissing = true;//a seed adds what is missing and rewrites nothing:  the admin's edit to a seeded grant wins (reviews/m3-closing.md #12)
				vector<MutationQL> one; one.push_back( move(m) );
				y.push_back( BlockAwait<QLAwait<jvalue>,jvalue>(QLAwait<jvalue>{RequestQL{move(one)}, Creds{executer}, shared_from_this()}) );
				continue;
			}
			auto key = m.FindKey();
			if( !key ){
				auto shift = m.TryNumber<uint8>( "shift" );
				key = { shift ? 1ul << *shift : 0 };
				m.Args["id"] = key->PK();
				m.Args.erase( "shift" );
			}
			auto input = key->IsPK()
				? "id:"+std::to_string(key->PK())
				: "slug:\""+move(key->NK())+'"';
			//a soft-deleted row is still a row - its keys keep their unique indexes - so it has to count as existing:  naming deleted
			//drops the select's `deleted is null`.  Only where the column is, or the select throws (rights, providerTypes, logLevels).
			//Without it a seeded role an admin deleted was re-created at the next -sync start and died on the index (reviews/m3-closing.md #1).
			let columns = m.DBTable && m.DBTable->FindColumn("deleted") ? "id deleted" : "id";
			auto ql = Ƒ( "{}({}){{ {} }}", DB::Names::ToSingular(m.JsonTableName), move(input), columns );
			if( auto existing = BlockAwait<TAwait<jobject>,jobject>(move(*QueryObject(move(ql), variables, executer))); existing.empty() ){
				if( auto name = m.Args.contains("name") ? nullptr : m.Args.if_contains("slug"); name )
					m.Args["name"] = Json::AsString( *name );
				if( auto t = key->IsPK() ? GetTablePtr(m.TableName()) : nullptr; t && t->SequenceColumn() )
					y.push_back( BlockAny<InsertAwait>({t, move(m), executer, true}) );
				else
					y.push_back( BlockAwait<QLAwait<jvalue>,jvalue>(QLAwait<jvalue>{move(m), executer}) );
			}else
				y.push_back( {} );
		}
		return y;
	}
}