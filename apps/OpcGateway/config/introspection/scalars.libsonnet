//The type references the introspection files spell their fields with - the __type shape the web client reads:  the scalars,
//an object by name, and NON_NULL / LIST around any type.
{
	String:: { kind: "SCALAR", name: "String" },
	UInt:: { kind: "SCALAR", name: "UInt" },
	Boolean:: { kind: "SCALAR", name: "Boolean" },
	DateTime:: { kind: "SCALAR", name: "DateTime" },
	Float:: { kind: "SCALAR", name: "Float" },
	Json:: { kind: "SCALAR", name: "Json" }, //a value as /opc carries it:  a number, string, boolean, array or object.
	Object( name ):: { kind: "OBJECT", name: name },
	NonNull( t ):: { kind: "NON_NULL", name: null, ofType: t },
	List( t ):: { kind: "LIST", name: null, ofType: t }
}