import { ExNodeId } from './ex-node-id';
import { histThresholdBlank } from './hist';
import { HistGroup, HistTemplate, histNodeJson, histNodeLabel, histNodeText, parseHistNodeText, toHistGroupMember, toHistResolved, toHistTemplateMember } from './hist-admin';

const row = { id: 5, name: 'Pumps', slug: 'pumps', guid: '0f8fad5b-d9cb-469f-a165-70867728950e', serverConnection: {id: 1, name: 'Local'}, histTemplate: {id: 2, name: 'Pump'}, root: '{"nsu":"urn:jde:pumps","i":5001}', exceptionDeviation: null, exceptionDeviationFormat: null, minTimeInterval: null, maxTimeInterval: 60000, stepped: null };

describe( 'HistGroup', ()=>{
	//the QL row's joins name the connection and template;  the thresholds are text, blank for null
	it( 'takes a QL row, and itself', ()=>{
		const g = new HistGroup( row );
		expect( g.serverConnectionId ).toBe( 1 );
		expect( g.connection ).toEqual( {id: 1, name: 'Local'} );
		expect( g.histTemplateId ).toBe( 2 );
		expect( g.thresholds ).toEqual( {...histThresholdBlank, maxTimeInterval: '60000'} );
		expect( g.rootNode?.nsu ).toBe( 'urn:jde:pumps' );
		const again = new HistGroup( g );
		expect( again.equals( g ) ).toBe( true );
		expect( again.connection ).toEqual( g.connection );
	} );
	//a new group needs its connection; the root, when typed, has to be a node; a threshold typed has to be a value
	it( 'saves once it has a name, a slug and a connection, and refuses a bad root or threshold', ()=>{
		const g = new HistGroup( {name: 'Pumps', slug: 'pumps'} );
		expect( g.canSave ).toBe( false );
		g.serverConnectionId = 1;
		expect( g.canSave ).toBe( true );
		g.root = 'pump1';
		expect( g.rootError ).toBeDefined();
		expect( g.fieldError( 'root' ) ).toBe( g.rootError );
		expect( g.canSave ).toBe( false );
		g.root = 'ns=2;i=5001';
		expect( g.canSave ).toBe( true );
		g.thresholds = {...histThresholdBlank, minTimeInterval: '-1'};
		expect( g.canSave ).toBe( false );
	} );
	//a create states the source and each threshold set;  an update never sends the source, and a cleared threshold goes as null
	it( 'builds a create with the source, and an update with what changed', ()=>{
		const fresh = new HistGroup( {name: 'Pumps', slug: ' pumps ', serverConnectionId: 1, histTemplateId: 2, root: ' ns=2;i=5001 ', thresholds: {...histThresholdBlank, minTimeInterval: '1000', stepped: 'false'}} );
		expect( fresh.mutation( new HistGroup( {} ) ).map( m=>m.toString() ) ).toEqual( ['createHistGroup( slug:"pumps", name:"Pumps", serverConnectionId:1, histTemplateId:2, root:"ns=2;i=5001", minTimeInterval:1000, stepped:false )'] );
		const adHoc = new HistGroup( {name: 'Ad hoc', slug: 'adhoc', serverConnectionId: 1, histTemplateId: 0} );
		expect( adHoc.mutation( new HistGroup( {} ) ).map( m=>m.toString() ) ).toEqual( ['createHistGroup( slug:"adhoc", name:"Ad hoc", serverConnectionId:1 )'] );
		const original = new HistGroup( row );
		const edited = new HistGroup( {...original, thresholds: {...original.thresholds, maxTimeInterval: '', exceptionDeviation: '2'}, serverConnectionId: 9, root: 'ns=0;i=85'} );
		expect( edited.equals( original ) ).toBe( false );
		expect( edited.mutation( original ).map( m=>m.toString() ) ).toEqual( ['updateHistGroup( id:5, exceptionDeviation:2, maxTimeInterval:null )'] );
		expect( new HistGroup( {...original} ).mutation( original ) ).toEqual( [] );
	} );
	it( 'HistTemplate saves its name and slug', ()=>{
		const t = new HistTemplate( {name: 'Pump', slug: 'pump'} );
		expect( t.canSave ).toBe( true );
		expect( t.mutation( new HistTemplate( {} ) ).map( m=>m.toString() ) ).toEqual( ['createHistTemplate( slug:"pump", name:"Pump" )'] );
		expect( new HistTemplate( t ).equals( t ) ).toBe( true );
	} );
} );

