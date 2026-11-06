import { NodeId } from './node-id';
import { Variable } from './node';
import { OpcError } from './opc-error';
import { silence } from 'jde-spa/testing';
import { EDataChangeTrigger, EDeadbandType, discardText, filterText, parametersProblem, queueText, samplingText, WatchRow } from './watch';
import { fieldsOf, parametersOf } from '../pages/gateway-detail/watch/watch-parameters-dialog';

const A = new NodeId( {ns: 2, i: 1} );

//Clients 6b (#419, spec *Watch list*):  a row's parameters as the profile keeps them, the cells read them and the dialog edits them.
describe( 'WatchRow', ()=>{
	afterEach( ()=>vi.restoreAllMocks() );
	it( 'round-trips through the profile\'s JSON', ()=>{
		const row = new WatchRow( 'local', A, {samplingInterval: 100, queueSize: 5, discardOldest: false, filter: {trigger: EDataChangeTrigger.StatusValueTimestamp, deadbandType: EDeadbandType.Absolute, deadbandValue: 0.5}} );
		const revived = WatchRow.fromJson( JSON.parse( JSON.stringify( {items: [row.toJson()]} ) ).items[0] );
		expect( revived.opc ).toBe( 'local' );
		expect( revived.node.equals( A ) ).toBe( true );
		expect( revived.parameters ).toEqual( row.parameters );
		expect( revived.key ).toBe( row.key );
	} );

	it( 'leaves a persisted parameter out that is no number', ()=>{
		const revived = WatchRow.fromJson( <any>{opc: 'local', node: A.toJson(), parameters: {samplingInterval: "fast", queueSize: 2, filter: {deadbandType: "1", deadbandValue: 1}}} );
		expect( revived.parameters ).toEqual( {queueSize: 2} );
		expect( WatchRow.fromJson( <any>{opc: 'local', node: A.toJson()} ).parameters ).toEqual( {} );
	} );
	//one the gateway refuses refused the connection's whole Subscribe, and a queue size the encoder refuses threw (419-watch #10)
	it( 'leaves a persisted parameter out that the gateway would refuse, keeping the rest', ()=>{
		const warn = silence();
		const parameters = ( p:object )=>WatchRow.fromJson( <any>{opc: 'local', node: A.toJson(), parameters: p} ).parameters;
		expect( parameters( {samplingInterval: 100, queueSize: -1} ) ).toEqual( {samplingInterval: 100} );
		expect( warn ).toHaveBeenCalledWith( "A watch row's queueSize is left out:  The queue size is a whole number." );
		expect( parameters( {queueSize: 1.5, discardOldest: false} ) ).toEqual( {discardOldest: false} );
		expect( parameters( {queueSize: 2**32} ) ).toEqual( {} );
		expect( parameters( {queueSize: 3, filter: {trigger: 7, deadbandType: 1, deadbandValue: 1}} ) ).toEqual( {queueSize: 3} );
		expect( parameters( {filter: {deadbandType: 5, deadbandValue: 1}} ) ).toEqual( {} );
		expect( parameters( {filter: {deadbandType: 1, deadbandValue: -1}} ) ).toEqual( {} );
		expect( parameters( {filter: {deadbandType: 2, deadbandValue: 101}} ) ).toEqual( {} );
		expect( parameters( {filter: {trigger: 2, deadbandType: 2, deadbandValue: 100}} ) ).toEqual( {filter: {trigger: 2, deadbandType: 2, deadbandValue: 100}} );
	} );

	it( 'is named by the node described, else by its id', ()=>{
		const row = new WatchRow( 'local', A, {} );
		expect( row.name ).toBe( 'ns=2;i=1' );
		row.describe( new Variable( <any>{ns: 2, i: 1, name: 'rpm', dataType: {i: 11}, value: 7} ), 'Objects/pump1/rpm' );
		expect( row.name ).toBe( 'rpm' );
		expect( row.variable ).toBeDefined();
		expect( row.value ).toBe( 7 );//the describing read's reading, until the first push
		expect( row.sc ).toBe( 0 );
		expect( row.error ).toBeUndefined();
	} );

	it( 'says why a node that is no variable has nothing to watch', ()=>{
		const row = new WatchRow( 'local', A, {} );
		row.describe( <any>{ nodeClass: 1, name: 'pump1', isVariable: false }, 'Objects/pump1' );
		expect( row.error ).toContain( 'Object' );
		expect( row.variable ).toBeUndefined();
	} );

	//a refused subscribe's OpcError became the value, and a Bad push without one blanked the last value and its times (419-watch #6)
	it( 'keeps the last value through a refused subscribe and a Bad reading without one', ()=>{
		const row = new WatchRow( 'local', A, {} );
		const source = new Date( 1000 ), server = new Date( 1001 );
		row.setReading( {value: 7, sc: 0, source, server} );
		row.setReading( {value: new OpcError( 0x80340000, "Subscribe", "", undefined ), sc: 0x80340000} );
		expect( row.value ).toBe( 7 );
		expect( row.sc ).toBe( 0x80340000 );
		const bad = new Date( 2000 );
		row.setReading( {sc: 0x80310000, source: bad, server: bad} );
		expect( row.value ).toBe( 7 );
		expect( row.sourceTime ).toBe( bad );//the Bad reading's own
		row.setReading( {value: 8, sc: 0, source, server} );
		row.setReading( {value: 8, sc: 0} );//the describing read of what the push showed keeps the push's times
		expect( row.sourceTime ).toBe( source );
	} );

	it( 'reads the cells from what the row asks, the defaults where it asks nothing', ()=>{
		const defaults = new WatchRow( 'local', A, {} );
		expect( [samplingText( defaults.parameters ), queueText( defaults.parameters ), discardText( defaults.parameters ), filterText( defaults.parameters )] ).toEqual( ['500', '1', 'oldest', ''] );
		const asked = new WatchRow( 'local', A, {samplingInterval: 0, queueSize: 10, discardOldest: false, filter: {deadbandType: EDeadbandType.Percent, deadbandValue: 5}} );
		expect( [samplingText( asked.parameters ), queueText( asked.parameters ), discardText( asked.parameters ), filterText( asked.parameters )] ).toEqual( ['0', '10', 'newest', 'StatusValue, 5% percent'] );
		expect( filterText( {filter: {trigger: EDataChangeTrigger.Status, deadbandType: EDeadbandType.None, deadbandValue: 0}} ) ).toBe( 'Status, no deadband' );
	} );
} );

