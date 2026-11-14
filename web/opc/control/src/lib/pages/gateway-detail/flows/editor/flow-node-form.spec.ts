import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { stable } from 'jde-spa/testing';
import { FlowGraph, FlowNode } from '../../../../model/flow';
import { FlowNodePatch, flowNodeTypes, newFlowNode } from '../../../../model/flow-nodes';
import { FlowFields } from './flow-fields';
import { FlowNodeForm } from './flow-node-form';

const node = ( type:string, props:Record<string,unknown>={}, id='n' ):FlowNode=>({ id, type, name: '', x: 0, y: 0, props, wires: [] });
const create = async ( of:FlowNode, others:FlowNode[]=[] )=>{
	const fixture = TestBed.createComponent( FlowNodeForm );
	const graph:FlowGraph = { nodes: [of, ...others] };
	fixture.componentRef.setInput( 'node', of );
	fixture.componentRef.setInput( 'graph', graph );
	const patches:FlowNodePatch[] = [];
	fixture.componentInstance.patch.subscribe( p=>patches.push( p ) );
	await stable( fixture );
	const element = <HTMLElement>fixture.nativeElement;
	//the field under a label, typed into and left, as a user does
	const type = ( label:string, text:string )=>{
		const field = [...element.querySelectorAll( 'mat-form-field' )].find( f=>f.querySelector( 'mat-label' )?.textContent?.trim()==label )!;
		const input = <HTMLInputElement|HTMLTextAreaElement>field.querySelector( 'input, textarea' );
		input.value = text;
		input.dispatchEvent( new Event( 'change' ) );
	};
	const fields = ()=>fixture.debugElement.queryAll( By.directive( FlowFields ) ).map( d=><FlowFields>d.componentInstance );
	const labels = ()=>[...element.querySelectorAll( 'mat-label, mat-checkbox label, legend' )].map( l=>l.textContent!.trim() );
	return { fixture, element, patches, type, fields, labels };
};

