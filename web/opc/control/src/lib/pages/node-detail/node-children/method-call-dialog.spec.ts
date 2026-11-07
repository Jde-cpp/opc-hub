import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { flush, stable } from 'jde-spa/testing';
import { MAT_DIALOG_DATA, MatDialogRef } from '@angular/material/dialog';
import { Enum } from '../../../model/enum';
import { CallResult, MethodArguments } from '../../../model/method';
import { NodeId } from '../../../model/node-id';
import { Method, OpcObject } from '../../../model/node';
import { StatusCode } from '../../../model/types';
import { Value } from '../../../model/value';
import { ValueEditor } from '../value-editor/value-editor';
import { MethodCallDialog, MethodCallDialogData } from './method-call-dialog';

const pump = new OpcObject( <any>{ns: 2, i: 5005, name: 'Pump (Manual)', browse: {ns: 2, name: 'pumpManual'}} );
const toHertz = new Method( <any>{ns: 2, i: 6055, name: 'toHertz', browse: {ns: 2, name: 'toHertz'}, description: {text: 'Converts rpm to Hz.'}}, pump );
const declared = new MethodArguments( {name: 'toHertz',
	inputArguments: [{name: 'rpm', dataType: {i: 11}, valueRank: -1, description: 'The motor speed, in rpm.'}],
	outputArguments: [{name: 'hertz', dataType: {i: 11}}, {name: 'text', dataType: {i: 12}}]} );
const BadInvalidArgument = 0x80AB0000, BadTypeMismatch = 0x80740000, BadUserAccessDenied = 0x801F0000;

//The dialog over a stub gateway:  `arguments` answers the method's declaration, `calls` is what it sent, `answerWith` sets the
//result each call gets, `names` the status codes it asked the names of, and `editors` the ValueEditor per input.
const open = async ( data:Partial<MethodCallDialogData>={} )=>{
	let answer:CallResult|Error = { statusCode: 0, inputArgumentResults: [], outputArguments: [] };
	const call = vi.fn( async ( _args:Value[] )=>{ if( answer instanceof Error ) throw answer; return answer; } );
	const names = vi.fn( ( _codes:StatusCode[] )=>{} );
	const ref = { disableClose: <boolean|undefined>undefined, close: vi.fn( ( _r?:CallResult )=>{} ) };
	TestBed.resetTestingModule();//a second dialog in one test
	TestBed.configureTestingModule({ providers: [
		{ provide: MatDialogRef, useValue: ref },
		{ provide: MAT_DIALOG_DATA, useValue: <MethodCallDialogData>{ object: pump, method: toHertz, arguments: async ()=>declared, call, names, ...data } }
	]});
	const fixture = TestBed.createComponent( MethodCallDialog );
	const dialog = fixture.componentInstance;
	await stable( fixture, 2 );//ngOnInit's load, then the arguments rendered:  the only load - an explicit one beside it reset what a test typed when the second landed
	return { fixture, dialog, ref,
		editors: ()=>fixture.debugElement.queryAll( By.directive( ValueEditor ) ).map( d=><ValueEditor>d.componentInstance ),
		calls: ()=>call.mock.calls.map( ([args])=>args ),
		names: ()=>names.mock.calls.map( ([codes])=>codes ),
		answerWith: ( a:CallResult|Error )=>{ answer = a; } };
};

