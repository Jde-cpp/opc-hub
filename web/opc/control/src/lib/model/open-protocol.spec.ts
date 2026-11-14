import { ceilingsError, ceilingsText, composeDriver, composeOpenProtocolUrl, hostError, identityFields, parseCeilings, parseDriver, parseOpenProtocolUrl, portError, readOp, opIds, scBadWaitingForInitialData, systemTypeText, timeZoneError } from './open-protocol';

//docs/open-protocol/spec.md *The row*:  the url is open-protocol://host[:port], 4545 where the port is left off (§2.1.1), and the form
//shows the two apart.  The gateway's OpenProtocolRow is the rule;  the form says what it would refuse.
describe( 'Open Protocol url', ()=>{
	it( 'parses the host and the port, the default where the url names none', ()=>{
		expect( parseOpenProtocolUrl("open-protocol://plc:4546") ).toEqual( {host: "plc", port: "4546"} );
		expect( parseOpenProtocolUrl("open-protocol://plc") ).toEqual( {host: "plc", port: "4545"} );
		expect( parseOpenProtocolUrl(" open-protocol://10.0.0.5:4545 ") ).toEqual( {host: "10.0.0.5", port: "4545"} );
		expect( parseOpenProtocolUrl("open-protocol://[::1]:4547") ).toEqual( {host: "[::1]", port: "4547"} );//the last colon's digits are the port
	} );
	it( 'reads no host off a url of another scheme, so a row switched to the kind starts blank', ()=>{
		expect( parseOpenProtocolUrl("opc.tcp://plc:4840") ).toEqual( {host: "", port: "4545"} );
		expect( parseOpenProtocolUrl(undefined) ).toEqual( {host: "", port: "4545"} );
	} );
	it( 'composes host:port, the port stated so a saved url and the form compare as one', ()=>{
		expect( composeOpenProtocolUrl(" plc ", "4546") ).toBe( "open-protocol://plc:4546" );
		expect( composeOpenProtocolUrl("plc", "") ).toBe( "open-protocol://plc:4545" );
		expect( composeOpenProtocolUrl("plc", null) ).toBe( "open-protocol://plc:4545" );
		expect( parseOpenProtocolUrl(composeOpenProtocolUrl("[::1]", 4545)) ).toEqual( {host: "[::1]", port: "4545"} );
	} );
	it( 'refuses what the gateway refuses of a host and a port, and says nothing of an empty host', ()=>{
		expect( hostError("") ).toBeUndefined();//the required marker covers it
		expect( hostError("plc.plant.local") ).toBeUndefined();
		expect( hostError("[fe80::1]") ).toBeUndefined();
		expect( hostError("plc 1") ).toContain( "no space" );
		expect( hostError("plc/station") ).toContain( "slash" );
		expect( hostError("fe80::1") ).toContain( "brackets" );
		expect( portError("") ).toBeUndefined();//4545
		expect( portError("4545") ).toBeUndefined();
		expect( portError("65535") ).toBeUndefined();
		for( const bad of ["0", "65536", "-1", "45a", "1.5"] )
			expect( portError(bad) ).toContain( "1 to 65535" );
	} );
} );

