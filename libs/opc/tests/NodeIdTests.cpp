//NodeId: ownership, ordering, and the three round trips the review asks for - ToJson->FromJson, ToString->DecodeJson
//and InsertParams (the shape the insert procs dispatch on) - across all four identifier kinds.
#include <concepts>
#include <gtest/gtest.h>
#include <absl/hash/hash.h>
#include <jde/db/Value.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/ql/types/TableQL.h>

#define let const auto

namespace Jde::Opc::Tests{
	Ω numeric( UA_UInt16 ns, UA_UInt32 id )ι->NodeId{ return NodeId{ ns, id }; }
	Ω fromJson( sv json )ε->NodeId{ return NodeId{ parse(json) }; }
	//guidNode is now redundant with fromJson - review2 #18 made the "g" form parseable - but it stays as the one path that
	//does not depend on the string parse.  bytesNode is the same for "b", which ByteStringJsonRoundTrip exercises.
	Ω guidNode( UA_UInt16 ns, const UA_Guid& guid )ι->NodeId{
		UA_NodeId ua{};
		ua.namespaceIndex = ns;
		ua.identifierType = UA_NODEIDTYPE_GUID;
		ua.identifier.guid = guid;
		return NodeId{ move(ua) };
	}
	Ω bytesNode( UA_UInt16 ns, const vector<uint8_t>& bytes )ι->NodeId{
		UA_NodeId ua{};
		ua.namespaceIndex = ns;
		ua.identifierType = UA_NODEIDTYPE_BYTESTRING;
		UA_ByteString_allocBuffer( &ua.identifier.byteString, bytes.size() );
		::memcpy( ua.identifier.byteString.data, bytes.data(), bytes.size() );
		return NodeId{ move(ua) };
	}
	constexpr UA_Guid nodeGuid{ 0x12345678, 0x1234, 0x5678, {0x12,0x34,0x56,0x78,0x12,0x34,0x56,0x78} };

	TEST( NodeIdTests, NumericIdentifier ){
		let n = numeric( 2, 5002 );
		EXPECT_TRUE( n.IsNumeric() );
		EXPECT_FALSE( n.IsString() );
		EXPECT_FALSE( n.IsGuid() );
		EXPECT_FALSE( n.IsBytes() );
		EXPECT_EQ( n.namespaceIndex, 2 );
		ASSERT_TRUE( n.Numeric() );
		EXPECT_EQ( *n.Numeric(), 5002u );
		EXPECT_FALSE( n.String() );
		EXPECT_FALSE( n.Guid() );
		EXPECT_FALSE( n.Bytes() );
		EXPECT_EQ( numeric(0, 7).namespaceIndex, 0 );
	}

	TEST( NodeIdTests, SystemNodes ){
		EXPECT_TRUE( NodeId::ObjectsFolder().IsSystem() );
		EXPECT_TRUE( numeric(0, 32750).IsSystem() );
		EXPECT_FALSE( numeric(0, 32751).IsSystem() );
		EXPECT_FALSE( numeric(2, 85).IsSystem() );  //a vendor namespace is never a system node...
		EXPECT_FALSE( fromJson(R"({"s":"tag"})").IsSystem() ); //...and neither is a non-numeric identifier.
	}

	//Every wrapper owns its identifier:  a copy allocates its own, a move leaves the source as the null node id.
	TEST( NodeIdTests, StringIdentifierOwnership ){
		let src = fromJson( R"({"ns":2,"s":"tag.one"})" );
		ASSERT_TRUE( src.IsString() );
		EXPECT_EQ( *src.String(), "tag.one" );

		NodeId copy{ src };
		EXPECT_EQ( *copy.String(), "tag.one" );
		EXPECT_NE( copy.identifier.string.data, src.identifier.string.data );

		NodeId source{ src };
		NodeId moved{ move(source) };
		EXPECT_EQ( *moved.String(), "tag.one" );
		EXPECT_TRUE( source.IsNumeric() ); //UA_NodeId_init leaves ns=0, i=0.
		EXPECT_EQ( *source.Numeric(), 0u );
		EXPECT_EQ( source.namespaceIndex, 0 );
	}

