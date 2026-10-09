#include "HistoryUpdateAwait.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	HistoryUpdateRequest::HistoryUpdateRequest( HistoryUpdateRequest&& x )ι:
		UA_HistoryUpdateRequest{ x }{
		UA_HistoryUpdateRequest_init( &x );
	}
	α HistoryUpdateRequest::operator=( HistoryUpdateRequest&& x )ι->HistoryUpdateRequest&{
		if( this!=&x ){
			UA_HistoryUpdateRequest_clear( this );
			*( UA_HistoryUpdateRequest* )this = x;
			UA_HistoryUpdateRequest_init( &x );
		}
		return *this;
	}
	α HistoryUpdateRequest::Add( void* details, const UA_DataType& type )ε->void{
		UA_ExtensionObject entry; UA_ExtensionObject_init( &entry );
		UA_ExtensionObject_setValue( &entry, details, &type );//owned:  the request's clear frees it.
		if( let sc = UA_Array_append((void**)&historyUpdateDetails, &historyUpdateDetailsSize, &entry, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT]); sc ){
			UA_ExtensionObject_clear( &entry );
			throw UAException{ sc };
		}
	}
	α HistoryUpdateRequest::Update( const UA_NodeId& node, UA_PerformUpdateType type, vector<Value>&& values )ε->void{
		auto details = UA_UpdateDataDetails_new();
		UA_NodeId_copy( &node, &details->nodeId );
		details->performInsertReplace = type;
		details->updateValues = (UA_DataValue*)UA_Array_new( values.size(), &UA_TYPES[UA_TYPES_DATAVALUE] );
		details->updateValuesSize = values.size();
		for( uint i=0; i<values.size(); ++i ){
			details->updateValues[i] = values[i];//moved:  values[i] lets go of what it pointed at.
			UA_DataValue_init( &values[i] );
		}
		Add( details, UA_TYPES[UA_TYPES_UPDATEDATADETAILS] );
	}
	α HistoryUpdateRequest::DeleteRaw( const UA_NodeId& node, UA_DateTime start, UA_DateTime end )ε->void{
		auto details = UA_DeleteRawModifiedDetails_new();
		UA_NodeId_copy( &node, &details->nodeId );
		details->isDeleteModified = false;//the modifications are the server's audit trail, as a group's are.
		details->startTime = start;
		details->endTime = end;
		Add( details, UA_TYPES[UA_TYPES_DELETERAWMODIFIEDDETAILS] );
	}
	α HistoryUpdateRequest::DeleteAtTime( const UA_NodeId& node, const vector<UA_DateTime>& times )ε->void{
		auto details = UA_DeleteAtTimeDetails_new();
		UA_NodeId_copy( &node, &details->nodeId );
		if( let sc = UA_Array_copy(times.data(), times.size(), (void**)&details->reqTimes, &UA_TYPES[UA_TYPES_DATETIME]); sc ){
			UA_DeleteAtTimeDetails_delete( details );
			throw UAException{ sc };
		}
		details->reqTimesSize = times.size();
		Add( details, UA_TYPES[UA_TYPES_DELETEATTIMEDETAILS] );
	}

	HistoryUpdateResponse::HistoryUpdateResponse( UA_HistoryUpdateResponse&& x, uint requested )ι:
		UA_HistoryUpdateResponse{ x }, Requested{ requested }{
		UA_HistoryUpdateResponse_init( &x );
	}
	HistoryUpdateResponse::HistoryUpdateResponse( HistoryUpdateResponse&& x )ι:
		UA_HistoryUpdateResponse{ x }, Requested{ x.Requested }{
		UA_HistoryUpdateResponse_init( &x );
	}
	α HistoryUpdateResponse::operator=( HistoryUpdateResponse&& x )ι->HistoryUpdateResponse&{
		if( this!=&x ){
			UA_HistoryUpdateResponse_clear( this );
			*( UA_HistoryUpdateResponse* )this = x;
			Requested = x.Requested;
			UA_HistoryUpdateResponse_init( &x );
		}
		return *this;
	}
	α HistoryUpdateResponse::Validate( Handle uahandle, SL sl )ε->void{
		THROW_IFX( responseHeader.serviceResult, UAClientException(responseHeader.serviceResult, uahandle, "HistoryUpdate", sl) );
		THROW_IFSL( resultsSize!=Requested, "HistoryUpdate answered {} of {} entries.", resultsSize, Requested );
	}

	α HistoryUpdateAwait::Suspend()ι->void{
		_client->PostUA( [this]{//UA submissions must run on the client's strand; `this` outlives suspension (resume only via OnComplete).
			try{
				UACε( __UA_Client_AsyncService(*_client, &_request, &UA_TYPES[UA_TYPES_HISTORYUPDATEREQUEST], OnResponse, &UA_TYPES[UA_TYPES_HISTORYUPDATERESPONSE], this, &_requestId) );
				_client->Process( _requestId, "historyUpdate" );
			}
			catch( UAException& e ){
				ResumeExp( move(e) );
			}
		});
	}
	α HistoryUpdateAwait::OnResponse( UA_Client* /*client*/, void* await, UA_UInt32 /*requestId*/, void* response )ι->void{
		ASSERT( await );
		static_cast<HistoryUpdateAwait*>( await )->OnComplete( static_cast<UA_HistoryUpdateResponse*>(response) );
	}
	α HistoryUpdateAwait::OnComplete( UA_HistoryUpdateResponse* r )ι->void{
		_client->ClearRequest( _requestId );
		HistoryUpdateResponse response{ move(*r), _request.Size() };
		try{
			response.Validate( _client->Handle(), _sl );
			Resume( move(response) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}