//`driver`:  {"timeZone":…,"revisions":{"0061":3}}, either member left out where unset, null with neither;  the ceilings as one line in the
//spelling `_Op.Revisions` shows them in.
describe( 'Open Protocol driver settings', ()=>{
	it( 'parses the zone and the ceilings, and nothing from text that is not an object', ()=>{
		expect( parseDriver('{"timeZone":"Europe/Stockholm","revisions":{"0061":3,"0001":6}}') ).toEqual( {timeZone: "Europe/Stockholm", revisions: {"0061": 3, "0001": 6}} );
		expect( parseDriver('{"revisions":{"0061":3}}') ).toEqual( {timeZone: null, revisions: {"0061": 3}} );
		expect( parseDriver(null) ).toEqual( {timeZone: null, revisions: {}} );
		expect( parseDriver("not json") ).toEqual( {timeZone: null, revisions: {}} );
		expect( parseDriver("[1]") ).toEqual( {timeZone: null, revisions: {}} );
	} );
	it( 'composes one canonical text, members in order and MIDs sorted, null with neither', ()=>{
		expect( composeDriver("Europe/Stockholm", {"0061": 3, "0001": 6}) ).toBe( '{"timeZone":"Europe/Stockholm","revisions":{"0001":6,"0061":3}}' );
		expect( composeDriver(" Europe/Stockholm ", {}) ).toBe( '{"timeZone":"Europe/Stockholm"}' );
		expect( composeDriver(null, {"0061": 3}) ).toBe( '{"revisions":{"0061":3}}' );
		expect( composeDriver("", {}) ).toBeNull();
		const d = parseDriver( '{"revisions": {"0061": 3}, "timeZone": "UTC"}' );//written by hand, spaces and all
		expect( composeDriver(d.timeZone, d.revisions) ).toBe( '{"timeZone":"UTC","revisions":{"0061":3}}' );
	} );
	it( 'reads the ceilings as MID:revision pairs, spaces or commas between, and spells them back sorted', ()=>{
		expect( parseCeilings("0061:3 0001:6") ).toEqual( {"0061": 3, "0001": 6} );
		expect( parseCeilings(" 0061:3,0001:6 ") ).toEqual( {"0061": 3, "0001": 6} );
		expect( parseCeilings("") ).toEqual( {} );
		expect( parseCeilings(null) ).toEqual( {} );
		expect( ceilingsText({"0061": 3, "0001": 999}) ).toBe( "0001:999 0061:3" );
		expect( ceilingsText({}) ).toBe( "" );
	} );
	it( 'refuses a pair that is not four digits and a revision from 1 to 999', ()=>{
		for( const bad of ["61:3", "0061", "0061:0", "0061:1000", "0061:3 x", "0061=3"] ){
			expect( parseCeilings(bad) ).toBeUndefined();
			expect( ceilingsError(bad) ).toContain( "MID:revision" );
		}
		expect( ceilingsError("0061:3") ).toBeUndefined();
		expect( ceilingsError("") ).toBeUndefined();
	} );
	it( 'takes a zone the database names in its own spelling, and nothing else', ()=>{
		expect( timeZoneError("") ).toBeUndefined();//the gateway's own
		expect( timeZoneError("Europe/Stockholm") ).toBeUndefined();
		expect( timeZoneError("UTC") ).toBeUndefined();
		expect( timeZoneError("Mars/Olympus") ).toContain( "Not a zone" );
		expect( timeZoneError("europe/stockholm") ).toContain( "Europe/Stockholm" );//resolved to the database's spelling, which the gateway wants
	} );
} );

//What MID 0002 answered, as testConnection reports it and `_Op.Identity` holds it, and `_Op` itself read by id (spec *Address space*)
describe( 'Open Protocol identity and _Op', ()=>{
	const identity = { controllerName: "PF6000-1", supplierCode: "ACT", openProtocolVersion: "2.20.1", controllerSoftwareVersion: "3.8.0", toolSoftwareVersion: "1.2", controllerSerialNumber: "SN1", systemType: 3, systemSubtype: 1, stationId: 2, stationName: "Station A" };
	it( 'lists the identity a row per field, the system by name and the station by name and id', ()=>{
		const rows = identityFields( identity );
		expect( rows.map( r=>r.label ) ).toEqual( ["Controller", "System", "Supplier Code", "Open Protocol Version", "Controller Software", "Tool Software", "Serial Number", "Station"] );
		expect( rows[0].value ).toBe( "PF6000-1" );
		expect( rows[1].value ).toBe( "Power Focus 6000, subtype 1" );
		expect( rows[7].value ).toBe( "Station A (2)" );
	} );
	it( 'leaves a field its revision did not carry blank, and shows an unknown system type as its number', ()=>{
		const rows = identityFields( {...identity, supplierCode: null, systemType: 9, systemSubtype: null, stationId: null, stationName: null} );
		expect( rows[2].value ).toBe( "" );
		expect( rows[1].value ).toBe( "9" );
		expect( rows[7].value ).toBe( "" );
		expect( systemTypeText(null) ).toBe( "" );
		expect( systemTypeText(1) ).toBe( "Power Focus 4000" );
	} );
	it( 'asks for the eleven variables of the row\'s namespace by the string ids of their paths', ()=>{
		expect( opIds.length ).toBe( 11 );
		expect( opIds[0] ).toEqual( {ns: 2, s: "_Op.Connected"} );
		expect( opIds.map( id=>id.s ) ).toContain( "_Op.Skew" );
	} );
	it( 'reads the rows by name, a Bad read as its status alone, an Uncertain one as its value and status, a row not answered as absent', ()=>{
		const op = readOp( [
			{ id: {ns: 2, s: "_Op.Connected"}, value: true },
			{ id: {ns: 2, s: "_Op.Skew"}, value: {sc: scBadWaitingForInitialData} },
			{ id: {ns: 2, s: "_Op.Results"}, value: {v: 12, sc: 0x40000000} },
			{ id: {ns: 2, s: "_Op.LastError"}, value: "" }
		] );
		expect( op.Connected ).toEqual( {value: true} );
		expect( op.Skew ).toEqual( {sc: scBadWaitingForInitialData} );
		expect( op.Results ).toEqual( {value: 12, sc: 0x40000000} );
		expect( op.LastError ).toEqual( {value: ""} );
		expect( op.Since ).toBeUndefined();
		expect( readOp(undefined) ).toEqual( {} );
	} );
} );