	TEST( NodeIdTests, Assignment ){
		NodeId a = fromJson( R"({"s":"a"})" );
		let b = fromJson( R"({"s":"b"})" );
		a = b;
		EXPECT_EQ( *a.String(), "b" );
		EXPECT_NE( a.identifier.string.data, b.identifier.string.data ); //a deep copy, not a shared buffer.

		NodeId c = fromJson( R"({"s":"c"})" );
		a = move( c );
		EXPECT_EQ( *a.String(), "c" );
		EXPECT_TRUE( c.IsNumeric() );

		auto& alias = a; //through a reference: `a = a` is -Wself-assign-overloaded, and the guard is what is under test.
		a = alias;
		EXPECT_EQ( *a.String(), "c" );
	}

	TEST( NodeIdTests, Ordering ){
		EXPECT_TRUE( numeric(0,1) < numeric(0,2) );
		EXPECT_FALSE( numeric(0,2) < numeric(0,1) );
		EXPECT_TRUE( numeric(0,5) < numeric(1,1) );  //namespace outranks the identifier.
		EXPECT_TRUE( numeric(0,5) == numeric(0,5) );
		EXPECT_FALSE( numeric(0,5) == numeric(1,5) );

		EXPECT_TRUE( fromJson(R"({"s":"a"})") < fromJson(R"({"s":"b"})") );
		EXPECT_TRUE( fromJson(R"({"s":"a"})") == fromJson(R"({"s":"a"})") );
		EXPECT_FALSE( numeric(0,1) == fromJson(R"({"s":"a"})") ); //identifierType breaks the tie.
		EXPECT_TRUE( numeric(0,1) < fromJson(R"({"s":"a"})") );   //NUMERIC(0) before STRING(3).
	}

	TEST( NodeIdTests, FromJsonForms ){
		EXPECT_EQ( *fromJson(R"({"ns":2,"i":5002})").Numeric(), 5002u );
		EXPECT_EQ( fromJson(R"({"ns":2,"i":5002})").namespaceIndex, 2 );
		EXPECT_EQ( *fromJson(R"({"s":"tag"})").String(), "tag" );
		EXPECT_EQ( fromJson(R"({"s":"tag"})").namespaceIndex, 0 );
		EXPECT_EQ( *fromJson("42").Numeric(), 42u );                        //a bare number is a numeric id in ns 0.
		EXPECT_EQ( *fromJson(R"("tag")").String(), "tag" );                 //a bare string is a string id in ns 0.
		EXPECT_EQ( *fromJson(R"({"ns":3,"id":{"i":7}})").Numeric(), 7u );   //a nested "id" inherits the outer namespace.
		EXPECT_EQ( fromJson(R"({"ns":3,"id":{"i":7}})").namespaceIndex, 3 );
		EXPECT_THROW( fromJson("true"), Exception );
	}

	TEST( NodeIdTests, GuidIdentifier ){
		let id = guidNode( 2, nodeGuid );
		ASSERT_TRUE( id.IsGuid() );
		ASSERT_TRUE( id.Guid() );
		EXPECT_EQ( Jde::ToString(*id.Guid()), "12345678-1234-5678-1234-567812345678" );
		EXPECT_EQ( id.ToJson().at("g").as_string(), "12345678-1234-5678-1234-567812345678" );

		//#18: the "g" form only became constructible once ToGuid stopped stripping the dashes.
		let fromG = fromJson( R"({"ns":2,"g":"12345678-1234-5678-1234-567812345678"})" );
		ASSERT_TRUE( fromG.IsGuid() );
		EXPECT_TRUE( fromG==id );
	}

	TEST( NodeIdTests, JsonRoundTrip ){
		for( let json : { R"({"ns":2,"i":5002})", R"({"ns":0,"i":85})", R"({"ns":2,"s":"tag.one"})", R"({"ns":0,"s":"x"})",
				R"({"ns":2,"g":"12345678-1234-5678-1234-567812345678"})" } ){
			let original = fromJson( json );
			let round = NodeId{ jvalue{original.ToJson()} };
			EXPECT_TRUE( round==original ) << json;
		}
	}

