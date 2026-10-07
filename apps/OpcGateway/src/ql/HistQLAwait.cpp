#include "HistQLAwait.h"
#include <jde/fwk/str.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	α HistQLAwait::Execute()ι->TAwait<HistoryReadResponse>::Task{
		try{
			_args.emplace( _query, _sl );
			_nodes.resize( _args->Nodes.size() );
			for( bool first=true;; first=false ){
				auto response = co_await HistoryReadAwait{ Request(first, false), _client, _sl };
				for( uint i=0; i<_asked.size(); ++i )
					Take( _asked[i], response.Result(i) );
				if( Emittable()>=_args->Limit || !std::ranges::any_of(_nodes, &Node::More) )
					break;
			}
			auto page = Page();
			let continuation = Continuation( page );
			if( std::ranges::any_of(_nodes, &Node::More) ){
				try{
					co_await HistoryReadAwait{ Request(false, true), _client, _sl };
				}
				catch( const std::exception& e ){//the server's to drop with the session, then.
					DBG( "[{}]Releasing the history continuation points failed:  {}", hex(_client->Handle()), e.what() );
				}
			}
			vector<StatusCode> statuses; statuses.reserve( _nodes.size() );
			for( let& node : _nodes )
				statuses.push_back( node.Status );
			Resume( HistQL::ToJson(_query, _args->Nodes, move(page), continuation, statuses) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α HistQLAwait::Reach()Ι->Horizon{
		Horizon y;
		for( let& node : _nodes ){
			if( !node.More )
				continue;
			y.Open = false;
			if( !node.Last )
				y.Blocked = true;
			else if( !y.Time || Before(*node.Last, *y.Time) )
				y.Time = node.Last;
		}
		return y;
	}

	α HistQLAwait::Request( bool first, bool release )ι->HistoryReadRequest{
		UA_ReadRawModifiedDetails details; UA_ReadRawModifiedDetails_init( &details );
		details.isReadModified = _args->Modified;
		details.returnBounds = _args->Bounds;
		details.numValuesPerNode = (UA_UInt32)std::min<uint>( _args->Limit, std::numeric_limits<UA_UInt32>::max() );
		if( let resume = ResumeAt() ){//from the last time returned, inclusive:  the counts pass over what the pages before returned there.
			details.startTime = *resume;
			//To the end the read goes toward.  An end alone, read backward from it, resumes bounded at the first tick
			//after 1601:  a startTime alone would read forward from the resume time.
			details.endTime = _args->Start && _args->End ? *_args->End : _args->Reverse() ? 1 : 0;
		}
		else{
			details.startTime = _args->Start.value_or( 0 );
			details.endTime = _args->End.value_or( 0 );
		}
		_instant = details.startTime && details.startTime==details.endTime;
		HistoryReadRequest y{ details, UA_TIMESTAMPSTORETURN_BOTH, release };
		let horizon = Reach();
		_asked.clear();
		for( uint i=0; i<_nodes.size(); ++i ){
			let& node = _nodes[i];
			//Among the nodes the server has more for, the ones holding the merge back:  those that returned nothing yet,
			//else those at the horizon.
			let ask = first || ( node.More && (release || (horizon.Blocked ? !node.Last : node.Last==horizon.Time)) );
			if( ask ){
				y.Add( _args->Nodes[i], node.Point );
				_asked.push_back( i );
			}
		}
		return y;
	}

	α HistQLAwait::Take( uint slot, UA_HistoryReadResult& result )ι->void{
		auto& node = _nodes[slot];
		node.Status = result.statusCode;
		node.Point.assign( (const char*)result.continuationPoint.data, result.continuationPoint.length );
		node.More = node.Point.size() && !UA_StatusCode_isBad( result.statusCode );
		if( UA_StatusCode_isBad(result.statusCode) ){//the node's answer, as the server gave it.
			DBG( "[{}]hist {} answered {}.", hex(_client->Handle()), _args->Nodes[slot].ToString(), UAException::Message(result.statusCode) );
			return;
		}
		UA_DataValue* values{}; size_t count{};
		const UA_ModificationInfo* modifications{}; size_t modificationCount{};
		if( result.historyData.encoding==UA_EXTENSIONOBJECT_DECODED || result.historyData.encoding==UA_EXTENSIONOBJECT_DECODED_NODELETE ){
			let type = result.historyData.content.decoded.type;
			let data = result.historyData.content.decoded.data;
			if( type==&UA_TYPES[UA_TYPES_HISTORYDATA] ){
				let& history = *static_cast<const UA_HistoryData*>( data );
				values = history.dataValues; count = history.dataValuesSize;
			}
			else if( type==&UA_TYPES[UA_TYPES_HISTORYMODIFIEDDATA] ){
				let& history = *static_cast<const UA_HistoryModifiedData*>( data );
				values = history.dataValues; count = history.dataValuesSize;
				modifications = history.modificationInfos; modificationCount = history.modificationInfosSize;
			}
		}
		let resume = ResumeAt();
		let reversed = _instant && _args->Reverse();//one instant's records come forward:  the pages before took them from the end.
		uint pushed{};
		for( uint k=0; k<count; ++k ){
			let i = reversed ? count-1-k : k;
			Held held{ {slot, Value{move(values[i])}} };
			if( modifications && i<modificationCount )
				held.Value.Modified = HistQL::Modification{ modifications[i].modificationTime, modifications[i].updateType, ToString(modifications[i].userName) };
			let time = Time( held.Value.Data );
			if( !node.Opened ){//the node's first value this call:  the bound at the end the read starts from, and the resume.
				node.Opened = true;
				node.Skip = resume ? _args->Continuation->counts( slot ) : 0;
				if( _args->Bounds ){
					if( !resume )
						held.Opening = held.Value.Bound = true;
					//At the resume time, a record there is the node's first at it, which the pages before had or not as
					//the counts say.  Any other is a record before it, which a page before returned, or none.
					else if( time!=*resume || NotFound(held.Value.Data) )
						continue;
				}
			}
			if( node.Skip && resume && time==*resume ){
				--node.Skip;
				continue;
			}
			node.Last = time;
			node.Pending.push_back( move(held) );
			++pushed;
		}
		//The closing bound, the node's at the end the read goes toward, ends its last page.  An open end has none.
		if( !node.More && pushed && _args->Bounds && _args->Start && _args->End ){
			auto& closing = node.Pending.back();
			closing.Closing = closing.Value.Bound = true;
		}
	}

	α HistQLAwait::Earlier( const Held& a, uint slotA, const Held& b, uint slotB )Ι->bool{
		let timeA = Time( a.Value.Data ), timeB = Time( b.Value.Data );
		if( timeA!=timeB )
			return Before( timeA, timeB );
		return a.Closing!=b.Closing ? b.Closing : slotA<slotB;
	}

	α HistQLAwait::Emittable()Ι->uint{
		let horizon = Reach();
		if( horizon.Blocked )
			return 0;
		uint y{};
		for( let& node : _nodes ){
			for( let& held : node.Pending ){
				if( horizon.Open || (!held.Closing && !Before(*horizon.Time, Time(held.Value.Data))) )
					++y;
			}
		}
		return y;
	}

	α HistQLAwait::Page()ι->vector<HistQL::ReadValue>{
		vector<HistQL::ReadValue> y;
		let horizon = Reach();
		if( horizon.Blocked )
			return y;
		vector<uint> at( _nodes.size() );//each node's next pending value.
		for( ;; ){
			optional<uint> pick;
			for( uint i=0; i<_nodes.size(); ++i ){
				if( at[i]<_nodes[i].Pending.size() && (!pick || Earlier(_nodes[i].Pending[at[i]], i, _nodes[*pick].Pending[at[*pick]], *pick)) )
					pick = i;
			}
			if( !pick )
				break;
			auto& held = _nodes[*pick].Pending[at[*pick]];
			if( !horizon.Open && (held.Closing || Before(*horizon.Time, Time(held.Value.Data))) )
				break;//past the horizon, or a closing bound while a node has more:  the next page reads it again.
			if( y.size()>=_args->Limit && !held.Opening && !held.Closing )
				break;
			y.push_back( move(held.Value) );
			++at[*pick];
		}
		for( uint i=0; i<_nodes.size(); ++i )
			_nodes[i].Pending.erase( _nodes[i].Pending.begin(), _nodes[i].Pending.begin()+at[i] );
		return y;
	}

	α HistQLAwait::Continuation( const vector<HistQL::ReadValue>& page )Ι->string{
		let more = std::ranges::any_of( _nodes, []( let& node ){ return node.More || node.Pending.size(); } );
		if( !more || page.empty() )
			return {};
		let time = Time( page.back().Data );
		vector<uint32_t> counts( _nodes.size() );
		for( auto p=page.rbegin(); p!=page.rend() && Time(p->Data)==time; ++p ){
			if( !NotFound(p->Data) )
				++counts[p->Slot];
		}
		Hist::Proto::Continuation next;
		next.set_crc( _args->Crc() );
		next.set_time( time );
		let resume = ResumeAt();
		for( uint i=0; i<counts.size(); ++i )
			next.add_counts( counts[i]+( resume && *resume==time ? _args->Continuation->counts(i) : 0 ) );
		return HistQL::Encode( next );
	}
}