describe( 'MethodCallDialog', ()=>{
	it( 'builds an editor per input argument from the declaration, and sends nothing until each has a value', async ()=>{
		const d = await open();
		expect( d.dialog.title ).toBe( 'Pump (Manual).toHertz' );
		expect( d.editors().map( e=>e.kind() ) ).toEqual( ['number'] );
		expect( d.dialog.label( d.dialog.inputs[0] ) ).toBe( 'rpm (Double)' );
		expect( d.dialog.problem() ).toBe( 'Enter rpm.' );
		await d.dialog.apply();
		expect( d.calls() ).toHaveLength( 0 );
		expect( d.fixture.nativeElement.textContent ).toContain( 'Converts rpm to Hz.' );
	} );
	//reviews-issue/318-opc-calls.md W1:  open62541 answers a refused call with an empty value per declared output, which the dialog
	//listed as the outputs' names over nothing;  a Bad status is the whole answer
	it( 'lists no outputs for a refused call, and an empty array as one', async ()=>{
		const d = await open();
		d.dialog.setValue( 0, 1500 );
		d.answerWith( {statusCode: 0x80280000, inputArgumentResults: [], outputArguments: [[], []]} );//BadSubscriptionIdInvalid
		await d.dialog.apply();
		expect( d.dialog.outputs() ).toEqual( [] );
		d.fixture.detectChanges();
		expect( d.fixture.nativeElement.querySelector( 'dl.outputs' ) ).toBeNull();
		d.answerWith( {statusCode: 0, inputArgumentResults: [], outputArguments: [[], 'none']} );
		await d.dialog.apply();
		expect( d.dialog.outputs() ).toEqual( [{name: 'hertz', text: '[]'}, {name: 'text', text: 'none'}] );
	} );
	//reviews-issue/318-opc-calls.md #3:  an enumeration of the server's own is a choice of its values, sent as the id, and an output of
	//one reads by name
	it( 'offers an enumeration\'s values and sends the one chosen', async ()=>{
		const health = new Enum( new NodeId( <any>{ns: 2, i: 6244} ), {name: 'DeviceHealthEnumeration', enumValues: [{id: 0, name: 'NORMAL'}, {id: 2, name: 'OFF_SPEC'}]} as any );
		const arguments_ = new MethodArguments( {name: 'setHealth', inputArguments: [{name: 'health', dataType: {ns: 2, i: 6244}, baseDataType: {i: 29}}], outputArguments: [{name: 'was', dataType: {ns: 2, i: 6244}, baseDataType: {i: 29}}]} );
		arguments_.inputs[0].enum = arguments_.outputs[0].enum = health;
		const d = await open( {arguments: async ()=>arguments_} );
		expect( d.editors()[0].kind() ).toBe( 'enum' );
		expect( d.dialog.label( d.dialog.inputs[0] ) ).toBe( 'health (DeviceHealthEnumeration)' );
		expect( d.fixture.nativeElement.querySelector( 'mat-select' ) ).not.toBeNull();
		expect( d.dialog.problem() ).toBe( 'Enter health.' );
		d.dialog.setValue( 0, 2 );
		d.answerWith( {statusCode: 0, inputArgumentResults: [], outputArguments: [0]} );
		await d.dialog.apply();
		expect( d.calls() ).toEqual( [[2]] );
		expect( d.dialog.outputs() ).toEqual( [{name: 'was', text: 'NORMAL'}] );
	} );
	//reviews-issue/318-opc-calls.md #9:  the spec loaded the arguments itself and then ran ngOnInit, which loaded them again
	it( 'reads the arguments once, and keeps what was typed while the page awaits', async ()=>{
		const read = vi.fn( async ()=>declared );
		const d = await open( {arguments: read} );
		d.dialog.setValue( 0, 1500 );
		await flush();
		await d.dialog.apply();
		expect( read ).toHaveBeenCalledTimes( 1 );
		expect( d.calls() ).toEqual( [[1500]] );
	} );
	it( 'calls with the arguments typed and shows the status and the outputs by name', async ()=>{
		const d = await open();
		d.dialog.setValue( 0, 1500 );
		expect( d.dialog.problem() ).toBeUndefined();
		d.answerWith( {statusCode: 0, inputArgumentResults: [], outputArguments: [25, '25 Hz']} );
		await d.dialog.apply();
		expect( d.calls() ).toEqual( [[1500]] );
		expect( d.dialog.outputs() ).toEqual( [{name: 'hertz', text: '25'}, {name: 'text', text: '25 Hz'}] );
		expect( d.dialog.argumentResults() ).toEqual( [] );
		expect( d.names() ).toEqual( [] );//Good needs no name
		expect( d.ref.close ).not.toHaveBeenCalled();//open for another go
		expect( d.ref.disableClose ).toBe( false );
	} );
	it( 'shows a refusal with each argument\'s status, by name once fetched', async ()=>{
		const d = await open();
		d.dialog.setValue( 0, 1500 );
		d.answerWith( {statusCode: BadInvalidArgument, inputArgumentResults: [BadTypeMismatch], outputArguments: []} );
		await d.dialog.apply();
		expect( d.dialog.argumentResults() ).toEqual( [{name: 'rpm', status: BadTypeMismatch}] );
		expect( d.dialog.outputs() ).toEqual( [] );
		expect( d.names() ).toEqual( [[BadInvalidArgument, BadTypeMismatch]] );
		d.fixture.detectChanges();
		const list = d.fixture.nativeElement.querySelector( 'ul.results' );
		expect( list.getAttribute('role') ).toBeNull();//reviews-issue/318-opc-calls.md #15:  a list, so its items are list items to axe
		expect( list.parentElement.getAttribute('role') ).toBe( 'alert' );
		expect( list.querySelectorAll('li') ).toHaveLength( 2 );
		d.answerWith( {statusCode: BadUserAccessDenied, inputArgumentResults: [], outputArguments: []} );
		await d.dialog.apply();
		expect( d.names().at( -1 ) ).toEqual( [BadUserAccessDenied] );
	} );
	it( 'shows the gateway\'s refusal before any call, and the arguments\' when they cannot be read', async ()=>{
		const d = await open();
		d.dialog.setValue( 0, 1500 );
		d.answerWith( new Error( 'BadNodeIdUnknown' ) );
		await d.dialog.apply();
		expect( d.dialog.error() ).toBe( 'BadNodeIdUnknown' );
		expect( d.dialog.result() ).toBeUndefined();
		const unread = await open( {arguments: async ()=>{ throw new Error( 'BadNodeIdUnknown' ); }} );
		expect( unread.dialog.loadError() ).toBe( 'BadNodeIdUnknown' );
		expect( unread.dialog.problem() ).toBe( 'The arguments could not be read.' );
	} );
	it( 'takes a method with no arguments, and a toggle for a Boolean', async ()=>{
		const none = await open( {arguments: async ()=>new MethodArguments( {name: 'reset'} )} );
		expect( none.dialog.problem() ).toBeUndefined();
		await none.dialog.apply();
		expect( none.calls() ).toEqual( [[]] );
		const flag = await open( {arguments: async ()=>new MethodArguments( {name: 'run', inputArguments: [{name: 'on', dataType: {i: 1}}, {name: 'label', dataType: {i: 12}}]} )} );
		expect( flag.editors().map( e=>e.kind() ) ).toEqual( ['boolean', 'text'] );
		expect( flag.dialog.problem() ).toBeUndefined();//a toggle holds false, and text may be blank
		flag.editors()[0].setFlag( true );
		flag.editors()[1].setText( 'x' );
		await flag.dialog.apply();
		expect( flag.calls() ).toEqual( [[true, 'x']] );
	} );
} );