	//L16: AllocUAString and the UA_String_fromChars( ….c_str() ) sites measured the source with strlen, so an identifier
	//holding an embedded NUL was truncated at it - while the socket path (opc.Common.cpp) copies by size, so the same
	//node id addressed two different nodes depending on which door it came in, and InsertParams stored more bytes than
	//the Row constructor read back.  Nothing mainstream emits such an id; this pins the asymmetry, not a live bug.
	TEST( NodeIdTests, EmbeddedNulSurvivesAStringIdentifier ){
		const string identifier{ "a\0b", 3 };
		ASSERT_EQ( identifier.size(), 3u );

		let fromObject = NodeId{ jvalue{jobject{ {"ns",2}, {"s",identifier} }} };
		ASSERT_TRUE( fromObject.IsString() );
		EXPECT_EQ( fromObject.identifier.string.length, 3u ) << "truncated at the NUL";
		EXPECT_EQ( *fromObject.String(), identifier );

		EXPECT_EQ( *NodeId{ jvalue{jstring{identifier}} }.String(), identifier );//the bare-string spelling too.

		//What is persisted has to be what comes back: InsertParams binds String(), and the Row ctor reads that column.
		let params = fromObject.InsertParams();
		ASSERT_EQ( params.size(), 5u );
		EXPECT_EQ( params[2].get_string(), identifier );

		EXPECT_TRUE( NodeId{ jvalue{fromObject.ToJson()} }==fromObject );//and the json round trip.
	}

	//review3 #8: "ns" was applied only when it was already a number, so a string ns was *ignored* rather than rejected -
	//{"ns":"2","i":85} addressed ns=0;i=85, which is UA_NS0ID_OBJECTSFOLDER - and an object carrying none of id/s/i/b/g
	//fell through every branch and came back as the zero-initialised {ns,NUMERIC,0}.  Both were silent, and both produce
	//a plausible-looking wrong node rather than an error.  This is the id OpcAuthorize compares ACL criteria against.
	TEST( NodeIdTests, FromJsonRejectsAnUnaddressableObject ){
		EXPECT_THROW( fromJson(R"({"ns":"2","i":85})"), Exception );  //a string ns...
		EXPECT_THROW( fromJson(R"({"ns":null,"i":85})"), Exception );
		EXPECT_THROW( fromJson(R"({"ns":2})"), Exception );           //...and no identifier at all...
		EXPECT_THROW( fromJson(R"({"ns":2,"S":"Tag"})"), Exception ); //...including a wrong-case key.
		EXPECT_THROW( fromJson(R"({})"), Exception );

		EXPECT_EQ( fromJson(R"({"ns":2,"i":85})").namespaceIndex, 2 );//the spellings that do address a node still do.
		EXPECT_EQ( fromJson(R"({"i":85})").namespaceIndex, 0 );
		EXPECT_EQ( *fromJson(R"({"id":{"ns":4,"s":"tag"}})").String(), "tag" );
	}

	//L22: ParseQL required the id to be an object or an array of objects, so a bare `id:5002` - which NodeId( TableQL )
	//accepts through the very same FromJson - produced an *empty* vector, and the client saw BadNothingToDo rather than
	//anything about the id.  `id:[5002,5003]` was worse: it threw out of as_object().  system=true on the TableQL so it
	//resolves no db view; ParseQL only reads Args.
	TEST( NodeIdTests, ParseQlTakesEveryIdSpelling ){
		let parseQl = []( sv args )ε->vector<NodeId>{
			return NodeId::ParseQL( QL::TableQL{"nodes", parse(args).as_object(), nullptr, {}, true} );
		};
		ASSERT_EQ( parseQl(R"({"id":5002})").size(), 1u );
		EXPECT_EQ( *parseQl(R"({"id":5002})")[0].Numeric(), 5002u );
		EXPECT_EQ( *parseQl(R"({"id":"tag"})")[0].String(), "tag" );
		EXPECT_EQ( parseQl(R"({"id":{"ns":2,"i":5002}})")[0].namespaceIndex, 2 );//the object spelling still works...

		let many = parseQl( R"({"id":[5002,{"ns":2,"i":5003},"tag"]})" );//...and an array now takes all three.
		ASSERT_EQ( many.size(), 3u );
		EXPECT_EQ( *many[0].Numeric(), 5002u );
		EXPECT_EQ( many[1].namespaceIndex, 2 );
		EXPECT_EQ( *many[2].String(), "tag" );

		EXPECT_TRUE( parseQl(R"({})").empty() );              //no id is still no nodes, not an error...
		EXPECT_THROW( parseQl(R"({"id":true})"), Exception ); //...but an unreadable one is loud.

		//The point of the finding: it agrees with NodeId( TableQL ), the other reader of the same field.
		QL::TableQL bare{ "nodes", parse(R"({"id":5002})").as_object(), nullptr, {}, true };
		EXPECT_TRUE( NodeId{bare}==parseQl(R"({"id":5002})")[0] );
	}

