#pragma once
//`Commands` of an Open Protocol row as the driver's suites call them (DriverCommandTests, DriverSimulatorTests):  a method
//through the QL's `call`, as a web session's client calls one under its issued token, and what that client reads after.
#include "DriverResults.h"

namespace Jde::Opc::Gateway::Tests{
	struct DriverCommandFixture : DriverResultFixture{
	protected:
		static constexpr StatusCode Good{ UA_STATUSCODE_GOOD };
		Ω WaitRevisions( const DriverRow& row, sv text )ι->bool{ return WaitOp( row, "Revisions", [=]( let& v ){ return OpText( v )==text; } ); }
		Ω Method( sv name )ι->NodeId{ return DriverNode( Ƒ("Commands.{}", name) ); }
		//A method of the row's `Commands` through the QL's `call`:  the call's status, the server's whatever it is.
		Ω Call( const DriverRow& row, sv method, jarray args={} )ε->StatusCode{
			const jobject vars{ {"opc", row.Slug}, {"object", DriverNode("Commands").ToJson()}, {"method", Method(method).ToJson()}, {"args", move(args)} };
			return Socket().QuerySync( "call( opc: $opc, object: $object, method: $method, args: $args ){ statusCode }", vars ).as_object().at( "statusCode" ).to_number<StatusCode>();
		}
		//Until the session's user may, or may not, call the row's methods, as a method's UserExecutable has it:  a grant reaches
		//the gateway's authorizer after the mutation that made it returns.
		Ω WaitExecutable( const DriverRow& row, bool executable )ι->bool{
			return WaitUntil( [&]{
				try{
					let node = Socket().QuerySync( "node( opc: $opc, id: $id ){ userExecutable }", {{"opc", row.Slug}, {"id", Method("EnableTool").ToJson()}} ).as_object();
					return Json::FindBool( node, "userExecutable" )==executable;
				}
				catch( const std::exception& ){
					return false;
				}
			}, 15s );
		}
		Ω Text( const Value& v )ι->string{ return v.hasValue && !v.IsEmpty() ? serialize( v.ValueJson(nullptr) ) : "no value"; }
		//Until the row's `path` reads Good with `json`, as a web session's client reads it.
		Ω WaitValue( const DriverRow& row, sv path, sv json )ι->bool{
			return WaitUntil( [&]{
				try{
					let v = ReadValue( BlockAwait(UAClient::GetClient(row.Slug, SessionCredential())), DriverNode(path) );
					return !v.status && Text( v )==json;
				}
				catch( const std::exception& ){
					return false;
				}
			}, 15s );
		}
	};
}