import { ObjectType, OpcObject, UaNode, Variable } from './node';
import { NodeId } from './node-id';
import { childBySegment, EAccess } from './types';
import { variable } from 'jde-opc/testing';

const x = ( json:object={} )=>variable( 1, 'x', json );

//clients spec, Gaps #6:  the model read referenceType and typeDefinition where the browse writes refType and typeDef
//(Browse::Response::SetJson), so neither was ever filled.
describe( 'UaNode browse keys', ()=>{
	it( 'reads the reference type and the type definition under the keys the gateway writes', ()=>{
		const v = x( {refType: {ns: 0, i: 47}, typeDef: {ns: 2, i: 1002}} );
		expect( v.refType?.uaString() ).toBe( "i=47" );
		expect( v.typeDef?.uaString() ).toBe( "ns=2;i=1002" );
		expect( v.typeDef?.constructor ).toBe( NodeId );//an id, not an ObjectType node with no name
		const o = new OpcObject( <any>{ns: 2, i: 5, name: "o", browse: {ns: 2, name: "o"}, refType: {ns: 0, i: 35}, typeDef: {ns: 0, i: 61}} );
		expect( o.refType?.uaString() ).toBe( "i=35" );
		expect( o.typeDef?.uaString() ).toBe( "i=61" );
	} );
	it( 'has neither where the browse sent neither', ()=>{
		expect( x().refType ).toBeUndefined();
		expect( x().typeDef ).toBeUndefined();
	} );
} );

//clients spec *Events* (#424):  the Events tab shows on a node whose EventNotifier has the SubscribeToEvents bit, read as the other
//attributes are - a number, {v,sc} from a server that answers with a status, null or a status alone from one that doesn't say
describe( 'UaNode.notifiesEvents', ()=>{
	it( 'is bit 0 of the EventNotifier, however the attribute came', ()=>{
		const o = ( eventNotifier:unknown )=>new OpcObject( <any>{ns: 2, i: 5005, name: 'o', browse: {ns: 2, name: 'o'}, eventNotifier} );
		expect( o( 1 ).notifiesEvents ).toBe( true );
		expect( o( 5 ).notifiesEvents ).toBe( true );
		expect( o( 0 ).notifiesEvents ).toBe( false );
		expect( o( {v: 1, sc: 0x40000000} ).notifiesEvents ).toBe( true );
		expect( o( {sc: 0x80350000} ).notifiesEvents ).toBe( false );
		expect( o( null ).notifiesEvents ).toBe( false );
		expect( o( undefined ).eventNotifier ).toBeUndefined();
		expect( x().notifiesEvents ).toBe( false );
	} );
} );

//clients spec, Gaps #8:  a push carries both timestamps (Opc.FromServer.proto NodeValues.source/server) and the row kept
//value and status alone.
describe( 'Variable timestamps', ()=>{
	const source = new Date( 1_700_000_000_000 ), server = new Date( 1_700_000_000_500 );
	it( 'keeps both timestamps a push carries', ()=>{
		const v = x( {value: 7} );
		expect( v.sourceTime ).toBeUndefined();//the browse brings none
		v.setReading( {value: 8, sc: 0, source, server} );
		expect( v ).toMatchObject( {value: 8, sourceTime: source, serverTime: server} );
	} );
	it( 'times a Bad push that carries no value, which the row keeps its last value through', ()=>{
		const v = x( {value: 7} );
		v.setReading( {sc: 0x808C0000, source, server} );
		expect( v ).toMatchObject( {value: 7, sourceTime: source, serverTime: server} );
	} );
	it( 'drops them for a new reading that came without - a write\'s echo or a re-read over REST', ()=>{
		const v = x( {value: 7} );
		v.setReading( {value: 8, sc: 0, source, server} );
		v.setReading( {value: 9, sc: 0} );
		expect( v.value ).toBe( 9 );
		expect( v.sourceTime ).toBeUndefined();
		expect( v.serverTime ).toBeUndefined();
		v.setReading( {value: 9, sc: 0, source, server} );
		v.setReading( {value: 9, sc: 0x40000000} );//the value again, but another quality:  another reading
		expect( v.sourceTime ).toBeUndefined();
	} );
	//reviews-issue/412-clients-web-fixes.md #8:  a Refresh, or a write's echo after the push it caused, blanked the times of a
	//value the push had timed.
	it( 'keeps them for a REST reading of the value and quality the push brought', ()=>{
		const v = x( {value: 7} );
		v.setReading( {value: 8, sc: 0, source, server} );
		v.setReading( {value: 8, sc: 0} );
		expect( v ).toMatchObject( {value: 8, sourceTime: source, serverTime: server} );
	} );
	it( 'hands them to a Refresh\'s row that reads the same, and the value too through a fault', ()=>{
		const before = x( {value: 8} );
		before.setReading( {value: 8, sc: 0, source, server} );
		const same = x( {value: 8} ), changed = x( {value: 9} );
		same.takeOver( before );
		changed.takeOver( before );
		expect( same ).toMatchObject( {sourceTime: source, serverTime: server} );
		expect( changed.sourceTime ).toBeUndefined();
		const faulted = x( {value: {sc: 0x808C0000}} );
		before.setReading( {sc: 0x808C0000, source, server} );
		faulted.takeOver( before );
		expect( faulted ).toMatchObject( {value: 8, sc: 0x808C0000, sourceTime: source} );
	} );
} );