	//DecodeJson takes the QL arg spelling - `ns:4,i:5002`, with or without braces - through QL::Parser.
	TEST( NodeIdTests, DecodeJsonQlForm ){
		let n = NodeId::DecodeJson( "ns:2,i:5002" );
		EXPECT_EQ( n.namespaceIndex, 2 );
		ASSERT_TRUE( n.Numeric() );
		EXPECT_EQ( *n.Numeric(), 5002u );

		let braced = NodeId::DecodeJson( R"({ns:2,s:"tag.one"})" );
		ASSERT_TRUE( braced.String() );
		EXPECT_EQ( *braced.String(), "tag.one" );
		EXPECT_EQ( braced.namespaceIndex, 2 );
	}

	//reviews/opc-review2.md #9 reports that ToString returns the fragment `"Id":5002,"Namespace":2` because
	//options.stringNodeIds is left unset.  That does not reproduce against the installed open62541: a value-initialized
	//UA_EncodeJsonOptions already encodes a NodeId as the quoted string form, so the substr(1,size-2) strips the quotes
	//it means to.  Enabled, as the regression guard for whatever the encoder default does next.
	TEST( NodeIdTests, ToStringEmitsTheUaStringForm ){
		EXPECT_EQ( numeric(2, 5002).ToString(), "ns=2;i=5002" );
		EXPECT_EQ( numeric(0, 85).ToString(), "i=85" );
		EXPECT_EQ( fromJson(R"({"ns":2,"s":"tag.one"})").ToString(), "ns=2;s=tag.one" );
	}

	TEST( NodeIdTests, ToStringOfAList ){
		vector<NodeId> ids;
		ids.emplace_back( numeric(2, 5002) );
		ids.emplace_back( fromJson(R"({"ns":2,"s":"tag"})") );
		EXPECT_EQ( NodeId::ToString(ids), R"([{"ns":2,"i":5002},{"ns":2,"s":"tag"}])" );
	}

	TEST( NodeIdTests, InsertParamsNumeric ){
		let params = numeric( 2, 5002 ).InsertParams();
		ASSERT_EQ( params.size(), 5u );
		EXPECT_EQ( params[0].ToUInt(), 2u );    //namespaceIndex
		EXPECT_EQ( params[1].ToUInt(), 5002u ); //numeric
		EXPECT_TRUE( params[2].is_null() );     //string
	}

	TEST( NodeIdTests, InsertParamsString ){
		let params = fromJson( R"({"ns":2,"s":"tag.one"})" ).InsertParams();
		ASSERT_EQ( params.size(), 5u );
		EXPECT_EQ( params[0].ToUInt(), 2u );
		EXPECT_TRUE( params[1].is_null() );
		EXPECT_EQ( params[2].get_string(), "tag.one" );
	}

	TEST( NodeIdTests, InsertParamsGuid ){
		let params = guidNode( 2, nodeGuid ).InsertParams();
		ASSERT_EQ( params.size(), 5u );
		EXPECT_TRUE( params[1].is_null() );
		EXPECT_TRUE( params[2].is_null() );
		ASSERT_EQ( params[3].get_bytes().size(), 16u );
		EXPECT_EQ( params[3].get_guid(), ToGuid(nodeGuid) );
	}

	TEST( NodeIdTests, InsertParamsBytes ){
		const vector<uint8_t> bytes{ 0xde, 0xad, 0xbe, 0xef };
		let params = bytesNode( 2, bytes ).InsertParams();
		ASSERT_EQ( params.size(), 5u );
		EXPECT_TRUE( params[1].is_null() );
		EXPECT_TRUE( params[2].is_null() );
		EXPECT_EQ( params[4].get_bytes(), bytes );
	}


