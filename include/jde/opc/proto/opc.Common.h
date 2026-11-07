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

	//How ToValue carries a structure.  Encoded:  the ExtensionObject it travels in a Variant as, the type's DefaultBinary id and its
	//binary form, which the historian stores (libs/historian/docs/spec.md *Values*), whole whether or not anything here knows its type.
	//Decoded:  as `structure`, its members by name in the shape each kind takes here (clients spec *Structures*) - open62541's own,
	//and a server's own by `types` - a decoded value by the type it holds and an encoded body by its encoding id, one no type
	//explains kept as extension_object and noted with `types` (ITypes::Missing).  The socket's pushes and pages go Decoded.
	enum class EStructures : uint8{ Encoded, Decoded };
	//A scalar is its oneof member, an array is `array`, and an empty variant sets nothing.  Aliases go by kind (UtcTime is
	//a date, an enum an int32), a structure goes as `structures` says, and DataValue/DiagnosticInfo throw BadNotSupported.  Decoded,
	//text that isn't UTF-8 throws BadEncodingError, and a structure with a member either refuses goes as its body, as Encoded sends it.
	α ToValue( const UA_Variant& v, EStructures structures=EStructures::Encoded, const ITypes* types=nullptr )ε->Proto::Value;
	α ToValue( const void* element, const UA_DataType& type, EStructures structures=EStructures::Encoded, const ITypes* types=nullptr )ε->Proto::Value;//one element, e.g. of an array.
	//Every extension_object in `v` - the value, or an element of its array - whose body a type explains, as its `structure`:  a
	//stored value, Encoded, onto the socket, decoded by the connection's registry as it stands (clients spec *Structures*).
	α DecodeStructures( Proto::Value& v, const ITypes* types )ι->void;
	//Whether ToValue holds v, which it doesn't for a DataValue, a DiagnosticInfo, a BitfieldCluster, or a Variant holding
	//one:  a check that costs no exception.
	α Supported( const UA_Variant& v )ι->bool;
	//Whether the text ToValue, Encoded, puts in a proto3 `string` is UTF-8:  a String, XmlElement, name, locale, string NodeId
	//or namespace URI.  protobuf serializes one that isn't, logging only, and refuses to parse it back.
	α Utf8( const UA_Variant& v )ι->bool;
	α Utf8( const UA_ExpandedNodeId& id )ι->bool;
	α Utf8( sv text )ι->bool;
	//The inverse, up to the wire:  the result binary-encodes as the original did, while an alias comes back as its built-in
	//type and an extension_object as an encoded ExtensionObject.  A `structure` is rebuilt member by member (FillMembers) as
	//the type its id names, UA_TYPES' or `types`' chain's, and refused, BadDecodingError, when neither explains it.
	α ToVariant( const Proto::Value& v, const ITypes* types=nullptr )ε->Variant;
}