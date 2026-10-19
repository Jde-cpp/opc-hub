#include "HistGroupEditQLAwait.h"
#include "../hist/HistHost.h"
#include "HistQL.h"

#define let const auto
namespace Jde::Opc::Gateway{
	α HistGroupEditQLAwait::IsApplicable( const QL::MutationQL& m )ι->bool{
		return HistQL::FindEdit( m.CommandName ) && HistQL::Target( m )==HistQL::ETarget::Group;
	}
	α HistGroupEditQLAwait::Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>{
		return IsApplicable( m ) ? mu<HistGroupEditQLAwait>( move(m), move(executer), sl ) : nullptr;
	}

	α HistGroupEditQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			using enum HistQL::EEdit;
			let edit = *HistQL::FindEdit( _query.CommandName );
			const HistQL::EditArgs args{ edit, _query, _sl };
			auto& hist = HistHost::Instance();
			let host = hist.Gate( args.Group, edit==Purge ? Access::ERights::Delete : Access::ERights::Update, true, UserPK(), _sl );
			let& group = host->Group();
			let indexes = host->Indexes( args.Nodes, _sl );
			let count = indexes.size();
			vector<StatusCode> statuses( count, UA_STATUSCODE_GOOD );
			vector<optional<uint>> entries( count );//each node's entry in the edit, none for one that isn't a member.
			vector<Hist::EditDetails> details;
			if( edit<Purge ){
				vector<vector<Value>> values( count );//each node's, in the order named.
				vector<string> names( count ); vector<const UA_DataType*> types( count );//each member's, once for all its values.
				for( uint i=0; i<count; ++i ){
					if( indexes[i] ){
						names[i] = args.Nodes[i].ToString();
						types[i] = group->DataType( *indexes[i] );
					}
				}
				for( let& v : args.Values ){
					if( indexes[v.Slot] )
						values[v.Slot].push_back( HistQL::ToValue(v, names[v.Slot], types[v.Slot], args.Command(), _sl) );
				}
				for( uint i=0; i<count; ++i ){
					if( indexes[i] ){
						entries[i] = details.size();
						details.push_back( Hist::UpdateData{*indexes[i], HistQL::PerformType(edit), move(values[i])} );
					}
				}
			}
			else{
				for( uint i=0; i<count; ++i ){
					if( !indexes[i] )
						continue;
					entries[i] = details.size();
					if( args.Times.empty() )
						details.push_back( Hist::DeleteRaw{*indexes[i], args.Start, args.End} );
					else
						details.push_back( Hist::DeleteAtTime{*indexes[i], args.Times} );
				}
			}
			vector<Hist::EditResult> results;
			if( details.size() )
				results = co_await group->Edit( move(details), hist.Writer(UserPK()), _sl );
			vector<std::span<const StatusCode>> operations( count );
			for( uint i=0; i<count; ++i ){
				if( !entries[i] ){
					statuses[i] = UA_STATUSCODE_BADNODEIDUNKNOWN;//not a member.
					continue;
				}
				let& result = results.at( *entries[i] );
				statuses[i] = result.Status;
				operations[i] = result.Results;
			}
			Resume( _query.ResultRequest ? HistQL::ToJson(*_query.ResultRequest, HistQL::NodesJson(args.Nodes), HistQL::EditRows(args, operations, statuses), statuses) : jvalue{} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}