	//#2 (fixed):  NodeId converts implicitly to its owning UA_NodeId base but had no way to hand ownership over, so every
	//`raw = move(wrapper)` in the consumers compiled as a shallow copy through the sliced base and the wrapper still freed
	//the identifier the raw struct now points at.  Move() is the transfer, matching Variant::Move()/ExNodeId::Move().
	template<class T> concept HasMove = requires( T t ){ { t.Move() } -> std::same_as<UA_NodeId>; };
	TEST( NodeIdTests, MoveHandsOwnershipOver ){
		EXPECT_TRUE( HasMove<NodeId> );

		NodeId id = fromJson( R"({"ns":2,"s":"tag.one"})" );
		let* identifier = id.identifier.string.data;
		auto raw = id.Move();
		EXPECT_EQ( raw.identifier.string.data, identifier ); //transferred, not reallocated...
		EXPECT_EQ( raw.namespaceIndex, 2 );
		EXPECT_TRUE( id.IsNumeric() );                       //...and the source is left the null node id, so ~NodeId frees nothing.
		EXPECT_EQ( *id.Numeric(), 0u );
		UA_NodeId_clear( &raw );                             //Move() made us the owner.
	}

	//The slicing assignment the consumers used to write.  Kept as a guard: it still compiles (the implicit base
	//conversion is load-bearing for the vendor API), so nothing but review catches a regression to it.
	TEST( NodeIdTests, AssigningToARawNodeIdIsAShallowCopy ){
		NodeId id = fromJson( R"({"ns":2,"s":"tag.one"})" );
		UA_NodeId raw{};
		raw = id; //slices - both now point at the same buffer, and only `id` will free it.
		EXPECT_EQ( raw.identifier.string.data, id.identifier.string.data );
	}

	//#4 (fixed): DecodeJson handed UA_decodeJson a `NodeId*` where a `UA_NodeId*` was expected.  NodeId is polymorphic
	//(virtual dtor), so the base sits behind the vptr and the implicit conversion to void* performed no adjustment - the
	//decoder wrote its 24-byte image over the vptr and the returned node carried a bogus identifier.  ToString and
	//DecodeJson are each other's inverse and must round trip.
	TEST( NodeIdTests, DecodeJsonRoundTripsToString ){
		for( let& n : { numeric(2, 5002), numeric(0, 85), fromJson(R"({"ns":2,"s":"tag.one"})"),
				guidNode(2, nodeGuid), fromJson(R"({"ns":4,"s":"Examples/Stacklights"})"),
				fromJson(R"({"ns":2,"s":"urn:plant:line1"})"), fromJson(R"({"ns":2,"s":"Tag:5"})") } )
			EXPECT_TRUE( NodeId::DecodeJson(n.ToString())==n ) << n.ToString();
	}

	//review3 #9: ToString obtained the `ns=2;s=…` form by json-encoding the node and stripping the outer quotes, which
	//left the *encoder's* escaping of ", \, tab and newline in place; DecodeJson then quoted the text back into a json
	//string, escaping it a second time.  `a"b` came out of ToString as `a\"b` and decoded to the four-character
	//identifier `a\"b`, so the node was keyed on one id and looked up by another - GatewayQLAwait stores ToString() in
	//q.Args["name"], and ACL criteria are keyed on it.  Both ends now use the vendor's own printer/parser, which have no
	//escaping step at all.
	TEST( NodeIdTests, ToStringRoundTripsAnEscapableIdentifier ){
		for( let& identifier : { "a\"b"s, "a\\b"s, "a\tb"s, "a\nb"s, "a\rb"s, "a%b"s, "a b"s, "a;b"s } ){
			let n = NodeId{ jvalue{jobject{ {"ns",2}, {"s",identifier} }} };
			ASSERT_EQ( *n.String(), identifier );
			let round = NodeId::DecodeJson( n.ToString() );
			EXPECT_TRUE( round==n ) << "'" << identifier << "' -> '" << n.ToString() << "' -> '" << round.String().value_or("") << "'";
			EXPECT_EQ( round.String().value_or(""), identifier ) << n.ToString();
		}
	}