//The side form of each node type (#455):  the fields its type has, and the props an edit makes of them.
describe( 'FlowNodeForm', ()=>{
	//one form, given a node of each type in turn, as the editor's is as the selection moves
	it( "shows every type's fields", async ()=>{
		const { fixture, labels, element } = await create( newFlowNode( 'comment', 0, 0, 'n' ) );
		for( const type of flowNodeTypes ){
			const of = newFlowNode( type.type, 0, 0, 'n' );
			fixture.componentRef.setInput( 'node', of );
			fixture.componentRef.setInput( 'graph', {nodes: [of]} );
			await stable( fixture, 1 );
			const expected = type.fields( of.props ).map( f=>f.label );
			expect( [type.type, expected.filter( label=>!labels().includes( label ) )] ).toEqual( [type.type, []] );
			expect( element.querySelector( 'header h3' )!.textContent ).toBe( type.type );
			expect( labels()[0] ).toBe( 'Name' );
		}
	} );
	it( 'names the node, and sets a text, a number and a flag', async ()=>{
		const { patches, type, fields } = await create( node( 'filter', {band: 1} ) );
		type( 'Name', ' Dead band ' );
		type( 'Property', 'payload.rpm' );
		type( 'Band', '2.5' );
		expect( patches ).toEqual( [{name: 'Dead band'}, {props: {band: 1, property: 'payload.rpm'}}, {props: {band: 2.5}}] );
		const form = fields()[0], flag = ( key:string )=>form.fields().find( f=>f.key==key )!;
		form.setFlag( flag( 'percent' ), true );
		form.setFlag( flag( 'perTopic' ), false );
		form.setFlag( flag( 'perTopic' ), true );//its default:  left out
		expect( patches.slice( 3 ) ).toEqual( [{props: {band: 1, percent: true}}, {props: {band: 1, perTopic: false}}, {props: {band: 1}}] );
		expect( [form.checked( flag( 'percent' ) ), form.checked( flag( 'perTopic' ) )] ).toEqual( [false, true] );
	} );
	//a blank field takes its key away, so the node takes its default:  the gateway refuses an empty text
	it( 'takes a key away for a field left blank', async ()=>{
		const { patches, type } = await create( node( 'filter', {band: 1, property: 'payload.rpm'} ) );
		type( 'Band', ' ' );
		type( 'Property', '' );
		expect( patches ).toEqual( [{props: {property: 'payload.rpm'}}, {props: {band: 1}}] );
	} );
	it( 'sets a choice, a list and a group', async ()=>{
		const { patches, fields, type } = await create( node( 'timer', {at: {times: ['06:00']}} ) );
		const top = fields()[0], at = fields()[1];
		expect( at.fields().map( f=>f.key ) ).toEqual( ['times', 'days', 'zone'] );
		at.setList( at.fields()[1], ['mon', 'fri'] );
		type( 'Times', '06:00, 14:30' );
		type( 'Times', '' );//the group's one key gone:  the group goes
		expect( patches ).toEqual( [{props: {at: {times: ['06:00'], days: ['mon', 'fri']}}}, {props: {at: {times: ['06:00', '14:30']}}}, {props: {}}] );
		expect( top.group( top.fields()[1] ) ).toEqual( {times: ['06:00']} );
		const delay = await ( async ()=>{ TestBed.resetTestingModule(); return create( node( 'delay', {duration: 'PT1S'} ) ); } )();//a second form, of a type whose fields follow a choice
		const form = delay.fields()[0], mode = form.fields()[0];
		expect( [form.choice( mode ), form.unset( mode ), form.label( mode, 'rate' )] ).toEqual( ['', 'Hold each message (default)', 'Limit the rate'] );
		form.setChoice( mode, 'rate' );
		expect( delay.patches ).toEqual( [{props: {duration: 'PT1S', mode: 'rate'}}] );
	} );
	it( "edits a Function node's body in a plain editor", async ()=>{
		const { patches, type, element, fields } = await create( node( 'function', {code: 'return msg;', onStop: 'x'} ) );
		type( 'On message', 'msg.payload *= 2;\nreturn msg;' );
		type( 'Outputs', '2' );
		type( 'On stop', '' );
		type( 'On message', '' );//a body may be empty
		expect( patches ).toEqual( [
			{props: {code: 'msg.payload *= 2;\nreturn msg;', onStop: 'x'}}, {props: {code: 'return msg;', onStop: 'x', outputs: 2}},
			{props: {code: 'return msg;'}}, {props: {code: '', onStop: 'x'}}
		] );
		//Tab indents;  Escape first lets the next Tab leave
		const area = <HTMLTextAreaElement>element.querySelector( '.code textarea' );
		area.value = 'ab';
		area.setSelectionRange( 1, 1 );
		const key = ( k:string )=>{ const e = new KeyboardEvent( 'keydown', {key: k, cancelable: true} ); Object.defineProperty( e, 'target', {value: area} ); fields()[0].onCodeKey( e ); return e.defaultPrevented; };
		expect( [key( 'Tab' ), area.value] ).toEqual( [true, 'a\tb'] );
		expect( [key( 'Escape' ), key( 'Tab' ), area.value] ).toEqual( [false, false, 'a\tb'] );
		expect( key( 'Tab' ) ).toBe( true );
	} );
	//a change rule's fields follow its `t`, and what another `t` left behind goes
	it( "edits a change node's rules", async ()=>{
		const { patches, fields } = await create( node( 'change', {rules: [{t: 'set', p: 'payload', to: 0}]} ) );
		const top = fields()[0], rules = top.fields()[0];
		expect( fields()[1].fields().map( f=>f.key ) ).toEqual( ['t', 'pt', 'p', 'tot', 'to'] );
		top.setRule( rules, 0, {t: 'delete', p: 'payload', to: 0} );
		top.addRule( rules );
		expect( patches ).toEqual( [{props: {rules: [{t: 'delete', p: 'payload'}]}}, {props: {rules: [{t: 'set', p: 'payload', to: 0}, {t: 'set'}]}}] );
	} );
	//a switch's ports are its rules:  a change says where each port was, so its wires follow
	it( "says where a switch's ports went", async ()=>{
		const rules = [{t: 'gt', v: 10}, {t: 'null'}, {t: 'else'}];
		const { patches, fields } = await create( node( 'switch', {rules} ) );
		const top = fields()[0], field = top.fields().find( f=>f.key=='rules' )!;
		top.removeRule( field, 1 );
		top.moveRule( field, 0, 1 );
		top.moveRule( field, 0, -1 );//nowhere to go
		top.setRule( field, 2, {t: 'true'} );
		top.addRule( field );
		expect( patches.map( p=>p.ports ) ).toEqual( [[0, 2], [1, 0, 2], [0, 1, 2], [0, 1, 2, -1]] );
		expect( patches[0].props ).toEqual( {rules: [rules[0], rules[2]]} );
		expect( patches[3].props!['rules'] ).toEqual( [...rules, {t: 'eq'}] );
		top.setChoice( top.fields()[0], '' );//another prop:  the ports stay
		expect( patches.at( -1 ) ).toEqual( {props: {rules}} );
	} );
	it( "offers a catch the graph's other nodes, and says a fault", async ()=>{
		const of = node( 'catch', {scope: ['a']} );
		const { fields, patches, fixture, element } = await create( of, [node( 'function', {code: ''}, 'a' ), node( 'debug', {}, 'b' )] );
		const form = fields()[0], scope = form.fields()[1];
		expect( form.nodes().map( n=>n.id ) ).toEqual( ['a', 'b'] );//not itself
		expect( form.list( scope ) ).toEqual( ['a'] );
		form.setList( scope, [] );
		expect( patches ).toEqual( [{props: {}}] );
		expect( element.querySelector( '.fault' ) ).toBeNull();
		fixture.componentRef.setInput( 'fault', '`scope` names "gone", which is not a node of the graph.' );
		await stable( fixture );
		expect( element.querySelector( '.fault' )!.textContent ).toContain( 'which is not a node of the graph' );
	} );
} );