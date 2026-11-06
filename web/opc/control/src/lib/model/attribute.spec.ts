import { Attribute, attributeDefs } from './attribute';
import { NodeView } from './node-view';

//reviews-issue/417-clients-attributes.md #4:  every plain object was a browse name, a node id or json, so a DateTime Value read as raw
//json, and a structure with a `name` or an `i` member as a browse name or a node id
describe( 'Attribute.text', ()=>{
	const text = ( key:string, json:unknown )=>new Attribute( attributeDefs.find( d=>d.key==key )!, json ).text;
	it( 'spells a DateTime, a browse name and a node id by their whole shape', ()=>{
		expect( text('dataValue', {v: {seconds: 1762300000, nanos: 5000000}, sc: 0}) ).toBe( NodeView.time( new Date( 1762300000005 ) ) );//local, as the row's timestamps
		expect( text('browse', {ns: 2, name: 'pump1'}) ).toBe( '2:pump1' );
		expect( text('dataValue', {v: {ns: 0, name: 'x'}, sc: 0}) ).toBe( '0:x' );//a QualifiedName value
		expect( text('id', {ns: 2, i: 5001}) ).toBe( 'ns=2;i=5001' );
		expect( text('dataType', {i: 11}) ).toBe( 'Double' );
		expect( text('dataType', {ns: 2, i: 3002}) ).toBe( 'ns=2;i=3002' );
	} );
	it( 'leaves a structure that only looks like one as its json', ()=>{
		expect( text('dataValue', {v: {name: 'Pump', rpm: 3}, sc: 0}) ).toBe( '{"name":"Pump","rpm":3}' );
		expect( text('dataValue', {v: {s: 'x', count: 1}, sc: 0}) ).toBe( '{"s":"x","count":1}' );
		expect( text('dataValue', {v: {nsu: 'urn:x', i: 5}, sc: 0}) ).toBe( '{"nsu":"urn:x","i":5}' );//an ExpandedNodeId keeps its uri
		expect( text('dataValue', {v: [{name: 'a', n: 1}, {name: 'b', n: 2}], sc: 0}) ).toBe( '{"name":"a","n":1}, {"name":"b","n":2}' );
	} );
} );