	//review3 #4, the consumer's half: an ACL criteria is stored in the UA spelling (the SPA's uaString()), and a ':' in
	//the identifier must not change which parser reads it.  The two silent cases are the dangerous ones - `Tag:5` and
	//friends parsed as json args and returned ns=0;i=0, attaching the permission to a node that does not exist.
	TEST( NodeIdTests, DecodeJsonKeepsColonsInTheIdentifier ){
		for( let& id : { "urn:plant:line1"sv, "Channel1.Device1:Tag"sv, "Tag:5"sv, "Tag:12.5"sv, "Tag:true"sv, "Tag:null"sv, "2026-08-24T10:00:00Z"sv } ){
			let n = NodeId::DecodeJson( Ƒ("ns=2;s={}", id) );
			EXPECT_EQ( n.namespaceIndex, 2 ) << id;
			ASSERT_TRUE( n.String() ) << id;
			EXPECT_EQ( *n.String(), id ) << id;
		}
		let noNamespace = NodeId::DecodeJson( "s=urn:plant:line1" );
		ASSERT_TRUE( noNamespace.String() );
		EXPECT_EQ( *noNamespace.String(), "urn:plant:line1" );
		EXPECT_EQ( noNamespace.namespaceIndex, 0 );
	}

	//The form OpcAuthorize actually stores in an ACL resource.Criteria - bare and unquoted.  Getting this wrong keyed
	//authorization on the wrong node rather than failing.
	TEST( NodeIdTests, DecodeJsonReadsTheBareUaForm ){
		let n = NodeId::DecodeJson( "ns=4;i=6020" );
		EXPECT_EQ( n.namespaceIndex, 4 );
		ASSERT_TRUE( n.Numeric() );
		EXPECT_EQ( *n.Numeric(), 6020u );

		let noNamespace = NodeId::DecodeJson( "i=85" );
		EXPECT_EQ( noNamespace.namespaceIndex, 0 );
		EXPECT_EQ( *noNamespace.Numeric(), 85u );

		let stringId = NodeId::DecodeJson( "ns=2;s=tag.one" );
		ASSERT_TRUE( stringId.String() );
		EXPECT_EQ( *stringId.String(), "tag.one" );

		EXPECT_THROW( NodeId::DecodeJson("not a node id"), Exception ); //loudly, so OpcAuthorize logs and drops it.
	}

	//The QL arg spelling picks the other branch - it is not the UA spelling, and it has a ':'.  Both reach the same node.
	TEST( NodeIdTests, DecodeJsonAgreesAcrossBothSpellings ){
		EXPECT_TRUE( NodeId::DecodeJson("ns=4;i=6020")==NodeId::DecodeJson("ns:4,i:6020") );
	}

	//#7 (fixed): the serializer emitted lowercase hex for a byte-string identifier while FromJson base64-decoded it, so a
	//node could not re-read its own json - BadNodeIdUnknown, or a hit on the wrong node when the hex parsed as valid
	//base64.  base64 won, since that is what OPC-UA specifies and what the parser already did.
	TEST( NodeIdTests, ByteStringJsonRoundTrip ){
		const vector<uint8_t> bytes{ 0xde, 0xad, 0xbe, 0xef };
		let original = bytesNode( 2, bytes );
		ASSERT_TRUE( original.IsBytes() );
		EXPECT_EQ( original.ToJson().at("b").as_string(), "3q2+7w==" ) << "the wire format is base64, not hex";
		let round = NodeId{ jvalue{original.ToJson()} };
		EXPECT_TRUE( round==original ) << serialize( original.ToJson() );

		//lengths either side of a base64 padding boundary, since that is where a hand-rolled codec goes wrong.
		for( let length : { 1u, 2u, 3u, 5u, 64u } ){
			let padded = bytesNode( 2, vector<uint8_t>(length, 0xab) );
			EXPECT_TRUE( NodeId{jvalue{padded.ToJson()}}==padded ) << length << ": " << serialize( padded.ToJson() );
		}
	}

	//A "b" that is not base64 must be rejected, not silently read as an empty identifier - review #1's
	//"unchecked base64 status" known-open item, which sits in the line this fix touches.
	TEST( NodeIdTests, ByteStringRejectsMalformedBase64 ){
		EXPECT_THROW( fromJson(R"({"ns":2,"b":"!!!not base64!!!"})"), Exception );
	}