describe( 'hist node text', ()=>{
	//the column's spelling, the user's UA spelling, and neither
	it( 'parses a column, a UA string, and nothing else', ()=>{
		const byUri = parseHistNodeText( '{"nsu":"urn:jde:pumps","i":6012}' )!;
		expect( byUri.nsu ).toBe( 'urn:jde:pumps' );
		expect( byUri.id ).toBe( 6012 );
		const byIndex = parseHistNodeText( ' ns=2;s=pump1 ' )!;
		expect( byIndex.ns ).toBe( 2 );
		expect( byIndex.id ).toBe( 'pump1' );
		expect( byIndex.nsu ).toBeUndefined();
		expect( parseHistNodeText( 'pump1' ) ).toBeUndefined();
		expect( parseHistNodeText( '{"ns":2}' ) ).toBeUndefined();
		expect( parseHistNodeText( '' ) ).toBeUndefined();
	} );
	it( 'spells a node for a mutation, a column and a page', ()=>{
		const node = new ExNodeId( {ns: 2, i: 6012, nsu: 'urn:jde:pumps'} );
		expect( histNodeJson( node ) ).toEqual( {nsu: 'urn:jde:pumps', i: 6012} );
		expect( histNodeText( node ) ).toBe( '{"nsu":"urn:jde:pumps","i":6012}' );
		expect( histNodeLabel( node, 2 ) ).toBe( 'ns=2;i=6012' );
		expect( histNodeLabel( node, undefined ) ).toBe( 'nsu=urn:jde:pumps;i=6012' );
		const indexed = new ExNodeId( {ns: 3, i: 7} );
		expect( histNodeJson( indexed ) ).toEqual( {ns: 3, i: 7} );
		expect( histNodeText( indexed ) ).toBe( 'ns=3;i=7' );
	} );
} );

describe( 'hist admin rows', ()=>{
	it( 'reads a template member, a group node and a resolve answer', ()=>{
		const member = toHistTemplateMember( {id: 3, histTemplateId: 2, path: '[{"nsu":"urn:jde:pumps","name":"motorRpm"}]', minTimeInterval: 1000, stepped: null} );
		expect( member ).toEqual( {id: 3, templateId: 2, path: [{nsu: 'urn:jde:pumps', name: 'motorRpm'}], thresholds: {...histThresholdBlank, minTimeInterval: '1000'}} );
		const node = toHistGroupMember( {id: 7, node: '{"nsu":"urn:jde:pumps","i":6012}', histTemplateNodeId: 3, exceptionDeviation: 5} );
		expect( node.id ).toBe( 7 );
		expect( node.node.nsu ).toBe( 'urn:jde:pumps' );
		expect( node.templateNodeId ).toBe( 3 );
		expect( node.thresholds.exceptionDeviation ).toBe( '5' );
		expect( toHistGroupMember( {id: 8, node: '{"nsu":"u","i":1}', histTemplateNodeId: null} ).templateNodeId ).toBeUndefined();
		const resolved = toHistResolved( {nodes: [{templateNode: 3, path: '[{"nsu":"u","name":"a"}]', node: {nsu: 'u', i: 1}, status: 0, holder: null}, {templateNode: 4, path: '[]', node: null, status: 2153775104, holder: {id: 9, name: 'Other'}}]} );
		expect( resolved[0].node?.nsu ).toBe( 'u' );
		expect( resolved[0].holder ).toBeUndefined();
		expect( resolved[1].node ).toBeUndefined();
		expect( resolved[1].holder ).toEqual( {id: 9, name: 'Other'} );
		expect( toHistResolved( undefined ) ).toEqual( [] );
	} );
} );