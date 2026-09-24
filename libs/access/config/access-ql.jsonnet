// GraphQL introspection for the access schema's Group and User types - the field list and type of each, including the
// fields no table column carries (Group.members, the Identity union; User's certificate fields).  Read by QL::Configure
// from the `ql:` path beside a schema's `meta:` in the args files (libs/ql ql.cpp AddIntrospection) and served to the
// site's __type queries.
local String = { kind:"SCALAR", name:'String' };
local NonNullString = { kind: 'NON_NULL', name:null, ofType:String };
local Id = { kind: 'NON_NULL', name:null, ofType:{kind:"SCALER", name:"ID"} };
local Attributes = { kind:"SCALAR", name: 'UInt' };
local DateTime = { kind:"SCALAR",name:'DateTime' };
local NonNullDateTime = { kind:'NON_NULL',name:null, ofType: DateTime };
{
	Group:{
		fields: [
				{ name: 'id', type: Id },
				{ name: 'name', type: NonNullString },
				{ name: 'attributes',type: Attributes },
				{ name: 'created', type: NonNullDateTime },
				{ name: 'updated', type: DateTime },
				{ name: 'deleted', type:DateTime },
				{ name: 'slug',  type:String },
				{ name: 'description', type: String },
				{ name: 'provider', type: {kind:"ENUM", name:'Provider'} },
				{ name: 'members', type: {name: null, kind: "LIST", ofType: {name: "Identity", kind: "UNION"}} }
		]},
	User:{
		fields: [
				{ name: 'id', type: Id },
				{ name: 'name', type: NonNullString },
				{ name: 'provider', type: {kind:"ENUM", name:'Provider'} },
				{ name: 'slug',  type: NonNullString },
				{ name: 'attributes',type: Attributes },
				{ name: 'created', type: NonNullDateTime },
				{ name: 'updated', type: DateTime },
				{ name: 'deleted', type:DateTime },
				{ name: 'description', type: String },
				{ name: 'email', type: String },
				{ name: 'loginName', type: String },
				{ name: 'modulus', type: String },
				{ name: 'exponent', type: Attributes },
				{ name: 'issuer', type: String },
				{ name: 'subjectAlt', type: String },
				{ name: 'distinguished', type: String },
				{ name: 'expiration', type: DateTime },
				{ name: 'fingerprint', type: String }
		]}
}