	//#6 (fixed): the unused guid/bytes columns used to bind a *non-null* empty blob while the insert procs dispatch on
	//IS NULL, so a byte-string id took the guid branch and died in toUuidString with "Guid blob is 0 bytes, expected 16."
	TEST( NodeIdTests, UnusedIdentifierColumnsAreNull ){
		let numericParams = numeric( 2, 5002 ).InsertParams();
		EXPECT_TRUE( numericParams[3].is_null() ) << "guid column of a numeric id";
		EXPECT_TRUE( numericParams[4].is_null() ) << "bytes column of a numeric id";

		let bytesParams = bytesNode( 2, {0xde, 0xad} ).InsertParams();
		EXPECT_TRUE( bytesParams[3].is_null() ) << "guid column of a byte-string id";
		EXPECT_FALSE( bytesParams[4].is_null() );

		let guidParams = guidNode( 2, nodeGuid ).InsertParams();
		EXPECT_TRUE( guidParams[4].is_null() ) << "bytes column of a guid id";
		EXPECT_FALSE( guidParams[3].is_null() );
	}

	//#11 (fixed): open62541 treats a non-empty outBuf as a hard limit ("if sufficient"), so ToString's 1024-byte buffer
	//capped the identifier length - a 2048-char one silently fell back to the *other* textual format,
	//serialize(ToJson()) - `{"ns":0,"s":"xxx..."}` - which a DecodeJson round trip then read back as a different node.
	//The assertion checks the format, not just that the identifier appears somewhere: the fallback contained it too.
	TEST( NodeIdTests, LongStringIdentifierSurvivesToString ){
		for( let length : { 40u, 2048u, 8192u } ){
			let identifier = string( length, 'x' );
			let id = NodeId{ jvalue{jstring{identifier}} };
			ASSERT_TRUE( id.IsString() );
			EXPECT_EQ( id.ToString(), "s="+identifier ) << length << ": " << id.ToString().substr( 0, 64 );
		}
	}

	//absl::Hash must agree with equality, which operator< defines (reviews/abseil.md D3) - the gateway's by-node index rests on it.
	//Pairs that differ only in namespace, identifier type or identifier.
	TEST( NodeIdTests, AbslHashAgreesWithEquality ){
		const vector<NodeId> ids{ numeric(2, 5), numeric(3, 5), numeric(2, 6), fromJson(R"({"ns":2,"s":"5"})"), fromJson(R"({"ns":2,"s":"6"})"),
			guidNode(2, nodeGuid), guidNode(3, nodeGuid), bytesNode(2, {5}), bytesNode(2, {6}) };
		const vector<NodeId> copies = ids;
		for( uint i=0; i<ids.size(); ++i ){
			for( uint j=0; j<copies.size(); ++j ){
				ASSERT_EQ( ids[i]==copies[j], i==j ) << ids[i].ToString() << " vs " << copies[j].ToString();
				if( i==j )
					EXPECT_EQ( absl::HashOf(ids[i]), absl::HashOf(copies[j]) ) << ids[i].ToString();
				else
					EXPECT_NE( absl::HashOf(ids[i]), absl::HashOf(copies[j]) ) << ids[i].ToString() << " vs " << copies[j].ToString();
			}
		}
	}
	//Equal values held differently must hash the same - what absl::VerifyTypeImplementsAbslHashCorrectly checks, which needs gmock
	//(the deps build googletest with BUILD_GMOCK=OFF).  A null and an empty identifier both read as "", so they compare equal.
	TEST( NodeIdTests, AbslHashOfEqualRepresentations ){
		auto withString = []( UA_NodeIdType type, UA_String s ){
			UA_NodeId ua{};
			ua.namespaceIndex = 2;
			ua.identifierType = type;
			ua.identifier.string = s;
			return NodeId{ move(ua) };
		};
		for( let type : {UA_NODEIDTYPE_STRING, UA_NODEIDTYPE_BYTESTRING} ){
			let null = withString( type, UA_STRING_NULL );
			let empty = withString( type, UA_String_fromChars("") );
			ASSERT_TRUE( null==empty ) << type;
			EXPECT_EQ( absl::HashOf(null), absl::HashOf(empty) ) << type;
		}
	}
}