describe( 'Variable.historyReadable', ()=>{
	it( 'needs historizing, as a boolean or a {v,sc} reading', ()=>{
		expect( x( {historizing: true} ).historyReadable ).toBe( true );
		expect( x( {historizing: {v: true, sc: 0x40000000}} ).historyReadable ).toBe( true );
		expect( x( {historizing: false} ).historyReadable ).toBe( false );
		expect( x( {historizing: null} ).historyReadable ).toBe( false );//the server did not say
		expect( x( {historizing: {sc: 0x80000000}} ).historyReadable ).toBe( false );
		expect( x( {} ).historyReadable ).toBe( false );
	} );
	it( 'needs the HistoryRead bit when the server gave a level, and takes no level as unknown', ()=>{
		expect( x( {historizing: true, userAccessLevel: EAccess.Read|EAccess.HistoryRead} ).historyReadable ).toBe( true );
		expect( x( {historizing: true, userAccessLevel: EAccess.Read} ).historyReadable ).toBe( false );
		expect( x( {historizing: true, userAccessLevel: 0} ).historyReadable ).toBe( false );
		expect( x( {historizing: true, userAccessLevel: null} ).historyReadable ).toBe( true );
	} );
} );

describe( 'Variable.historyWritable', ()=>{
	it( 'needs historizing and the HistoryWrite bit, which OpcServer withholds on an unprotected node, and takes no level as unknown', ()=>{
		expect( x( {historizing: true, userAccessLevel: EAccess.Read|EAccess.HistoryRead|EAccess.HistoryWrite} ).historyWritable ).toBe( true );
		expect( x( {historizing: true, userAccessLevel: EAccess.Read|EAccess.HistoryRead} ).historyWritable ).toBe( false );
		expect( x( {historizing: true, userAccessLevel: EAccess.HistoryWrite} ).historyWritable ).toBe( true );
		expect( x( {historizing: false, userAccessLevel: EAccess.All} ).historyWritable ).toBe( false );
		expect( x( {historizing: true} ).historyWritable ).toBe( true );
	} );
} );
//clients spec *Address space* (#416):  every class the browse answers is a node with a page, but a Method;  the standard's own nodes
//are system nodes behind the children table's toggle, with Objects, Types and Views exempt as the root was;  and a namespace-0
//name is spelled bare in a url, as the default namespace's is.
describe( 'UaNode classes', ()=>{
	const json = ( nodeClass:number, i=1 )=>({ ns: 0, i, name: 'n', browse: {ns: 0, name: 'n'}, nodeClass });
	it( 'builds every class the gateway spells, and none it does not', ()=>{
		expect( [1, 2, 4, 8, 16, 32, 64, 128].map( c=>UaNode.fromJson( json(c) )?.nodeClass ) ).toEqual( [1, 2, 4, 8, 16, 32, 64, 128] );
		expect( UaNode.fromJson( json(1) ) ).toBeInstanceOf( OpcObject );
		expect( UaNode.fromJson( json(2) ) ).toBeInstanceOf( Variable );
		expect( UaNode.fromJson( json(8) ) ).toBeInstanceOf( ObjectType );
		expect( UaNode.fromJson( json(0) ) ).toBeUndefined();
	} );
	it( 'gives every class a page but a Method', ()=>{
		expect( [1, 2, 8, 16, 32, 64, 128].map( c=>UaNode.fromJson( json(c) )!.hasPage ) ).toEqual( [true, true, true, true, true, true, true] );
		expect( UaNode.fromJson( json(4) )!.hasPage ).toBe( false );
	} );
	it( 'marks the standard\'s nodes system, with the first level exempt', ()=>{
		expect( new OpcObject( json(1, 2253) ).isSystem ).toBe( true );//Server
		expect( UaNode.fromJson( json(8, 58) )!.isSystem ).toBe( true );//BaseObjectType
		expect( [84, 85, 86, 87].map( i=>new OpcObject( json(1, i) ).isSystem ) ).toEqual( [true, false, false, false] );//Root is never a row
		expect( x().isSystem ).toBe( false );//namespace 2
		expect( new OpcObject( <any>{ns: 0, s: 'own', name: 'n', browse: {ns: 0, name: 'n'}} ).isSystem ).toBe( false );//a string id of namespace 0 is a server's own
		expect( OpcObject.rootNode.isRoot ).toBe( true );
		expect( OpcObject.objectsNode.isRoot ).toBe( false );
	} );
	it( 'spells a namespace-0 name bare, as the default namespace\'s', ()=>{
		expect( OpcObject.objectsNode.browseFQ( 1 ) ).toBe( 'Objects' );
		expect( x().browseFQ( 2 ) ).toBe( 'x' );
		expect( x().browseFQ( 1 ) ).toBe( '2~x' );
	} );
	//reviews-issue/416-address-space.md #7:  a namespace-0 name and the default namespace's, siblings, were both spelled bare, and the
	//gateway took the default's where the store took the first in id order - namespace 0's.  A bare name is the default's on both
	//sides, so its namespace-0 twin is `0~name`;  under Root the other way round, as the gateway's translate takes Root's first segment.
	it( "spells a namespace-0 name `0~` beside a default namespace sibling of the same name", ()=>{
		const ns0 = new OpcObject( <any>{ns: 0, i: 2253, name: 'Server', browse: {ns: 0, name: 'Server'}} );
		const own = new OpcObject( <any>{ns: 2, i: 9, name: 'Server', browse: {ns: 2, name: 'Server'}} );
		const other = new OpcObject( <any>{ns: 2, i: 10, name: 'pump1', browse: {ns: 2, name: 'pump1'}} );
		expect( ns0.browseFQ( 2, [ns0, other] ) ).toBe( 'Server' );
		expect( ns0.browseFQ( 2, [ns0, own, other] ) ).toBe( '0~Server' );
		expect( own.browseFQ( 2, [ns0, own, other] ) ).toBe( 'Server' );
		expect( ns0.browseFQ( 0, [ns0, own] ) ).toBe( 'Server' );//namespace 0 the default:  no twin to tell apart
		expect( ns0.browseFQ( 2, [ns0, own], true ) ).toBe( 'Server' );//Root's children:  namespace 0's first
		expect( own.browseFQ( 2, [ns0, own], true ) ).toBe( '2~Server' );
	} );
	it( "takes a bare segment for the default namespace's child first, then namespace 0's", ()=>{
		const ns0 = { browse: {ns: 0, name: 'Server'} }, own = { browse: {ns: 2, name: 'Server'} }, objects = { browse: {ns: 0, name: 'Objects'} };
		expect( childBySegment( [ns0, own], 'Server', 2 ) ).toBe( own );
		expect( childBySegment( [ns0, own], '0~Server', 2 ) ).toBe( ns0 );
		expect( childBySegment( [ns0, own], '2~Server', 2 ) ).toBe( own );
		expect( childBySegment( [ns0, objects], 'Objects', 2 ) ).toBe( objects );
		expect( childBySegment( [ns0], 'Server', undefined ) ).toBe( ns0 );
		expect( childBySegment( [ns0, own], 'Server', 2, true ) ).toBe( ns0 );
		expect( childBySegment( [ns0, own], '3~Server', 2 ) ).toBeUndefined();
	} );
} );