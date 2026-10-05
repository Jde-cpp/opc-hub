#pragma once
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Variant.h>
DISABLE_WARNINGS
#include <jde/opc/proto/Opc.Common.pb.h>
ENABLE_WARNINGS

namespace Jde::Opc::ProtoUtils{
	α ToNodeId( const Proto::NodeId& proto )ι->NodeId;
	α ToNodeId( const UA_NodeId& id )ι->Proto::NodeId;
	α ToExNodeId( const Proto::ExpandedNodeId& proto )ι->ExNodeId;
	α ToExNodeId( const UA_ExpandedNodeId& id )ι->Proto::ExpandedNodeId;
	α ToNodeIds( google::protobuf::RepeatedPtrField<Proto::NodeId>&& proto )ι->flat_set<NodeId>;

	//A scalar is its oneof member, an array is `array`, and an empty variant sets nothing.  Aliases go by kind (UtcTime is
	//a date, an enum an int32), structures are binary-encoded into extension_object, and DataValue/DiagnosticInfo throw
	//BadNotSupported.
	α ToValue( const UA_Variant& v )ε->Proto::Value;
	α ToValue( const void* element, const UA_DataType& type )ε->Proto::Value;//one element, e.g. of an array.
	//Whether ToValue holds v, which it doesn't for a DataValue, a DiagnosticInfo, a BitfieldCluster, or a Variant holding
	//one:  a check that costs no exception.
	α Supported( const UA_Variant& v )ι->bool;
	//Whether the text ToValue puts in a proto3 `string` is UTF-8:  a String, XmlElement, name, locale, string NodeId or
	//namespace URI.  protobuf serializes one that isn't, logging only, and refuses to parse it back.
	α Utf8( const UA_Variant& v )ι->bool;
	α Utf8( const UA_ExpandedNodeId& id )ι->bool;
	α Utf8( sv text )ι->bool;
	//The inverse, up to the wire:  the result binary-encodes as the original did, while an alias comes back as its
	//built-in type and a structure as an encoded ExtensionObject.
	α ToVariant( const Proto::Value& v )ε->Variant;
}
