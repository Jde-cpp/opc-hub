import { NodeId } from '../model/node-id';
import { silence } from 'jde-spa/testing';
import { TestGateway } from 'jde-opc/testing';

//The gateway's two answers for a method:  `method( opc, id )` with each argument's base, and `__type` for an enumeration of the
//server's own - DeviceHealthEnumeration's values, and none for 7000, whose read fails.
class MethodGateway extends TestGateway{
	queries:string[] = [];
	override async ql<Y>( q?:any ):Promise<Y>{
		this.queries.push( q.text );
		if( q.text.startsWith('__type') ){
			if( q.text.includes('7000') )
				throw new Error( 'BadNodeIdUnknown' );
			return <Y>{ __type: {name: 'DeviceHealthEnumeration', enumValues: [{id: 0, name: 'NORMAL'}, {id: 2, name: 'OFF_SPEC'}]} };
		}
		return <Y>{ method: {name: 'configure', inputArguments: [
			{name: 'health', dataType: {ns: 2, i: 6244}, baseDataType: {i: 29}},
			{name: 'mode', dataType: {ns: 2, i: 7000}, baseDataType: {i: 29}},
			{name: 'speed', dataType: {ns: 5, s: 'Speed'}, baseDataType: {i: 11}}], outputArguments: []} };
	}
}

//reviews-issue/318-opc-calls.md #3:  the dialog's arguments come with their bases, and each enumeration of the server's own with its
//values, so the editor is a choice of them;  one whose values can't be read stays the number it is sent as.
describe( 'Gateway.methodArguments', ()=>{
	it( 'asks for each base, and reads the values of each enumeration of the server\'s own', async ()=>{
		const g = new MethodGateway();
		const warn = silence( 'warn' );
		const m = await g.methodArguments( 'cn', new NodeId( <any>{ns: 5, i: 6055} ) );
		expect( warn ).toHaveBeenCalledTimes( 1 );//7000's values
		expect( g.queries[0] ).toContain( 'baseDataType' );
		expect( g.queries.filter( q=>q.startsWith('__type') ) ).toHaveLength( 2 );//the two enumerations, not the Double
		expect( m.inputs.map( a=>a.editor ) ).toEqual( ['enum', 'number', 'number'] );
		expect( m.inputs[0].enum?.enumValues.map( e=>e.name ) ).toEqual( ['NORMAL', 'OFF_SPEC'] );
		expect( m.inputs[1].enum ).toBeUndefined();
	} );
} );