describe( 'WatchParametersDialog fields', ()=>{
	it( 'spell the parameters and read them back', ()=>{
		const p = { samplingInterval: 100, queueSize: 5, discardOldest: false, filter: {trigger: EDataChangeTrigger.StatusValueTimestamp, deadbandType: EDeadbandType.Absolute, deadbandValue: 0.5} };
		const fields = fieldsOf( p );
		expect( fields ).toEqual( {samplingInterval: '100', queueSize: '5', discardOldest: 'false', trigger: '2', deadbandType: '1', deadbandValue: '0.5'} );
		expect( parametersOf( fields ) ).toEqual( p );
	} );

	it( 'leave a blank field out, and send no filter for the server\'s default', ()=>{
		expect( fieldsOf( {} ) ).toEqual( {samplingInterval: '', queueSize: '', discardOldest: '', trigger: '', deadbandType: '0', deadbandValue: '0'} );
		expect( parametersOf( fieldsOf( {} ) ) ).toEqual( {} );
		//a trigger alone is a filter with no deadband;  a deadband alone leaves the trigger to the server
		expect( parametersOf( {...fieldsOf( {} ), trigger: '0'} ) ).toEqual( {filter: {trigger: EDataChangeTrigger.Status, deadbandType: EDeadbandType.None, deadbandValue: 0}} );
		expect( parametersOf( {...fieldsOf( {} ), deadbandType: '2', deadbandValue: '10'} ) ).toEqual( {filter: {deadbandType: EDeadbandType.Percent, deadbandValue: 10}} );
	} );

	it( 'name what the gateway would refuse', ()=>{
		expect( parametersProblem( parametersOf( {...fieldsOf( {} ), samplingInterval: 'fast'} ) ) ).toContain( 'sampling' );
		expect( parametersProblem( parametersOf( {...fieldsOf( {} ), queueSize: '1.5'} ) ) ).toContain( 'queue' );
		expect( parametersProblem( parametersOf( {...fieldsOf( {} ), deadbandType: '1', deadbandValue: '-1'} ) ) ).toContain( 'deadband' );
		expect( parametersProblem( parametersOf( {...fieldsOf( {} ), deadbandType: '2', deadbandValue: '101'} ) ) ).toContain( '100' );
		expect( parametersProblem( parametersOf( {...fieldsOf( {} ), deadbandType: '2', deadbandValue: '100'} ) ) ).toBeUndefined();
		expect( parametersProblem( {} ) ).toBeUndefined();
	} );
} );