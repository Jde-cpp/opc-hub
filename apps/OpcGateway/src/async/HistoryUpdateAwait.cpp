#include "HistoryUpdateAwait.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
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

	α HistoryUpdateResponse::Validate( Handle uahandle, SL sl )ε->void{
		THROW_IFX( responseHeader.serviceResult, UAClientException(responseHeader.serviceResult, uahandle, "HistoryUpdate", sl) );
		THROW_IFSL( resultsSize!=Requested, "HistoryUpdate answered {} of {} entries.", resultsSize, Requested );
	}

	α HistoryUpdateAwait::Convert( UA_HistoryUpdateResponse&& r )ε->HistoryUpdateResponse{
		HistoryUpdateResponse response{ move(r), _request.Size() };
		response.Validate( _client->Handle(), _sl );
		return response;
	}
}